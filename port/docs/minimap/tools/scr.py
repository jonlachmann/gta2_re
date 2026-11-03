# Minimal reader for a compiled GTA2 .scr: base pointers, command headers, string table (layout from ScriptManager::Load).
import struct

FP = 16384.0  # Fix16: 14 fractional bits


class Scr:
    def __init__(self, path):
        d = open(path, 'rb').read()
        self.ptrs = struct.unpack_from('<6000H', d, 0)
        self.data = d[12000:12000 + 65536]
        tsize = struct.unpack_from('<H', d, 12000 + 65536)[0]
        s = d[12000 + 65536 + 2:]
        self.strings = {}
        o = 0
        while o < tsize:
            sid, zone, typ, _, ln = struct.unpack_from('<hhhhB', s, o)
            name = s[o + 9:o + 9 + ln].split(b'\0')[0].decode('latin1')
            self.strings[sid] = (name, typ)
            o += 9 + ln

    def commands(self):
        seen = set()
        for i, p in enumerate(self.ptrs):
            if p and p not in seen and p + 8 <= len(self.data):
                seen.add(p)
                this, typ, nxt, ret = struct.unpack_from('<hHhh', self.data, p)
                yield i, p, typ

    def fix_vec(self, off):
        return tuple(v / FP for v in struct.unpack_from('<3i', self.data, off))
