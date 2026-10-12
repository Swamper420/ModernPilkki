"""Verify OBJECT_POSITIONS payloads (FUN_004b6d70, 48/48 exact-consume).

Payload: u32 ngroups + groups (u32 nitems + nitems x 25B records).
Record: u8 flag(0), u8 class, u16 pad, i32 x, i32 y, i32 z, u64 tail.
x/y = 8.24 fixed world units (+-128); z small int; loader splits tail
u64 into 3xi16 (low 48 bits). Group semantics open (mixed classes).
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
                            _, cls, _, x, y, z = struct.unpack(
                                "<BBHiii", r[:16])
                            assert abs(x / 2**24) <= 128.5, f"x {x}"
                            assert abs(y / 2**24) <= 128.5, f"y {y}"
                except AssertionError as e:
                    bad += 1
                    print("FAIL", f, e)
            off = end + 9 + ln
    print(f"{n - bad}/{n} OBJECT_POSITIONS exact, "
          f"{items} items, {len(classes)} classes")
    sys.exit(1 if bad else 0)
