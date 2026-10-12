"""Verify OBJECT_POSITIONS payloads (FUN_004b6d70, 48/48 exact-consume).

Payload: u32 ngroups + groups (u32 nitems + nitems x 25B records).
Record: u8 0x00, u8 class, u32 a, u32 b, u32 c, 11B tail.
Field semantics open (a/b huge as u32, c small ~300s); exact framing verified.
Usage: python3 tools/objpos.py
"""
import glob
import struct
import sys

sys.path.insert(0, "tools")
from ves import HDR


def groups(payload):
    (ng,) = struct.unpack("<I", payload[:4])
    off = 4
    out = []
    for _ in range(ng):
        (ni,) = struct.unpack("<I", payload[off:off + 4])
        off += 4
        recs = []
        for _ in range(ni):
            rec = payload[off:off + 25]
            assert len(rec) == 25, "truncated record"
            assert rec[0] == 0, f"record magic {rec.hex()}"
            recs.append(rec)
            off += 25
        out.append(recs)
    assert off == len(payload), f"leftover {len(payload)-off}"
    return out


if __name__ == "__main__":
    bad = n = items = 0
    classes = set()
    for f in sorted(glob.glob("extracted/lakes/*.ves")):
        data = open(f, "rb").read()
        off = len(HDR)
        while data[off:off + 1] in (b"\n", b"\r"):
            off += 1
        while len(data) - off > 4:
            end = data.find(b">", off)
            tag = data[off + 1:end].decode("ascii")
            (ln,) = struct.unpack("<I", data[end + 1:end + 5])
            payload = data[end + 5:end + 5 + ln]
            if tag == "OBJECT_POSITIONS":
                n += 1
                try:
                    for recs in groups(payload):
                        items += len(recs)
                        for r in recs:
                            classes.add(r[1])
                except AssertionError as e:
                    bad += 1
                    print("FAIL", f, e)
            off = end + 9 + ln
    print(f"{n - bad}/{n} OBJECT_POSITIONS exact, "
          f"{items} items, {len(classes)} classes")
    sys.exit(1 if bad else 0)
