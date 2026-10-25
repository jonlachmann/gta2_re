#!/usr/bin/env python3
"""Compares a server's GTA2_NET_DUMPPEDS dump with a client's (the divergence check, docs/netcode.md "Checks").

usage: Scripts/port/ped_diff.py <server dump> <client dump> [--peds N]

Both hold every ped's Ped, Char and Sprite bytes per tick, by the server's ids. The members come from clang's record
layouts of this build (64-bit: the decomp's field_XX offsets are the 32-bit ones). Pointers are left out (they differ
between the two games anyway). Per member: how many peds first differed in it after 0, 1, 2-3, ... ticks of the
client on its own; and per ped the members that differed first, the likeliest causes.
"""
import collections
import os
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FLAGS = ("-fshort-wchar -std=c++17 -fms-extensions -IPort/compat -include Port/compat/port_linux.h -ISource -I. "
         "-I/opt/homebrew/include -D_CRT_SECURE_NO_WARNINGS -D_USE_32BIT_TIME_T -DGTA2_PORT -DNDEBUG -DBUILD_MSS").split()


# Each game's own numbering, not state
LOCAL = {"Char.field_0_id", "Sprite.field_20_instance_id", "Ped.field_200_id"}


def layouts():
    """{class: [(offset, name, is_pointer)]}, the leaf members, sorted"""
    with tempfile.NamedTemporaryFile("w", suffix=".cpp", delete=False) as f:
        f.write('#include "Source/Ped.hpp"\n#include "Source/char.hpp"\n#include "Source/sprite.hpp"\n'
                "int a = sizeof(Ped), b = sizeof(Char), c = sizeof(Sprite);\n")
    out = subprocess.run(["clang++", "-w", *FLAGS, "-Xclang", "-fdump-record-layouts-complete", "-fsyntax-only", f.name],
                         cwd=ROOT, capture_output=True, text=True).stdout
    os.unlink(f.name)
    result = {}
    for cls in ("Ped", "Char", "Sprite"):
        m = re.search(r"^\s*0 \| class %s\n(.*?)^\s*\| \[sizeof=" % cls, out, re.S | re.M)
        members, stack = [], []  # stack: (indent, name) of the enclosing members
        for line in m.group(1).splitlines():
            lm = re.match(r"^\s*(\d+)(?::\d+-\d+)? \|( +)(.*)$", line)
            if not lm:
                continue
            off, indent, decl = int(lm.group(1)), len(lm.group(2)), lm.group(3)
            name = decl.split()[-1]
            while stack and stack[-1][0] >= indent:
                stack.pop()
            full = ".".join([s[1] for s in stack] + [name])
            stack.append((indent, name))
            members.append((off, full, "*" in decl or decl.startswith("sptr ") or decl.startswith("uptr "), indent))
        # leaves: a member that the next one isn't nested in
        leaves = [(o, n, p) for i, (o, n, p, ind) in enumerate(members) if i + 1 == len(members) or members[i + 1][3] <= ind]
        result[cls] = sorted(leaves)
    return result


def member_at(leaves, offset):
    lo, hi = 0, len(leaves) - 1
    while lo < hi:  # the last leaf starting at or before offset
        mid = (lo + hi + 1) // 2
        if leaves[mid][0] <= offset:
            lo = mid
        else:
            hi = mid - 1
    return leaves[lo]


def read(path):
    """{(tick, id): (ped, char, sprite)}"""
    data, out = open(path, "rb").read(), {}
    i = 0
    while i + 20 <= len(data):
        tick, pid, np, nc, ns = struct.unpack_from("<5I", data, i)
        i += 20
        parts = []
        for n in (np, nc, ns):
            parts.append(data[i:i + n])
            i += n
        out[(tick, pid)] = parts
    return out


def bucket(age):
    b = 0
    while b < 8 and age >= (1 << b):
        b += 1
    return b


def main():
    server, client = read(sys.argv[1]), read(sys.argv[2])
    lay = layouts()
    classes = ("Ped", "Char", "Sprite")
    start = min(t for t, _ in client)
    first = {}  # (id, member) -> first age
    for (tick, pid), cparts in sorted(client.items()):
        sparts = server.get((tick, pid))
        if not sparts:
            continue
        for cls, s, c in zip(classes, sparts, cparts):
            if len(s) != len(c) or not s:
                continue
            leaves = lay[cls]
            for off in range(len(s)):
                if s[off] != c[off]:
                    o, name, ptr = member_at(leaves, off)
                    key = (pid, cls + "." + name)
                    if not ptr and key not in first and key[1] not in LOCAL:
                        first[key] = tick - start
    peds = {pid for _, pid in client}
    print("peds in the client's dump: %d, ticks %d..%d" % (len(peds), start, max(t for t, _ in client)))
    per_member = collections.defaultdict(lambda: [0] * 9)
    for (pid, member), age in first.items():
        per_member[member][bucket(age)] += 1
    print("per member, the peds that first differed after 0 | 1 | 2-3 | 4-7 | 8-15 | 16-31 | 32-63 | 64-127 | 128+ ticks:")
    for member, counts in sorted(per_member.items(), key=lambda kv: [-c for c in kv[1]]):
        print("  %-60s %s" % (member, " ".join("%3d" % c for c in counts)))
    print("per ped, the members that differed first (and when):")
    roots = collections.Counter()
    for pid in sorted(peds):
        ages = [(a, m) for (p, m), a in first.items() if p == pid]
        if not ages:
            continue
        earliest = min(a for a, _ in ages)
        names = sorted(m for a, m in ages if a == earliest)
        roots.update(names)
        if "--peds" in sys.argv:
            print("  %u after %d: %s" % (pid, earliest, ", ".join(names)))
    for member, n in roots.most_common(40):
        print("  %-60s first in %d peds" % (member, n))


if __name__ == "__main__":
    main()
