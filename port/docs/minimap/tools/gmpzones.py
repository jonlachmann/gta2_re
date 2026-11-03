# Reads the ZONE chunk of a .gmp: (type, x, y, w, h, name) in blocks.
import struct


def zones(path):
    d = open(path, 'rb').read(); p = 6; out = []
    while p + 8 <= len(d):
        t = d[p:p+4]; n = struct.unpack_from('<I', d, p + 4)[0]
        if t == b'ZONE':
            o = p + 8; e = o + n
            while o < e:
                typ, x, y, w, h, ln = struct.unpack_from('<6B', d, o)
                out.append((typ, x, y, w, h, d[o+6:o+6+ln].decode('latin1')))
                o += 6 + ln
        p += 8 + n
    return out
