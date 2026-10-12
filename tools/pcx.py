"""Decode Pro Pilkki custom PCX (FUN_005fefd0/005fb040/005fb7c0/005fc360).

Custom header: magic 0A 05 01 08, box zeroed on disk, '[' (0x5B) at 127.
Dims obfuscated (FUN_00600aa0): xmax = s16(@0x42)+0x80D, ymax = s16(@0x46)+0xBFF;
w = xmax+1 (+1 if (xmax-1) odd), h = ymax+1. planes @65: 1 = paletted
(+0x0C + 768B trailer), 3 = planar RGB, 4 = interleaved RGBA (.32.pcx).
RLE (all paths): byte <= 0xC0 literal, else run of (byte-0xC0) of next byte.
Runs may straddle row slots; game writes through (later slots overwrite),
so overrun tails are discarded. Pixels at 128.
Usage: python3 tools/pcx.py [file.pcx]  (no arg = verify all)
"""
import glob
import struct
import sys

BIAS_X = 0x80D
BIAS_Y = 0xBFF


def dims(d):
    xmax = struct.unpack("<h", d[0x42:0x44])[0] + BIAS_X
    ymax = struct.unpack("<h", d[0x46:0x48])[0] + BIAS_Y
    w = xmax + 1
    if ((xmax - 1) & 1) != 0:
        w += 1
    return w, ymax + 1


class Stream:
    def __init__(self, d, pos):
        self.d = d
        self.p = pos

    def take(self, n):
        """Next n units; overrun run tails discarded (game overwrite)."""
        out = bytearray()
        d, p = self.d, self.p
        while len(out) < n:
            b = d[p]
            p += 1
            if b <= 0xC0:
                out.append(b)
            else:
                cnt = b - 0xC0
                v = d[p]
                p += 1
                need = n - len(out)
                out += bytes([v]) * min(cnt, need)
        self.p = p
        return bytes(out)


def decode(d):
    assert d[:4] == bytes([10, 5, 1, 8]), "magic"
    assert d[127] == 0x5B, "wrapper"
    planes = d[65]
    assert planes in (1, 3, 4), f"planes={planes}"
    w, h = dims(d)
    assert w > 0 and h > 0, (w, h)
    s = Stream(d, 128)
    if planes == 1:
        idx = s.take(w * h)
        assert len(d) - s.p == 769 and d[s.p] == 0x0C, "palette trailer"
        return ("P", w, h, idx, d[s.p + 1:s.p + 769])
    if planes == 3:
        px = bytearray(w * h * 3)
        for y in range(h):
            # One RLE stream per row (runs cross plane slots, never rows;
            # channels share the row so tails land in place).
            row = s.take(3 * w)
            r, g, b = row[:w], row[w:2 * w], row[2 * w:3 * w]
            for x in range(w):
                o = (y * w + x) * 3
                px[o:o + 3] = bytes((r[x], g[x], b[x]))
        assert s.p == len(d), f"leftover {len(d)-s.p}"
        return ("RGB", w, h, bytes(px), None)
    px = bytearray(w * h * 4)
    for y in range(h):
        row = s.take(w * 4)
        for x in range(w):
            o = (y * w + x) * 4
            px[o:o + 4] = row[4 * x:4 * x + 4]
    assert s.p == len(d), f"leftover {len(d)-s.p}"
    return ("RGBA", w, h, bytes(px), None)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        kind, w, h, px, pal = decode(open(sys.argv[1], "rb").read())
        print(f"{kind} {w}x{h}")
        if kind != "P":
            ch = len(px) // (w * h)
            with open("/tmp/opencode/pcx.ppm", "wb") as f:
                f.write(f"P6\n{w} {h}\n255\n".encode())
                f.write(b"".join(px[i:i + 3] for i in range(0, len(px), ch)))
        sys.exit(0)
    bad = n = 0
    for f in sorted(glob.glob("extracted/**/*.pcx", recursive=True)):
        n += 1
        try:
            decode(open(f, "rb").read())
        except AssertionError as e:
            bad += 1
            print("FAIL", f, e)
    print(f"{n - bad}/{n} pcx decode exactly")
    sys.exit(1 if bad else 0)
