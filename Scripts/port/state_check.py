#!/usr/bin/env python3
# Port check (docs/port.md, M2): the attract replay in gta2_port.exe and 10.5.exe, headless and sped up,
# compared by their state logs (Port/headless/state_log.cpp, Scripts/port/state_diff.py). Padding is masked
# (Scripts/port/layout.py). When a chunk becomes different, both run again with a dump of those entries and
# the differing fields are printed by name.
# usage: state_check.py [until game frame, default 600]
#   env GTA2_DIR (the install), SPEED (default 10), ATTRACT_MS (default 3000), DUMP (GTA2_STATELOG_DUMP for all runs)
#   SECONDS=<n> runs each exe n s (whatever the frame) and DEMO=<n> compares the nth attract demo
#   TRACE_THIS=<n>: with the trace, the first n bytes of *this at each call, compared field by field
#   TRACE_FIELDS=<regex>: only those fields of *this
#   TRACE=<a>-<b>:<name>,..  instead: both exes with a call trace of those functions (names or address parts as in
#       og_function_data_v105.csv) at game frames a..b, then the first call or return that differs
# Needs build_port/gta2_port.exe (Scripts/port/build.sh) and build_port/gl_clang (build_gl_dlls_clang.sh).
import difflib, os, re, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout
from common import DEMO, GAME, IGNORE, ROOT, demos, looks_like_pointer

UNTIL = int(sys.argv[1]) if len(sys.argv) > 1 else 600
ATTRACT_MS = os.environ.get('ATTRACT_MS', '3000')
BASE = dict(os.environ, WINEPREFIX=os.path.join(ROOT, 'build_port', 'wineprefix'), WINEDEBUG='-all',
            GTA2_SPEED=os.environ.get('SPEED', '10'), GTA2_PORT_ATTRACT_MS=ATTRACT_MS, GTA2_LOCKSTEP='1')
# what state_diff.py ignores is no pointer target in the logs either (Port/headless/state_log.cpp)
BASE['GTA2_STATELOG_IGNORE'] = IGNORE
if os.environ.get('DUMP'):
    BASE['GTA2_STATELOG_DUMP'] = os.environ['DUMP']
STALL = int(os.environ.get('STALL', '20'))  # seconds without log progress before a run counts as dead (startup included)

EXE = os.path.join(ROOT, 'build_port', 'gta2_port.exe')
OG_EXE = os.path.join(GAME, 'gta2_105_attract.exe')
TABLE = os.path.join(GAME, 'state_port.log.globals')
MASK = os.path.join(GAME, 'state.mask')
PORT = {'GTA2_HEADLESS': '1'}
OG = {'RENDERER': os.path.join(ROOT, 'build_port', 'gl_clang'), 'GTA2_VIDEO_DLL': 'nullvideo.dll',
      'GTA2_STATELOG_TABLE': 'state_port.log.globals'}


def last_frame(log):
    try:
        with open(log, 'rb') as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 65536))
            lines = f.read().split(b'\n')
        for line in reversed(lines):
            parts = line.split()
            if len(parts) > 2 and parts[0] == b'F':
                return int(parts[2])
    except OSError:
        pass
    return -1


def run(exe, log, extra, until=None):
    until = UNTIL if until is None else until
    path = os.path.join(GAME, log)
    for suffix in ('', '.dump', '.trace'):
        if os.path.exists(path + suffix):
            os.remove(path + suffix)
    subprocess.run(['wineserver', '-k'], env=BASE)
    env = dict(BASE, GTA2_STATELOG=log, **extra)
    start = time.time()
    out = open(path + '.out', 'w')  # wine's messages, such as a crash's "Unhandled page fault"
    p = subprocess.Popen([os.path.join(ROOT, 'Scripts', 'port', 'run_wine.sh'), exe], env=env, stdout=out, stderr=out)
    # A crash opens winedbg's dialog and the game sits there: stop when the log stops growing
    size, moved, note = -1, time.time(), ''
    limit = float(os.environ.get('SECONDS', '600'))  # SECONDS=n: stop each run after n s (later demos, DEMO=)
    while (last_frame(path) < until or os.environ.get('SECONDS')) and p.poll() is None and time.time() - start < limit:
        time.sleep(0.5)
        now = os.path.getsize(path) if os.path.exists(path) else 0
        if now != size:
            size, moved = now, time.time()
        elif time.time() - moved > STALL:
            note = ' (STALLED: no log progress for %d s, crashed or hung)' % STALL
            break
    subprocess.run(['wineserver', '-k'], env=BASE)
    p.wait()
    out.close()
    crash = [l.strip() for l in open(path + '.out', errors='replace') if 'Unhandled' in l]
    note += (' CRASHED: ' + crash[0]) if crash else ''
    print('%s: up to game frame %d in %.0f s%s' % (os.path.basename(exe), last_frame(path), time.time() - start, note))
    return path


# ---- TRACE=: the call trace

def trace_targets(names):
    """The functions to trace, as label=address lists for 10.5.exe and for the port (from its symbols, which carry
    the original address)."""
    csv = [l.split(',') for l in open(os.path.join(ROOT, 'Scripts', 'bin_comp', 'og_function_data_v105.csv'))]
    nm = subprocess.run(['i686-w64-mingw32-nm', EXE], capture_output=True, text=True).stdout.split('\n')
    syms = [l.split() for l in nm if len(l.split()) == 3 and l.split()[1] in 'Tt']
    og, port = [], []
    for n in names.split(','):
        if n.endswith('::*'):  # every function of a class
            rows = [r for r in csv if len(r) > 1 and r[0].startswith(n[:-1])]
        else:
            rows = [r for r in csv if len(r) > 1 and (r[0].endswith(n) or r[1].lower() == '0x' + n.lower())][:1]
            if not rows:
                print('not in og_function_data_v105.csv, left out: ' + n)
        for row in rows:
            tag = row[1][2:].upper()
            sym = next((x[0] for x in syms if tag in x[2].upper()), None)
            if sym is None:
                continue  # not in the port as a function of its own (inlined, or a thunk)
            og.append('%s=%s' % (row[0].split('::')[-1], row[1][2:]))
            port.append('%s=%s' % (row[0].split('::')[-1], sym))
    return og, port


def this_diff(lay, this_layout, x, y):
    """The fields of *this that differ between two call lines, by name (padding and pointers left out)."""
    if '|' not in x or '|' not in y:
        return []
    only_fields = re.compile(os.environ.get('TRACE_FIELDS', ''))  # TRACE_FIELDS=<regex>: compare only these of *this
    name = x.split()[2]
    if name not in this_layout:
        this_layout[name] = lay.this_fields(name) or []
    da, db = (bytes.fromhex(''.join(w[6:8] + w[4:6] + w[2:4] + w[0:2] for w in l.split('|')[1].split())) for l in (x, y))
    out = []
    for off, n, f, *_ in this_layout[name]:
        if 'unused' in f or '_pad' in f or not only_fields.search(f):  # junk the original never reads
            continue
        if off + n > min(len(da), len(db)) or n > 4096:
            continue
        for o in range(off, off + n, 4):  # arrays dword by dword, each masked if it is a pointer in both
            w = min(4, off + n - o)
            va, vb = int.from_bytes(da[o:o + w], 'little'), int.from_bytes(db[o:o + w], 'little')
            if va != vb and not (w == 4 and looks_like_pointer(va) and looks_like_pointer(vb)):
                out.append('%s (+0x%x 10.5 %x port %x)' % (f, o, va, vb))
    return out


def mask(sigs, line, og):
    """A trace line as the two sides can agree on it: the arguments and the return value by their sizes, pointers as
    'ptr'."""
    line = line.split('|')[0]
    f = [w for w in line.split() if not w.startswith('^')]  # ^esp: see balance()
    if len(f) < 4:
        return line
    sig = sigs.get(f[2]) or ([4] * 4, 4)
    if sig[1] < 0:  # a class return: in 10.5.exe a hidden first argument, and eax points to the value
        sig = (sig[0], -sig[1])
        if og or sig[1] > 4:  # the port too, for classes past 4 bytes (MinGW returns those through memory)
            f = f[:3] + [f[4]] if f[0] == 'R' else f[:4] + f[5:]

    def val(v, n):
        v = int(v, 16) & ((1 << (8 * min(n, 4))) - 1)
        return 'ptr' if looks_like_pointer(v) else '%x' % v
    if f[0] == 'R':
        return ' '.join(f[:3] + ([val(f[3], sig[1])] if sig[1] else []))
    args, words = [], f[4:]
    for n in sig[0]:
        if n < -4:  # a class by value: 10.5.exe copies it onto the stack, the port may pass a pointer to a copy
            args.append('class')
            break  # the arguments after it sit elsewhere on each side
        args.append(('ptr' if n < 0 else val(words[0], n)) if words else '?')
        words = words[(abs(n) + 3) // 4:]
    return ' '.join(f[:3] + ['this'] + args)  # ecx: an address (or junk for a static function), see TRACE_THIS


def trace_frame(line):
    f = line.split()
    return int(f[1]) if len(f) > 1 and f[1].lstrip('-').isdigit() else None


def balance(sigs, lines, side):
    """Stack balance: each return must leave esp at entry + 4 + the arguments the callee pops (thiscall/stdcall),
    + 4 (cdecl, or a hidden return pointer). Anything else is a calling convention mismatch between caller and
    callee (the port crashed on one, docs/port.md)."""
    stack, bad = [], {}
    for l in lines:
        f = l.split('|')[0].split()
        esp = [int(w[1:], 16) for w in f if w.startswith('^')]
        if not esp:
            continue
        if f[0] == 'C':
            stack.append((f[2], esp[0]))
        elif f[0] == 'R' and stack:
            k = max(i for i, s in enumerate(stack) if s[0] == f[2]) if any(s[0] == f[2] for s in stack) else None
            if k is None:
                continue
            name, entry = stack[k]
            del stack[k:]
            sig = sigs.get(name)
            if sig and k == len(stack):
                p = sum((abs(n) + 3) // 4 * 4 for n in sig[0])
                moved = esp[0] - entry
                if moved not in (4, 4 + p, 8 + p, 8):
                    bad.setdefault(name, (moved, 4 + p))
    for name, (moved, want) in bad.items():
        print('%s: %s pops %d bytes with its return address, its arguments make %d' % (side, name, moved, want))


def whole_calls(lines):
    depth = 0
    for l in lines:
        depth += 1 if l.startswith('C ') else -1 if l.startswith('R ') else 0
        if depth < 0:
            return False
    return depth == 0


def show(ma, mb, i, j):
    for k in range(-8, 6):
        x = ma[i + k] if 0 <= i + k < len(ma) else ''
        y = mb[j + k] if 0 <= j + k < len(mb) else ''
        print('%s %-50s %s' % ('>' if k == 0 else ' ', x, y))


def trace():
    frames, names = os.environ['TRACE'].split(':', 1)
    og, port = trace_targets(names)
    print('tracing %d functions' % len(og))
    until = int(frames.split('-')[1])
    this_env = {'GTA2_STATELOG_TRACE_THIS': os.environ.get('TRACE_THIS', '0')}
    p = run(EXE, 'state_port.log', dict(PORT, GTA2_STATELOG_TRACE=frames + ':' + ','.join(port), **this_env), until + 1)
    o = run(OG_EXE, 'state_og.log', dict(OG, GTA2_STATELOG_TRACE=frames + ':' + ','.join(og), **this_env), until + 1)
    # Compare what the signatures say: the stack arguments by size, the return value by size, pointers masked
    lay = layout.Layout(EXE)
    sigs = {o.split('=')[0]: lay.signature(o.split('=')[0]) for o in og}
    a, b = open(o + '.trace').read().split('\n'), open(p + '.trace').read().split('\n')
    if os.environ.get('DEMO'):  # only the nth demo's lines
        a, b = ([l for demo, l in demos(lines, trace_frame) if demo == DEMO] for lines in (a, b))
    # Functions the port inlined everywhere never show up there: leave them out of both
    label = lambda l: l.split()[2] if len(l.split()) > 2 else ''
    both = {label(l) for l in a} & {label(l) for l in b}
    gone = sorted({label(l) for l in a + b} - both - {''})
    if gone:
        print('only in one trace (inlined?), left out: ' + ' '.join(gone))
    a, b = [l for l in a if label(l) in both], [l for l in b if label(l) in both]
    balance(sigs, b, 'port')
    balance(sigs, a, '10.5')

    # Align the two (clang inlines some calls only at some call sites), then report the first difference
    # that isn't just whole calls missing on one side
    ma, mb = [mask(sigs, x, True) for x in a], [mask(sigs, y, False) for y in b]
    this_layout = {}
    sm = difflib.SequenceMatcher(None, ma, mb, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            for k in range(i2 - i1):
                fields = this_diff(lay, this_layout, a[i1 + k], b[j1 + k])
                if fields:
                    print('call trace: *this differs at %s (10.5 line %d, port line %d):' % (ma[i1 + k], i1 + k, j1 + k))
                    print('  ' + '\n  '.join(fields[:20]))
                    return 1
        elif (tag == 'delete' and whole_calls(ma[i1:i2])) or (tag == 'insert' and whole_calls(mb[j1:j2])):
            continue
        else:
            print('call trace: differs at 10.5 line %d, port line %d (10.5 / port):' % (i1, j1))
            show(ma, mb, i1, j1)
            return 1
    print('call trace: the same (10.5 %d lines, port %d)' % (len(a), len(b)))
    return 0


# ---- the state comparison

def load_dump(path):
    """(frame, entry) -> dwords: the first record of each, for demo DEMO (frames start again each demo)"""
    d = {}
    frame_of = lambda line: int(line.split()[1])
    for demo, line in demos(open(path), frame_of):
        p = line.split()
        if demo == DEMO:
            d.setdefault((int(p[1]), p[2]), [int(v.split('@')[0], 16) for v in p[3:]])
    return d


def fields_at_onset(frame, onset):
    """Both again, with full dumps of the onset's entries at the frame before and at the onset; prints the fields
    that differ."""
    entries = sorted({e for e, off in onset})
    BASE['GTA2_STATELOG_DUMP'] = '%d-%d:%s' % (frame - 1, frame, ','.join(entries))
    dumps = [run(EXE, 'state_port.log', PORT, frame) + '.dump', run(OG_EXE, 'state_og.log', OG, frame) + '.dump']
    lay = layout.load(EXE, TABLE)
    pd, od = (load_dump(p) for p in dumps)
    for key in sorted(set(pd) & set(od)):
        fields = {}
        for i, (a, b) in enumerate(zip(od[key], pd[key])):
            if not any(e == key[1] and off <= i * 4 < off + 64 for e, off in onset):
                continue
            if a != b and not (looks_like_pointer(a) and looks_like_pointer(b)):
                f = layout.name(lay, key[1], i * 4)
                if f != '(padding)':
                    fields.setdefault(f, '+0x%x 10.5 %08x port %08x' % (i * 4, a, b))
        print('game frame %d %s: %s' % (key[0], key[1], '; '.join('%s (%s)' % kv for kv in list(fields.items())[:20]) or 'same'))


def main():
    if not os.path.exists(TABLE) or os.path.getmtime(TABLE) < os.path.getmtime(EXE):
        run(EXE, 'state_port.log', PORT, until=0)  # writes the table
    layout.write_mask(EXE, TABLE, MASK)
    BASE['GTA2_STATELOG_MASK'] = 'state.mask'
    subprocess.run(['python3', os.path.join(ROOT, 'Scripts', 'port', 'patch_attract.py'),
                    os.path.join(ROOT, 'Scripts', 'bin_comp', '10.5.exe'), OG_EXE, ATTRACT_MS], check=True)
    if os.environ.get('TRACE'):
        return trace()
    port = run(EXE, 'state_port.log', PORT)
    og = run(OG_EXE, 'state_og.log', OG)
    og2 = run(OG_EXE, 'state_og2.log', OG)
    diff = subprocess.run(['python3', os.path.join(ROOT, 'Scripts', 'port', 'state_diff.py'), og, port, TABLE],
                          env=dict(os.environ, NOISE=og2), stdout=subprocess.PIPE, text=True).stdout
    print(diff, end='')
    m = re.search(r'game frame (\d+): \d+ chunks became different: (.*)', diff)
    if not m or os.environ.get('NO_FIELDS'):
        return 0 if not m else 1
    onset = [(c.rsplit('+', 1)[0], int(c.rsplit('+', 1)[1], 16)) for c in m.group(2).split()]
    fields_at_onset(int(m.group(1)), onset)
    return 1


if __name__ == '__main__':
    sys.exit(main())
