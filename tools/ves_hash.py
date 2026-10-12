"""Verify .ves section trailers: H = XOR_j ((byte_j ^ 0x5A) << (j mod 24)).

Reverse-engineered from FUN_00430770 (v002/AVX2 path) in ProPilkki2.exe.
Usage: python3 tools/ves_hash.py  (checks all 48 lakes)
"""
import glob
import struct
import sys

sys.path.insert(0, "tools")
from ves import HDR, sections


def section_hash(payload):
    h = 0
    for j, b in enumerate(payload):
        h ^= ((b ^ 0x5A) << (j % 24)) & 0xFFFFFFFF
    return h & 0xFFFFFFFF


def raw_sections(path):
    """Yield (tag, payload, trailer); OBJECT_POSITIONS payload = raw span."""
    data = open(path, "rb").read()
    off = len(HDR)
    while data[off : off + 1] in (b"\n", b"\r"):
        off += 1
    while len(data) - off > 4:
        assert data[off : off + 1] == b"<", f"expected < at {off}"
        end = data.find(b">", off)
        tag = data[off + 1 : end].decode("ascii")
        if tag == "OBJECT_POSITIONS":
            (slen,) = struct.unpack("<I", data[end + 1 : end + 5])
            nxt = data.find(b"<DEPTH_TABLE>", end)
            yield tag, data[end + 1 : nxt], None
            off = nxt
        else:
            (ln,) = struct.unpack("<I", data[end + 1 : end + 5])
            payload = data[end + 5 : end + 5 + ln]
            (trailer,) = struct.unpack("<I", data[end + 5 + ln : end + 9 + ln])
            yield tag, payload, trailer
            off = end + 9 + ln


if __name__ == "__main__":
    bad = n = 0
    for f in sorted(glob.glob("extracted/lakes/*.ves")):
        for tag, payload, trailer in raw_sections(f):
            if tag == "OBJECT_POSITIONS" or trailer is None:
                continue
            n += 1
            if section_hash(payload) != trailer:
                bad += 1
                print("MISMATCH", f, tag)
    print(f"{n - bad}/{n} section hashes match")
    sys.exit(1 if bad else 0)
