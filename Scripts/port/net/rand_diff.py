#!/usr/bin/env python3
"""Compares two GTA2_NET_RANDLOG logs (a server's and a client's, docs/netcode.md "Checks"): per function that drew,
the rand() draws per tick on each side.

usage: Scripts/port/net/rand_diff.py <program> <server log> <client log>

Each log line is: cycle, the caller of rand(), and the two above it, as offsets in the program. A function is named
by the first frame that isn't an rng wrapper (rng::get_int_4F7AE0 and the like). Names come from atos (macOS) or
addr2line (Linux).
"""
import collections
import subprocess
import sys


def symbolize(program, offsets):
    offsets = sorted(offsets)
    if sys.platform == "darwin":
        out = subprocess.run(["atos", "-o", program, "-l", "0x100000000"] + ["0x%x" % (0x100000000 + o) for o in offsets],
                             capture_output=True, text=True).stdout.splitlines()
        names = [l.split(" (in ")[0] for l in out]
    else:
        out = subprocess.run(["addr2line", "-f", "-C", "-e", program] + ["0x%x" % o for o in offsets],
                             capture_output=True, text=True).stdout.splitlines()
        names = out[0::2]
    return {o: n.split("(")[0] for o, n in zip(offsets, names)}


def read(path):
    rows = []
    for line in open(path):
        cycle, *frames = line.split()
        rows.append((int(cycle), [int(f, 16) for f in frames]))
    return rows


def main():
    program, server, client = sys.argv[1], read(sys.argv[2]), read(sys.argv[3])
    names = symbolize(program, {f for _, frames in server + client for f in frames})
    ticks = sorted({c for c, _ in client} & {c for c, _ in server})

    def per_function(rows):
        counts = collections.Counter()
        for cycle, frames in rows:
            if cycle in ticks:
                named = [names[f] for f in frames if not names[f].startswith("rng::")]
                counts[named[0] if named else names[frames[0]]] += 1
        return counts

    s, c = per_function(server), per_function(client)
    n = len(ticks)
    print("ticks %d..%d: draws per tick, server %.1f, client %.1f" % (ticks[0], ticks[-1], sum(s.values()) / n, sum(c.values()) / n))
    print("%-70s %8s %8s" % ("per tick, by the function that drew", "server", "client"))
    for name in sorted(set(s) | set(c), key=lambda k: -(s[k] + c[k])):
        mark = "" if s[name] == c[name] else "  <-"
        print("%-70s %8.1f %8.1f%s" % (name[:70], s[name] / n, c[name] / n, mark))


if __name__ == "__main__":
    main()
