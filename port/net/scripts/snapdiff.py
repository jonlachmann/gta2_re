#!/usr/bin/env python3
# The snapshot check's findings (GTA2_NET_SNAPCHECK, docs/netcode.md "Checks"), field by field: each entity a client
# read wrong ("... entity <id> (<type>) is wrong, <how>: <fields>") against what the server says it sent then
# ("... read entity <id> of snapshot <tick> wrong; we sent (...): <fields>"). Prints the fields that differ.
# usage: port/net/scripts/snapdiff.py <dir with the server's and the clients' logs> (build_mac/net_stress/<n>)
import re
import sys
from pathlib import Path

FIELD = re.compile(r" (\S+?)=(-?\d+)(?: \(was (-?\d+)\))?")


def fields(text):
    return {m.group(1): (int(m.group(2)), m.group(3)) for m in FIELD.finditer(text)}


def main():
    d = Path(sys.argv[1])
    sent = {}  # (tick, id) -> (header, fields)
    for line in (d / "server.log").read_text(errors="replace").splitlines():
        m = re.search(r"read entity (\d+) of snapshot (\d+) wrong; we sent \(([^)]*)\):(.*)", line)
        if m:
            sent[(int(m.group(2)), int(m.group(1)))] = (m.group(3), fields(m.group(4)))
    found = 0
    for log in sorted(d.glob("*.log")):
        if log.name == "server.log":
            continue
        for line in log.read_text(errors="replace").splitlines():
            m = re.search(r"snapshot (\d+) \(baseline (\d+)[^)]*\): entity (\d+) \((\w+)\) is wrong, ([^:]+):(.*)", line)
            if not m:
                continue
            found += 1
            tick, base, eid, kind, how = int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4), m.group(5)
            got = fields(m.group(6))
            print(f"{log.stem}: snapshot {tick} (baseline {base}), {kind} {eid}, {how}")
            if (tick, eid) not in sent:
                print("  the server's copy isn't in its log")
                continue
            note, want = sent[(tick, eid)]
            for name, (value, _) in got.items():
                if name in want and want[name][0] != value:
                    was = want[name][1]
                    print(f"  {name}: read {value}, sent {want[name][0]}" + (f" (the server's baseline: {was})" if was else ""))
    if not found:
        print("no entity read wrong")


if __name__ == "__main__":
    main()
