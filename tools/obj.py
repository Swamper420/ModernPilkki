"""Verify .obj v2 layout (FUN_0040d320, magic 'Pro Pilkki Object').

Layout: magic[18] u32@330 u32@334 u32@378 u32 texcount (texcount x 50B names)
        0x300B block @30 (@330 x 12B verts) (@378 x 52B faces)
        u32@590 fcount (fcount x 12B) EOF
Usage: python3 tools/obj.py  (checks all .obj files)
"""
import glob
import struct
import sys

MAGIC = b"Pro Pilkki Object\x00"


def parse(path):
    d = open(path, "rb").read()
    assert d[:18] == MAGIC, f"bad magic: {d[:18]!r}"
    off = 18
    nvert, a, nface = struct.unpack("<III", d[off : off + 12])
    off += 12
    (nt,) = struct.unpack("<I", d[off : off + 4])
    off += 4
    off += nt * 0x32
    off += 0x300
    voff = off
    off += nvert * 12
    foff = off
    off += nface * 0x34
    (nf,) = struct.unpack("<I", d[off : off + 4])
    off += 4 + nf * 12
    # footer: u32 @d70, six u64 @d74..@da0, 0x68B block @da8 (+'U' conditional)
    blk = d[off + 4 + 6 * 8 : off + 4 + 6 * 8 + 0x68]
    off += 4 + 6 * 8 + 0x68
    extra = 0
    if blk[:1] == b"U":
        c0, c1, c2 = struct.unpack("<III", blk[8:20])
        extra = (c0 + c1 + c2) * 0x24
        off += extra
    # faces: u32 idx[3] + aux[3] + uv[6] pixels + flags; idx must be < nvert
    for i in range(nface):
        idx = struct.unpack("<3I", d[foff + i * 0x34 : foff + i * 0x34 + 12])
        assert all(v < nvert for v in idx), f"face {i} idx OOB in {path}"
    return (nvert, a, nface, nt, nf), off, len(d)


if __name__ == "__main__":
    bad = n = 0
    for f in sorted(glob.glob("extracted/gfx/obj/**/*.obj", recursive=True)):
        n += 1
        try:
            counts, off, size = parse(f)
            if off != size:
                bad += 1
                print("SIZE", f, counts, off, size)
        except AssertionError as e:
            bad += 1
            print("FAIL", f, e)
    print(f"{n - bad}/{n} .obj files parse exactly")
    sys.exit(1 if bad else 0)
