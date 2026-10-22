#!/usr/bin/env python3
# Port investigation aid (docs/port.md): stops an exe in the attract replay at game frame <frame>, at its
# <count>-th call of rng::get_int_4F7AE0 (winedbg breakpoints), and dumps memory there, so 10.5.exe and the
# port can be compared at the same moment. usage: snapshot.py <exe> <frame> <count> <what>...   what = name:address:dwords, or name:*address:dwords
# for a pointer global. Addresses are 10.5.exe's; for the port they are looked up by the decomp's symbol names
# (the ..._<address> suffix). env: GTA2_PORT_ATTRACT_MS for both (the original via patch_attract.py).
import os, re, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
GAME = os.path.join(ROOT, 'build_port', 'game')
ENV = dict(os.environ, WINEPREFIX=os.path.join(ROOT, 'build_port', 'wineprefix'), WINEDEBUG='-all')


def symbols(exe):
    out = subprocess.run(['i686-w64-mingw32-nm', exe], capture_output=True, text=True).stdout
    table = {}
    for line in out.split('\n'):
        parts = line.split()
        if len(parts) == 3:
            found = re.findall(r'_([0-9A-Fa-f]{6})', parts[2])  # the decomp's address suffix: the last one
            if found and parts[1] in 'TtBbDd' and not parts[2].startswith('.'):
                table.setdefault(int(found[-1], 16), int(parts[0], 16))
    return table


exe, frame, count, wants = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4:]
syms = symbols(exe)
addr = lambda a: syms.get(a, a)  # 10.5.exe: no symbols, the address itself
# Started under winedbg (WINE_LAUNCHER, run_wine.sh): attaching to a running wow64 process injects a
# thread that faults on macOS wine.
# Stop at the start of game frame <frame> (RunGameFrame_4DA850, with the frame counter log_random prints:
# gpRng_67AB34's), then at the <count>-th rng::get_int_4F7AE0 call of that frame. Stopping on every get_int
# from the start is far too slow under the debugger.
frame_expr = '*(int*)(*(int*)0x%x)' % addr(0x67AB34)
cmds = ['break *0x%x' % addr(0x4DA850), 'condition 1 %s == %d' % (frame_expr, frame), 'cont', 'delete break 1',
        'break *0x%x' % addr(0x4F7AE0)] + ['cont'] * count + ['print %s' % frame_expr]
for w in wants:
    name, a, n = w.split(':')
    if a.startswith('*'):  # *ADDR or *ADDR+OFFSET: through a pointer global
        base, _, off = a[1:].partition('+')
        cmds += ['x /%sx (*(unsigned int*)0x%x)+0x%x' % (n, addr(int(base, 16)), int(off or '0', 16))]
    else:
        cmds += ['x /%sx 0x%x' % (n, addr(int(a, 16)))]
cmds += ['kill', 'quit']
subprocess.run(['wineserver', '-k'], env=ENV)
out = subprocess.run([os.path.join(ROOT, 'Scripts', 'port', 'run_wine.sh'), exe], input='\n'.join(cmds) + '\n',
                     capture_output=True, text=True, errors='replace', env=dict(ENV, WINE_LAUNCHER='winedbg'), timeout=900).stdout
subprocess.run(['wineserver', '-k'], env=ENV)
print('\n'.join(l for l in out.split('\n') if l.strip() and 'fixme' not in l))
