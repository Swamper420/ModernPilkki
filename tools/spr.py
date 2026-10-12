"""Full .spr decoder (FUN_0062f750, verified exact-consume 1212/1213).

Header: u8 nframes, u8 bpp (1/2), u8 enc (0 raw / 1 RLE / 2 RLE-interleaved),
u8 flags, u16 w, h, transkey, u16 extra_len (nonzero only in frames.spr).
Then: u16[nframes] table1, u16[nframes] table2, pixel runs
(enc 0: raw w*h*nframes*bpp bytes; enc 1: (value:bpp + count:u8)*),
then 256x3B palette if bpp == 1. enc 2 (frames.spr only, unreferenced
by binary) not decoded.
Usage: python3 tools/spr.py
"""
import glob
import os
import struct
import sys


def decode(d):
    b0, b1, b2, b3 = d[0], d[1], d[2], d[3]
    w, h, key, eh = struct.unpack("<HHHH", d[4:12])
    assert eh == 0, "extra header (frames.spr only)"
    assert b1 in (1, 2) and b2 in (0, 1), f"unsupported ({b1},{b2})"
    off = 12
    t1 = list(struct.unpack("<" + "H" * b0, d[off:off + 2 * b0]))
    off += 2 * b0
    t2 = list(struct.unpack("<" + "H" * b0, d[off:off + 2 * b0]))
    off += 2 * b0
    total = w * h * b0
    if b2 == 0:
        off += total * b1
    else:
        got = 0
        while got < total:
            assert off + b1 + 1 <= len(d), "truncated run"
            cnt = d[off + b1]
            assert cnt != 0, "zero run"
            got += cnt
            off += b1 + 1
        assert got == total, f"overrun {got}>{total}"
    pal = None
    if b1 == 1:
        pal = d[off:off + 768]
        assert len(pal) == 768, "truncated palette"
        off += 768
    assert off == len(d), f"size mismatch {off}!={len(d)}"
    return (b0, b1, b2, b3, w, h, key, t1, t2, pal)


if __name__ == "__main__":
    bad = n = skipped = 0
    for f in sorted(glob.glob("extracted/gfx/spr/**/*", recursive=True)):
        if not os.path.isfile(f):
            continue
        d = open(f, "rb").read()
        if d[2] == 2:
            skipped += 1  # frames.spr: enc 2, no binary reference
            continue
        n += 1
        try:
            decode(d)
        except AssertionError as e:
            bad += 1
            print("FAIL", f, e)
    print(f"{n - bad}/{n} sprites decode exactly ({skipped} enc-2 skipped)")
    sys.exit(1 if bad else 0)
