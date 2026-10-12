"""Parse a Pro Pilkki .ves lake file (uniform grammar, verified 48/48).

Header: b"Pro Pilkki datafile v002\\nCopyright Procyon Products\\n" + blank line.
Every section: <TAG> u32-LE len payload[len] u32-trailer.
Trailer = XOR_j ((byte_j ^ 0x5A) << (j mod 24)) over payload (tools/ves_hash.py).
File ends with u32 trailer = hash of all preceding bytes.
OBJECT_POSITIONS payload = u32 groupcount + groups
(u32 nitems + nitems x 25B records); see docs/file-formats.md.
Usage: python3 tools/ves.py extracted/lakes/meri.ves
"""
import struct
import sys

HDR = b"Pro Pilkki datafile v002\nCopyright Procyon Products\n"


def sections(path):
    data = open(path, "rb").read()
    assert data[: len(HDR)] == HDR, f"bad header: {data[:64]!r}"
    off = len(HDR)
    while data[off : off + 1] in (b"\n", b"\r"):
        off += 1
    out = []
    while len(data) - off > 4:
        assert data[off : off + 1] == b"<", f"expected < at {off}"
        end = data.find(b">", off)
        tag = data[off + 1 : end].decode("ascii")
        (ln,) = struct.unpack("<I", data[end + 1 : end + 5])
        payload = data[end + 5 : end + 5 + ln]
        assert len(payload) == ln, f"truncated {tag}"
        (trailer,) = struct.unpack("<I", data[end + 5 + ln : end + 9 + ln])
        out.append((tag, ln, trailer))
        off = end + 9 + ln
    (file_trailer,) = struct.unpack("<I", data[off : off + 4])
    assert off + 4 == len(data), f"trailing bytes: {len(data)-off}"
    out.append(("FILE_TRAILER", 4, file_trailer))
    return out


if __name__ == "__main__":
    for tag, ln, trailer in sections(sys.argv[1]):
        print(f"{tag:20s} len={ln} trailer={trailer:08x}")
