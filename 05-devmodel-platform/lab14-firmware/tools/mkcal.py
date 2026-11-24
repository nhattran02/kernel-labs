#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""mkcal.py OUTDIR - write lfdcal calibration blobs (good and broken ones).

Blob layout, little-endian (see lfdcal.c):
  struct hdr   { u32 magic 'LFDC'; u16 version (1); u16 count;
                 u32 crc32 (zlib/IEEE CRC-32 of the entries); u32 reserved (0) }
  struct entry { u32 raw; u32 cal; }   x count, raw strictly increasing
"""
import os, struct, sys, zlib

MAGIC = 0x4344464C  # bytes 'L' 'F' 'D' 'C'


def blob(entries, magic=MAGIC, version=1, count=None, crc=None, reserved=0):
    body = b''.join(struct.pack('<II', r, c) for r, c in entries)
    if count is None:
        count = len(entries)
    if crc is None:
        crc = zlib.crc32(body)
    return struct.pack('<IHHII', magic, version, count, crc, reserved) + body


good = [(0, 100), (1000, 1100), (2000, 2300), (3000, 3600), (4095, 5000)]
blobs = {
    'cal.bin': blob(good),
    'cal-v2.bin': blob([(0, 0), (4095, 8190)]),
    'bad-magic.bin': blob(good, magic=0x12345678),
    'bad-version.bin': blob(good, version=2),
    'bad-crc.bin': blob(good, crc=0xdeadbeef),
    'truncated.bin': blob(good)[:-3],
    'huge-count.bin': blob(good, count=65535),
    'unsorted.bin': blob([(0, 0), (2000, 1), (1000, 2)]),
    'empty.bin': b'',
}
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for name, data in blobs.items():
    with open(os.path.join(out, name), 'wb') as f:
        f.write(data)
print(f'mkcal: wrote {len(blobs)} blobs to {out}')
