#!/usr/bin/env python3
# Port check (docs/port.md, M2): the attract replay in gta2_port.exe and 10.5.exe, headless and sped up,
# compared by their state logs (Port/headless/state_log.cpp, Scripts/port/state_diff.py). Padding is masked
# (Scripts/port/layout.py). When a chunk becomes different, both run again with a dump of those entries and
# the differing fields are printed by name.
# usage: state_check.py [until game frame, default 600]
#   env GTA2_DIR (the install), SPEED (default 10), ATTRACT_MS (default 3000), DUMP (GTA2_STATELOG_DUMP for all runs)
# Needs build_port/gta2_port.exe (Scripts/port/build.sh) and build_port/gl_clang (build_gl_dlls_clang.sh).
import os, re, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
GAME = os.path.join(ROOT, 'build_port', 'game')
UNTIL = int(sys.argv[1]) if len(sys.argv) > 1 else 600
ATTRACT_MS = os.environ.get('ATTRACT_MS', '3000')
BASE = dict(os.environ, WINEPREFIX=os.path.join(ROOT, 'build_port', 'wineprefix'), WINEDEBUG='-all',
            GTA2_SPEED=os.environ.get('SPEED', '10'), GTA2_PORT_ATTRACT_MS=ATTRACT_MS, GTA2_LOCKSTEP='1')
if os.environ.get('DUMP'):
    BASE['GTA2_STATELOG_DUMP'] = os.environ['DUMP']

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
    for suffix in ('', '.dump'):
        if os.path.exists(path + suffix):
            os.remove(path + suffix)
    subprocess.run(['wineserver', '-k'], env=BASE)
    env = dict(BASE, GTA2_STATELOG=log, **extra)
    start = time.time()
    p = subprocess.Popen([os.path.join(ROOT, 'Scripts', 'port', 'run_wine.sh'), exe], env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    while last_frame(path) < until and p.poll() is None and time.time() - start < 600:
        time.sleep(1)
    subprocess.run(['wineserver', '-k'], env=BASE)
    p.wait()
    print('%s: up to game frame %d in %.0f s' % (os.path.basename(exe), last_frame(path), time.time() - start))
    return path

EXE = os.path.join(ROOT, 'build_port', 'gta2_port.exe')
TABLE = os.path.join(GAME, 'state_port.log.globals')
MASK = os.path.join(GAME, 'state.mask')
if not os.path.exists(TABLE) or os.path.getmtime(TABLE) < os.path.getmtime(EXE):
    run(EXE, 'state_port.log', {'GTA2_HEADLESS': '1'}, until=0)  # writes the table
layout.write_mask(EXE, TABLE, MASK)
BASE['GTA2_STATELOG_MASK'] = 'state.mask'
PORT = {'GTA2_HEADLESS': '1'}
port = run(EXE, 'state_port.log', PORT)
subprocess.run(['python3', os.path.join(ROOT, 'Scripts', 'port', 'patch_attract.py'),
                os.path.join(ROOT, 'Scripts', 'bin_comp', '10.5.exe'), os.path.join(GAME, 'gta2_105_attract.exe'), ATTRACT_MS], check=True)
og_extra = {'RENDERER': os.path.join(ROOT, 'build_port', 'gl_clang'), 'GTA2_VIDEO_DLL': 'nullvideo.dll',
            'GTA2_STATELOG_TABLE': 'state_port.log.globals'}
og = run(os.path.join(GAME, 'gta2_105_attract.exe'), 'state_og.log', og_extra)
og2 = run(os.path.join(GAME, 'gta2_105_attract.exe'), 'state_og2.log', og_extra)
diff = subprocess.run(['python3', os.path.join(ROOT, 'Scripts', 'port', 'state_diff.py'), og, port, TABLE],
                      env=dict(os.environ, NOISE=og2), stdout=subprocess.PIPE, text=True).stdout
print(diff, end='')
m = re.search(r'game frame (\d+): \d+ chunks became different: (.*)', diff)
if not m or os.environ.get('NO_FIELDS'):
    sys.exit(0 if not m else 1)

# The fields: both again, with full dumps of those entries around the onset
frame = int(m.group(1))
onset = [(c.rsplit('+', 1)[0], int(c.rsplit('+', 1)[1], 16)) for c in m.group(2).split()]
entries = sorted({e for e, off in onset})
BASE['GTA2_STATELOG_DUMP'] = '%d-%d:%s' % (frame - 1, frame, ','.join(entries))
dumps = [run(EXE, 'state_port.log', PORT, frame) + '.dump',
         run(os.path.join(GAME, 'gta2_105_attract.exe'), 'state_og.log', og_extra, frame) + '.dump']

def load(path):
    d = {}
    for line in open(path):
        p = line.split()
        d.setdefault((int(p[1]), p[2]), [int(v, 16) for v in p[3:]])  # the first record of a frame
    return d

lay = layout.load(EXE, TABLE)
pd, od = (load(p) for p in dumps)
pointer = lambda v: 0x00400000 <= v <= 0x7FFEFFFF
for key in sorted(set(pd) & set(od)):
    fields = {}
    for i, (a, b) in enumerate(zip(od[key], pd[key])):
        if not any(e == key[1] and off <= i * 4 < off + 64 for e, off in onset):
            continue
        if a != b and not (pointer(a) and pointer(b)):
            f = layout.name(lay, key[1], i * 4)
            if f != '(padding)':
                fields.setdefault(f, '+0x%x 10.5 %08x port %08x' % (i * 4, a, b))
    print('game frame %d %s: %s' % (key[0], key[1], '; '.join('%s (%s)' % kv for kv in list(fields.items())[:20]) or 'same'))
sys.exit(1)
