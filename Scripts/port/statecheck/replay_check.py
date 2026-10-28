#!/usr/bin/env python3
# Port check, a first M2 (docs/port.md): the attract-mode replay played by two exes, frame by frame.
# Both sit on the start menu until the game starts attract mode (60 s of wall-clock idle, so the start frame
# varies by a few frames); a window of consecutive frames is captured from each, aligned on the offset where
# they agree, and every overlapping frame must be identical.
# usage: Scripts/port/statecheck/replay_check.py <exe A> <exe B>   (paths; 10.5.exe needs RENDERER, see run_wine.sh)
#   env: FIRST/LAST capture window in frames (default 2300/2420), GTA2_DIR, RENDERER
import hashlib, os, shutil, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
GAME = os.path.join(ROOT, 'build_port', 'game')
FIRST, LAST = int(os.environ.get('FIRST', 2300)), int(os.environ.get('LAST', 2420))
ENV = dict(os.environ, WINEPREFIX=os.path.join(ROOT, 'build_port', 'wineprefix'), WINEDEBUG='-all')


def capture(exe, tag):
    out = 'replay_' + tag
    shutil.rmtree(os.path.join(GAME, out), ignore_errors=True)  # stale frames would look finished
    subprocess.run(['wineserver', '-k'], env=ENV)
    env = dict(ENV, GTA2GL_CAPTURE='%s,%d-%d' % (out, FIRST, LAST))
    p = subprocess.Popen([os.path.join(ROOT, 'Scripts', 'port', 'run', 'run_wine.sh'), exe], env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    folder = os.path.join(GAME, out)
    last = os.path.join(folder, 'gl_%05d.bmp' % LAST)
    deadline = time.time() + LAST / 30.0 + 120
    while not os.path.exists(last) and time.time() < deadline:
        time.sleep(2)
    time.sleep(2)
    subprocess.run(['wineserver', '-k'], env=ENV)
    p.wait()
    frames = []
    for f in range(FIRST, LAST + 1):
        path = os.path.join(folder, 'gl_%05d.bmp' % f)
        frames.append(hashlib.md5(open(path, 'rb').read()).hexdigest() if os.path.exists(path) else None)
    return frames


a = capture(sys.argv[1], 'a')
b = capture(sys.argv[2], 'b')
best = (0, 0, 0)
for k in range(-(len(a) // 2), len(a) // 2 + 1):
    pairs = [(a[i], b[i + k]) for i in range(len(a)) if 0 <= i + k < len(b) and a[i] and b[i + k]]
    same = sum(1 for x, y in pairs if x == y)
    if same > best[0]:
        best = (same, k, len(pairs))
same, offset, overlap = best
distinct = len(set(x for x in a if x))
print('captured %d frames each (%d distinct in A); best offset %+d: %d of %d overlapping frames identical'
      % (len(a), distinct, offset, same, overlap))
# Where it goes wrong: the first frame (of A) after which the aligned frames stop matching
diverged = [FIRST + i for i in range(len(a)) if 0 <= i + offset < len(b) and a[i] and b[i + offset] and a[i] != b[i + offset]]
if diverged:
    print('first differing frame of A: %d (%d of the overlap differ)' % (diverged[0], len(diverged)))
sys.exit(0 if overlap and same == overlap and distinct > 10 else 1)
