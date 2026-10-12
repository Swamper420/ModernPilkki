"""Parse a Pro Pilkki .ves lake file.

Grammar (verified on all 48 lakes, see docs/file-formats.md):
  header: b"Pro Pilkki datafile v002\\nCopyright Procyon Products\\n" + blank line
  OBJECT_POSITIONS: u32 count + count records (opaque for now, ends at next tag)
  other sections: <TAG> u32-LE len payload[len] u32-trailer(unknown checksum)
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
        if tag == "OBJECT_POSITIONS":
            (slen,) = struct.unpack("<I", data[end + 1 : end + 5])
            (cnt,) = struct.unpack("<I", data[end + 5 : end + 9])
            # ponytail: record layout unknown, loader decomp will define it;
            # section span verified by DEPTH_TABLE landing on all 48 lakes
            nxt = data.find(b"<DEPTH_TABLE>", end)
            assert nxt - (end + 1) == slen + 8, f"bad OBJECT_POSITIONS span"
            out.append((tag, slen, cnt, None))
            off = nxt
        else:
            (ln,) = struct.unpack("<I", data[end + 1 : end + 5])
            payload = data[end + 5 : end + 5 + ln]
            assert len(payload) == ln, f"truncated {tag}"
            (trailer,) = struct.unpack("<I", data[end + 5 + ln : end + 9 + ln])
            out.append((tag, ln, trailer, None))
            off = end + 9 + ln
    (file_trailer,) = struct.unpack("<I", data[off : off + 4])
    assert off + 4 == len(data), f"trailing bytes: {len(data)-off}"
    out.append(("FILE_TRAILER", 4, file_trailer, None))
    return out


if __name__ == "__main__":
    for tag, ln, extra, _ in sections(sys.argv[1]):
        print(f"{tag:20s} len={ln} extra={extra}")
