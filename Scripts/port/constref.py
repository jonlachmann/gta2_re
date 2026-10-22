#!/usr/bin/env python3
# Port M0 helper (docs/port.md): turn Fix16&/Ang16&/Fix16_Point& parameters into const& in every
# declaration and definition of the named functions under Source/. Run from the repo root.
# usage: Scripts/port/constref.py name1,name2 [out_param1,out_param2]   (listed parameters stay non-const)
import re, sys, glob
names = sys.argv[1].split(',')
skip = set(sys.argv[2].split(',')) if len(sys.argv) > 2 else set()
# a declaration/definition: a return type token right before the (qualified) name, params may span lines
decl = re.compile(r'((?:Fix16|Fix16_Point|Ang16|void|bool|s32|char_type|Object_2C\*|class Ang16)\s+(?:__stdcall\s+)?(?:\w+::)*(?:%s)\s*)\(([^()]*)\)' % '|'.join(map(re.escape, names)))
def fix(m):
    return '%s(%s)' % (m.group(1), re.sub(r'(?<!const )\b(Fix16_Point|Fix16|Ang16)&(\s*)(\w+)', lambda pm: pm.group(0) if pm.group(3) in skip else 'const %s&%s%s' % (pm.group(1), pm.group(2), pm.group(3)), m.group(2)))
for f in glob.glob('Source/*.hpp') + glob.glob('Source/*.cpp'):
    s = open(f, newline='').read()
    n = decl.sub(fix, s)
    if n != s:
        open(f, 'w', newline='').write(n); print(f)
