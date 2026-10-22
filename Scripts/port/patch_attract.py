#!/usr/bin/env python3
# Test aid (docs/port.md): a copy of 10.5.exe whose start menu starts attract mode after <ms> of idle
# instead of 60 s, to match the port run with GTA2_PORT_ATTRACT_MS=<ms>. The original stays untouched.
# usage: patch_attract.py <10.5.exe> <out.exe> <ms>
import struct, sys

data = bytearray(open(sys.argv[1], 'rb').read())
# Frontend::Run_4AEDB0: cmp ecx, 60000 (81 F9 60 EA 00 00) at 0x4AEEA1, file offset 0xAEEA1 - 2
OFFSET = 0xAEEA1
assert data[OFFSET - 2:OFFSET + 4] == bytes.fromhex('81f960ea0000'), 'not 10.5.exe'
data[OFFSET:OFFSET + 4] = struct.pack('<I', int(sys.argv[3]))
open(sys.argv[2], 'wb').write(data)
