#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""embed_text.py <in> <out>: write a text file as a C string literal (for #include)."""
import sys

src, dst = sys.argv[1:]
with open(src, encoding='utf-8') as f, open(dst, 'w') as o:
    for line in f:
        o.write('"%s\\n"\n' % line.rstrip('\r\n').replace('\\', '\\\\').replace('"', '\\"'))
