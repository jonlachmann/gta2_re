# What the state check's scripts (state_check.py, state_diff.py, layout.py) share: the folders, the state log's table
# of entries, the attract demos within a log, and which values count as pointers (docs/port.md, "Validation").
import os, re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
GAME = os.path.join(ROOT, 'build_port', 'game')  # the install's copy that run_wine.sh runs in

# IGNORE=<regex> on entry and chunk names: render scratch and sound, whose state follows the real audio playback, not
# the game clock. state_diff.py leaves them out, and the state log doesn't take them as pointer targets.
IGNORE = os.environ.get('IGNORE', r'gTileVerts_|gSound_obj_66F680|gSampManager_6FFF00')
DEMO = int(os.environ.get('DEMO', '1'))  # DEMO=n: compare the nth attract demo of the logs


def looks_like_pointer(v):
    """A 32-bit value in the range the state log takes for a pointer (Port/validation/state_log.cpp)."""
    return 0x00400000 <= v <= 0x7FFEFFFF


def table_entries(table):
    """The state log's entries in its order, from the port run's <log>.globals: (name, size) for each global, then
    ('*' + name, size) for what a pointer global points to."""
    out = []
    for line in open(table):
        og, size, pointee, name = line.split()
        out.append((name, int(size)))
        if int(pointee):
            out.append(('*' + name, int(pointee)))
    return out


def demos(lines, frame_of):
    """Yields (demo, line) for every line of a log: the game frame starts again with each attract demo, so a frame
    lower than the one before begins the next. frame_of(line) is the line's game frame, or None."""
    demo, last = 1, None
    for line in lines:
        frame = frame_of(line)
        if frame is not None:
            if last is not None and frame < last:
                demo += 1
            last = frame
        yield demo, line
