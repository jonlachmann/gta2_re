#!/usr/bin/env python3
# Port check (docs/port.md, M2): the field layout of every state log entry, from the DWARF of gta2_port.exe
# (llvm-dwarfdump). Used for two things:
#  - padding masks: clang may store junk into padding (a byte field written as a dword), where 10.5.exe leaves
#    zeroes, so the state log skips padding bytes (GTA2_STATELOG_MASK, written by write_mask)
#  - field names: name(entry, offset) gives the field at an offset, e.g. *gPedPool_6787B8+0x23c ->
#    field_0_pool.field_4_pool[0].field_23C_...
# usage: layout.py <exe> <table> <mask out>      (or import it: load(exe, table))
import bisect, json, os, re, subprocess, sys

DWARFDUMP = os.environ.get('LLVM_DWARFDUMP', '/opt/homebrew/opt/llvm/bin/llvm-dwarfdump')
KEEP = ('DW_AT_name', 'DW_AT_type', 'DW_AT_byte_size', 'DW_AT_data_member_location', 'DW_AT_data_bit_offset',
        'DW_AT_bit_size', 'DW_AT_upper_bound', 'DW_AT_count', 'DW_AT_declaration', 'DW_AT_artificial')
DIE = re.compile(r'^0x([0-9a-f]+):( +)(DW_TAG_\w+|NULL)')
ATTR = re.compile(r'^ +(DW_AT_\w+)\t\((.*)\)$')

def parse(exe):
    """{offset: [tag, attrs, children]} for the type DIEs, and the named variables {name: type offset}."""
    dies, variables, stack = {}, {}, []
    cur = None
    out = subprocess.run([DWARFDUMP, '--debug-info', exe], capture_output=True, text=True, check=True).stdout
    for line in out.split('\n'):
        m = DIE.match(line)
        if m:
            depth = len(m.group(2))
            while stack and stack[-1][0] >= depth:
                stack.pop()
            if m.group(3) == 'NULL':
                cur = None
                continue
            cur = [m.group(3), {}, []]
            off = int(m.group(1), 16)
            dies[off] = cur
            if stack:
                stack[-1][1][2].append(off)
            stack.append((depth, cur))
            continue
        m = ATTR.match(line)
        if m and cur is not None and m.group(1) in KEEP:
            cur[1][m.group(1)] = m.group(2)
    for off, (tag, attrs, _) in dies.items():
        if tag == 'DW_TAG_variable' and 'DW_AT_name' in attrs and 'DW_AT_type' in attrs:
            variables.setdefault(attrs['DW_AT_name'].strip('"'), int(attrs['DW_AT_type'].split()[0], 16))
    return dies, variables

def num(v):
    return int(v.split()[0], 0)

class Layout:
    def __init__(self, exe):
        self.dies, self.variables = parse(exe)
        self.complete = {}  # class name -> a defining DIE, for pointers to a declaration
        for off, (tag, attrs, _) in self.dies.items():
            if tag in ('DW_TAG_structure_type', 'DW_TAG_class_type', 'DW_TAG_union_type') and 'DW_AT_declaration' not in attrs \
                    and 'DW_AT_name' in attrs:
                self.complete.setdefault(attrs['DW_AT_name'], off)

    def strip(self, off):
        """Follows typedefs, cv and declarations to the type itself."""
        while True:
            tag, attrs, _ = self.dies[off]
            if tag in ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type') and 'DW_AT_type' in attrs:
                off = num(attrs['DW_AT_type'])
            elif 'DW_AT_declaration' in attrs and attrs.get('DW_AT_name') in self.complete:
                off = self.complete[attrs['DW_AT_name']]
            else:
                return off

    def size(self, off):
        tag, attrs, children = self.dies[self.strip(off)]
        if 'DW_AT_byte_size' in attrs:
            return num(attrs['DW_AT_byte_size'])
        if tag == 'DW_TAG_array_type':
            n = 1
            for c in children:
                n *= self.count(c)
            return n * self.size(num(attrs['DW_AT_type']))
        if tag == 'DW_TAG_pointer_type':
            return 4
        return 0

    def count(self, sub):
        a = self.dies[sub][1]
        return num(a['DW_AT_count']) if 'DW_AT_count' in a else num(a['DW_AT_upper_bound']) + 1 if 'DW_AT_upper_bound' in a else 0

    def fields(self, off, base=0, prefix='', out=None):
        """[(offset, size, name)] of the leaf fields of a type."""
        out = [] if out is None else out
        off = self.strip(off)
        tag, attrs, children = self.dies[off]
        if tag in ('DW_TAG_structure_type', 'DW_TAG_class_type', 'DW_TAG_union_type'):
            for c in children:
                ctag, ca, _ = self.dies[c]
                if ctag == 'DW_TAG_inheritance':
                    self.fields(num(ca['DW_AT_type']), base + num(ca.get('DW_AT_data_member_location', '0')), prefix, out)
                elif ctag == 'DW_TAG_member' and 'DW_AT_declaration' not in ca:
                    name = prefix + ca.get('DW_AT_name', '"?"').strip('"')
                    if 'DW_AT_data_bit_offset' in ca:
                        bit = num(ca['DW_AT_data_bit_offset'])
                        out.append((base + bit // 8, (bit % 8 + num(ca['DW_AT_bit_size']) + 7) // 8, name))
                    else:
                        self.fields(num(ca['DW_AT_type']), base + num(ca.get('DW_AT_data_member_location', '0')), name + '.', out)
        elif tag == 'DW_TAG_array_type':
            dims = [self.count(c) for c in children]
            elem = num(attrs['DW_AT_type'])
            esize = self.size(elem)
            for d in dims[1:]:
                esize *= d
            plain = self.dies[self.strip(elem)][0] not in ('DW_TAG_structure_type', 'DW_TAG_class_type', 'DW_TAG_union_type', 'DW_TAG_array_type')
            if plain or len(dims) > 1:
                out.append((base, esize * (dims[0] if dims else 0), prefix.rstrip('.') + '[]'))
            else:
                for i in range(dims[0] if dims else 0):
                    self.fields(elem, base + i * esize, '%s[%d].' % (prefix.rstrip('.'), i), out)
        else:
            out.append((base, self.size(off), prefix.rstrip('.')))
        return out

    def entry(self, name):
        """Fields of a state log entry: a global, or *global for what it points to."""
        var = self.variables.get(name.lstrip('*'))
        if var is None:
            return None
        if name.startswith('*'):
            var = self.strip(var)
            if self.dies[var][0] != 'DW_TAG_pointer_type' or 'DW_AT_type' not in self.dies[var][1]:
                return None
            var = num(self.dies[var][1]['DW_AT_type'])
        return sorted(self.fields(var))

    def signature(self, name):
        """(sizes of the stack arguments, -4 for pointers; size of the return value, negative for a class)."""
        for off, (tag, attrs, children) in self.dies.items():
            if tag == 'DW_TAG_subprogram' and attrs.get('DW_AT_name') == '"%s"' % name:
                ret = self.size(num(attrs['DW_AT_type'])) if 'DW_AT_type' in attrs else 0
                if 'DW_AT_type' in attrs and self.dies[self.strip(num(attrs['DW_AT_type']))][0] in (
                        'DW_TAG_structure_type', 'DW_TAG_class_type', 'DW_TAG_union_type'):
                    ret = -ret  # a class: VC6 returns it through a hidden first argument
                # pointers and references as -4: compared as pointers, whatever range they point into
                # and classes by value as -size: each compiler passes those its own way (a copy or a pointer to one)
                def kind(t):
                    tag = self.dies[self.strip(t)][0]
                    if tag in ('DW_TAG_pointer_type', 'DW_TAG_reference_type', 'DW_TAG_rvalue_reference_type'):
                        return -4
                    if tag in ('DW_TAG_structure_type', 'DW_TAG_class_type', 'DW_TAG_union_type') and self.size(t) > 4:
                        return -self.size(t)
                    return self.size(t)
                args = [kind(num(self.dies[c][1]['DW_AT_type'])) for c in children
                        if self.dies[c][0] == 'DW_TAG_formal_parameter' and 'DW_AT_artificial' not in self.dies[c][1]]
                return args, ret
        return None

    def this_fields(self, name):
        """Fields of the class a member function's this points to, or None."""
        for off, (tag, attrs, children) in self.dies.items():
            if tag == 'DW_TAG_subprogram' and attrs.get('DW_AT_name') == '"%s"' % name:
                for c in children:
                    ca = self.dies[c][1]
                    if self.dies[c][0] == 'DW_TAG_formal_parameter' and 'DW_AT_artificial' in ca:
                        ptr = self.strip(num(ca['DW_AT_type']))
                        return sorted(self.fields(num(self.dies[ptr][1]['DW_AT_type'])))
                return None
        return None

def padding(fields, size):
    """[(offset, length)] of the bytes no field covers."""
    used = bytearray(size)
    for off, n, _ in fields:
        used[off:off + n] = b'\1' * len(used[off:off + n])
    out, i = [], 0
    while i < size:
        if not used[i]:
            j = i
            while j < size and not used[j]:
                j += 1
            out.append((i, j - i))
            i = j
        else:
            i += 1
    return out

def load(exe, table):
    """{entry name: [(offset, size, field)]}, cached next to the table."""
    cache = table + '.layout.json'
    if os.path.exists(cache) and os.path.getmtime(cache) > os.path.getmtime(exe):
        return {k: [tuple(f) for f in v] for k, v in json.load(open(cache)).items()}
    lay, out = Layout(exe), {}
    for line in open(table):
        og, size, pointee, name = line.split()
        for e, n in ((name, int(size)), ('*' + name, int(pointee))):
            if n:
                f = lay.entry(e)
                if f:
                    out[e] = f
    json.dump(out, open(cache, 'w'))
    return out

def name(layout, entry, offset):
    fields = layout.get(entry) or []
    i = bisect.bisect_right([f[0] for f in fields], offset) - 1
    if i >= 0 and fields[i][0] <= offset < fields[i][0] + fields[i][1]:
        return fields[i][2]
    return '(padding)' if fields else '?'

def write_mask(exe, table, path):
    lay = load(exe, table)
    sizes = {}
    for line in open(table):
        og, size, pointee, n = line.split()
        sizes[n], sizes['*' + n] = int(size), int(pointee)
    with open(path, 'w') as f:
        for e, fields in lay.items():
            for off, n in padding(fields, sizes[e]):
                f.write('%s %x %x\n' % (e, off, n))

if __name__ == '__main__':
    assert padding([(0, 1, 'a'), (4, 4, 'b')], 10) == [(1, 3), (8, 2)]
    write_mask(*sys.argv[1:4])
