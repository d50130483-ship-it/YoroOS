#!/usr/bin/env python3
"""mkbin.py - embed a binary blob into a C header (12 bytes per line)."""
import sys

src, name = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
out = ['/* generated from %s - do not edit */' % src,
       'static const unsigned char %s[] = {' % name]
for i in range(0, len(data), 12):
    out.append('    ' + ','.join('0x%02x' % b for b in data[i:i + 12]) + ',')
out.append('};')
out.append('static const int %s_len = %d;' % (name, len(data)))
sys.stdout.write('\n'.join(out) + '\n')
