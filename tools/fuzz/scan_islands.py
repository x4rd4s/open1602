#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scan_islands.py <anno1602-dir> [more dirs/files...]
# Lists every INSEL3/4/5 header in .gam/.szs/.szm/.scp files and flags islands of fewer than 2 tiles (0 x n or 1 x 1)
# (DET-1 / DET-V1: Open1602 cannot reload a save that holds such an island).
import os, struct, sys

EXT = ('.gam', '.szs', '.szm', '.scp')

def chunks(data):
    at = 0
    while at + 20 <= len(data):
        name = data[at:at + 16].split(b'\0', 1)[0].decode('latin-1')
        (length,) = struct.unpack_from('<I', data, at + 16)
        yield name, data[at + 20:at + 20 + length]
        at += 20 + length

def scan(path, stats):
    data = open(path, 'rb').read()
    for name, body in chunks(data):
        if name in ('INSEL3', 'INSEL4', 'INSEL5') and len(body) >= 3:
            num, w, h = body[0], body[1], body[2]
            stats['islands'] += 1
            stats['min_w'] = min(stats['min_w'], w)
            stats['min_h'] = min(stats['min_h'], h)
            if w * h < 2:
                stats['bad'] += 1
                print(f'TINY  {path}: {name} #{num} {w}x{h}')

def main():
    stats = {'files': 0, 'islands': 0, 'bad': 0, 'min_w': 999, 'min_h': 999}
    for root in sys.argv[1:]:
        paths = [root] if os.path.isfile(root) else [os.path.join(d, f) for d, _, fs in os.walk(root) for f in fs]
        for p in sorted(paths):
            if p.lower().endswith(EXT):
                stats['files'] += 1
                scan(p, stats)
    print(f"files {stats['files']}, islands {stats['islands']}, smallest width {stats['min_w']}, "
          f"smallest height {stats['min_h']}, islands under 2 tiles (0xN or 1x1): {stats['bad']}")

main()
