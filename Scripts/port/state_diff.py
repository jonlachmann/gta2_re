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
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import DEMO, IGNORE, demos, table_entries

CHUNK = 64

def chunks(table):
    return ['%s+0x%x' % (name, off) for name, n in table_entries(table) for off in range(0, n, CHUNK)]


def frame_line(line):
    parts = line.split()
    return int(parts[2]) if parts and parts[0] == 'F' else None


def frames(path):
    """Yields (game frame, live state) at the first record of each frame, for demo DEMO of a log. The state is every
    line's from the start: a line only has the chunks that changed."""
    state, last = {}, None
    for demo, line in demos(open(path), frame_line):
        frame = frame_line(line)
        if frame is None:
            continue
        if demo > DEMO:
            return
        if not line.endswith('\n'):
            return  # cut off by the kill
        for item in line.split()[3:]:
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
ignore = re.compile(IGNORE)
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
