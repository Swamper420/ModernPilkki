"""Decode .spr sprites (magic 01: u16-RGB565 color + u8 count triplets).

Header: magic u32 0x00010201, u16 w, u16 h, u16 transkey (0xF81F/0xF81B),
        u16 0, u16 w, u16 0. Pixels: (color,count)*, sum(count) == w*h.
Other magics (fonts/frames atlases) not yet decoded.
Usage: python3 tools/spr.py  (checks all magic-01 sprites)
"""
import glob
import struct
import sys

MAGIC = b"\x01\x02\x01\x00"


def decode(path):
    d = open(path, "rb").read()
    assert d[:4] == MAGIC, "not magic-01 sprite"
    (w, h, key, z1, w2, z2) = struct.unpack("<HHHHHH", d[4:16])
    assert (z1, z2) == (0, 0)
    px = []
    i = 16
    while i < len(d):
        (color,) = struct.unpack("<H", d[i : i + 2])
        n = d[i + 2]
        px.extend([color] * n)
        i += 3
    assert len(px) == w * h, f"{len(px)} != {w}*{h}"
    return w, h, key, px


if __name__ == "__main__":
    bad = n = skipped = 0
    for f in sorted(glob.glob("extracted/gfx/spr/**/*.spr", recursive=True)) + sorted(
        glob.glob("extracted/gfx/spr/**/*.SPR", recursive=True)
    ):
        if open(f, "rb").read(4) != MAGIC:
            skipped += 1
            continue
        n += 1
        try:
            decode(f)
        except AssertionError as e:
            bad += 1
            print("FAIL", f, e)
    print(f"{n - bad}/{n} magic-01 sprites decode exactly ({skipped} other-magics skipped)")
    sys.exit(1 if bad else 0)
