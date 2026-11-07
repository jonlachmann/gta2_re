#!/usr/bin/env python3
# A GTA2 .reg file (gta2_install.reg) as the port's gta2.ini (Port/registry_ini.cpp), so the port and
# 10.5.exe can run with the same settings. usage: reg2ini.py <file.reg>  (ini on stdout)
import re, sys

data = open(sys.argv[1], 'rb').read().decode('latin-1').replace('\\\r\n', '').replace('\\\n', '')
section = None
for line in data.splitlines():
    line = line.strip()
    m = re.match(r'\[(?:HKEY_[A-Z_]+)\\(.*)\]$', line)
    if m:
        key = m.group(1).lower()
        prefix = 'software\\dma design ltd\\gta2\\'
        section = key[len(prefix):] if key.startswith(prefix) else None
        if section:
            print('\n[%s]' % section)
        continue
    m = re.match(r'"([^"]*)"=(.*)$', line)
    if not section or not m:
        continue
    name, value = m.group(1).lower(), m.group(2)
    if value.startswith('dword:'):
        print('%s=dword:%d' % (name, int(value[6:], 16)))
    elif value.startswith('"'):
        print('%s=str:%s' % (name, value[1:-1].replace('\\\\', '\\')))
    elif value.startswith('hex'):
        m2 = re.match(r'hex(?:\((\d+)\))?:(.*)', value)
        kind = int(m2.group(1)) if m2.group(1) else 3  # plain hex: REG_BINARY
        print('%s=hex%d:%s' % (name, kind, re.sub(r'[\s,]', '', m2.group(2))))
