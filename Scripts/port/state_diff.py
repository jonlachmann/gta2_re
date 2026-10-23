#!/usr/bin/env python3
# Port check (docs/port.md, M2): compares two state logs (Port/headless/state_log.cpp) of the same replay,
# game frame by game frame, in 64-byte chunks of every global and of what pointer globals point to.
# A chunk that was identical and becomes different marks a divergence; chunks that never match (memory the
# game never initialised, or where the decomp's layout differs from 10.5.exe's) are only listed.
# usage: state_diff.py <log A> <log B> [<table>]     table: the port run's <log>.globals (default <log B>.globals)
#   env NOISE=<log C>: a second run of A's exe; chunks that differ between A and C are ignored
#   env IGNORE=<regex> on chunk names to leave out (default: render scratch, gTileVerts_*, and sound,
#       whose state follows the real audio playback, not the game clock)
import os, re, sys

CHUNK = 64

def chunks(table):
    out = []
    for line in open(table):
        og, size, pointee, name = line.split()
        for prefix, n in ((name, int(size)), ('*' + name, int(pointee))):
            if prefix.startswith('*') and not n:
                continue
            for off in range(0, n, CHUNK):
                out.append('%s+0x%x' % (prefix, off))
    return out

DEMO = int(os.environ.get('DEMO', '1'))  # DEMO=n: compare the nth attract demo of the logs


def frames(path):
    """Yields (game frame, live state) at the first record of each frame, for demo DEMO of a log."""
    state, last, demo = {}, -1, 1
    for line in open(path):
        parts = line.split()
        if not parts or parts[0] != 'F':
            continue
        frame = int(parts[2])
        if frame < last:
            demo += 1  # the game frame starts again: the next demo
            if demo > DEMO:
                return
        if not line.endswith('\n'):
            return  # cut off by the kill
        for item in parts[3:]:
            c, h = item.split(':')
            state[int(c, 16)] = h
        if frame != last:
            last = frame
            if demo == DEMO:
                yield frame, state

def lockstep(path_a, path_b):
    """Yields (frame, state A, state B) for the frames both logs have."""
    ga, gb = frames(path_a), frames(path_b)
    fa, sa = next(ga, (None, None))
    fb, sb = next(gb, (None, None))
    while fa is not None and fb is not None:
        if fa == fb:
            yield fa, sa, sb
            fa, sa = next(ga, (None, None))
            fb, sb = next(gb, (None, None))
        elif fa < fb:
            fa, sa = next(ga, (None, None))
        else:
            fb, sb = next(gb, (None, None))

names = chunks(sys.argv[3] if len(sys.argv) > 3 else sys.argv[2] + '.globals')
noise = set()
if os.environ.get('NOISE'):
    for f, sa, sc in lockstep(sys.argv[1], os.environ['NOISE']):
        noise |= {k for k in set(sa) | set(sc) if sa.get(k) != sc.get(k)}
ignore = re.compile(os.environ.get('IGNORE', r'gTileVerts_|gSound_obj_66F680|gSampManager_6FFF00'))
noise |= {k for k, n in enumerate(names) if ignore.search(n)}
equal_once, never, count, first = set(), None, 0, None
for f, sa, sb in lockstep(sys.argv[1], sys.argv[2]):
    count += 1
    first = f if first is None else first
    keys = set(sa) | set(sb)
    same = {k for k in keys if sa.get(k) == sb.get(k)}
    onset = sorted(k for k in keys - same - noise if k in equal_once)
    equal_once |= same
    if onset:
        print('compared game frames %d-%d (%d frames, %d chunks, %d noisy)' % (first, f, count, len(names), len(noise)))
        print('game frame %d: %d chunks became different: %s' % (f, len(onset), ' '.join(names[k] for k in onset[:30])))
        never = sorted(k for k in keys - equal_once - noise)
        break
else:
    print('compared %d frames from game frame %s: no chunk became different' % (count, first))
if never is not None:
    groups = {}
    for k in never:
        groups.setdefault(names[k].split('+')[0], 0)
        groups[names[k].split('+')[0]] += 1
    print('never identical (left out): %s' % ', '.join('%s (%d)' % g for g in sorted(groups.items(), key=lambda g: -g[1])[:25]))
sys.exit(0 if never is None else 1)
