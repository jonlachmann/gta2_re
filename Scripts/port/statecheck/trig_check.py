#!/usr/bin/env python3
# Port check (docs/port.md): the sine, cosine and tangent tables of a running exe, dumped with winedbg,
# against 10.5.exe's. The game fills them at start-up from the C library; any difference makes replays drift.
# usage: Scripts/port/statecheck/trig_check.py <exe in build_port/game> [<exe> ...]   (GTA2 copied there by run_wine.sh)
# The addresses come from the exe's symbols (gSin_table_667A80, ...) or, for 10.5.exe, the decomp's.
import os, re, subprocess, sys, time

GAME = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'build_port', 'game')
ENV = dict(os.environ, WINEPREFIX=os.path.abspath(os.path.join(GAME, '..', 'wineprefix')), WINEDEBUG='-all')
TABLES = ['gSin_table_667A80', 'gCos_table_669260', 'gTanTable_6663C8']


def addresses(exe):
    out = subprocess.run(['i686-w64-mingw32-nm', exe], capture_output=True, text=True).stdout
    found = {}
    for line in out.split('\n'):
        parts = line.split()
        for t in TABLES:
            if len(parts) == 3 and parts[2] == '_' + t:
                found[t] = int(parts[0], 16)
    # 10.5.exe has no symbols: the decomp names the original addresses
    return [found.get(t, int(t[-6:], 16)) for t in TABLES]


def dump(exe):
    subprocess.run(['wineserver', '-k'], env=ENV)
    p = subprocess.Popen(['wine', exe], cwd=GAME, env=ENV, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(12)  # past the static initialisers
    info = subprocess.run(['wine', 'winedbg'], input='info process\n', capture_output=True, text=True, env=ENV).stdout
    pid = next(l.split()[0] for l in info.split('\n') if os.path.basename(exe) in l)
    cmds = 'attach 0x%s\n' % pid + ''.join('x /1440x 0x%x\n' % a for a in addresses(os.path.join(GAME, exe))) + 'detach\nquit\n'
    out = subprocess.run(['wine', 'winedbg'], input=cmds, capture_output=True, text=True, env=ENV).stdout
    subprocess.run(['wineserver', '-k'], env=ENV)
    p.wait()
    values = []
    for line in out.replace('Wine-dbg>', '\n').split('\n'):
        m = re.match(r'\s*0x[0-9a-f]+ \S+:\s+((?:[0-9a-f]{8}\s*)+)$', line)
        if m:
            values += [int(x, 16) for x in m.group(1).split()]
    return [values[i * 1440:(i + 1) * 1440] for i in range(3)]


reference = dump('gta2_105.exe')
failures = 0
for exe in sys.argv[1:]:
    tables = dump(exe)
    for name, ref, got in zip(('sin', 'cos', 'tan'), reference, tables):
        diff = [i for i in range(1440) if len(got) != 1440 or got[i] != ref[i]]
        print('%s %s: %s' % (exe, name, 'identical to 10.5.exe' if not diff else 'differs at %s' % diff[:10]))
        failures += bool(diff)
sys.exit(failures != 0)
