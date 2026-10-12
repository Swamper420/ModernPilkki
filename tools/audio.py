"""Verify game audio: .sfx = WAV, .sfo = 2x Ogg + 194x OSND, .mus = OSND.

OSND chunk = Ogg page with magic swapped (FUN_00401870 framing:
header = 27 + u8@26 lacing bytes, payload = lacing sum, Ogg CRC32 @22):
rebuild = verify CRC, `OSND`->`OggS`, `pmusic`->`vorbis`, restamp CRC.
199/199 rebuilds ffmpeg-decode clean.
Usage: python3 tools/audio.py
"""
import glob
import struct
import sys


def sniff(path):
    with open(path, "rb") as f:
        magic = f.read(4)
    if magic == b"RIFF":
        return "wav"
    if magic == b"OggS":
        return "ogg"
    if magic == b"OSND":
        return "osnd"
    return "?"


def check_wav(path):
    d = open(path, "rb").read(28)
    assert d[0:4] == b"RIFF" and d[8:12] == b"WAVE" and d[12:16] == b"fmt "
    (audio,) = struct.unpack("<H", d[20:22])
    assert audio == 1, "not PCM"


def check_ogg(path):
    d = open(path, "rb").read(8)
    assert d[0:4] == b"OggS" and d[4] == 0, "not Ogg v0"


def check_osnd(path):
    d = open(path, "rb").read()
    assert d[:4] == b"OSND", "not OSND"
    offs = []
    i = -1
    while True:
        i = d.find(b"OSND", i + 1)
        if i < 0:
            break
        offs.append(i)
    assert offs[0] == 0 and offs[1] == 58, f"header chunk: {offs[:2]}"
    for a, b in zip(offs, offs[1:]):
        assert 58 <= b - a <= 9000, f"chunk gap {b - a}"
    assert len(d) - offs[-1] > 16, "truncated tail"
    return len(offs)


def oggcrc(d):
    poly = 0x04C11DB7
    c = 0
    for b in d:
        c ^= b << 24
        for _ in range(8):
            c = ((c << 1) ^ poly) & 0xFFFFFFFF if c & 0x80000000 \
                else (c << 1) & 0xFFFFFFFF
    return c


def rebuild_osnd(path):
    """Full demux + Ogg rebuild; asserts framing, CRCs, contiguity."""
    d = open(path, "rb").read()
    assert d[:4] == b"OSND", "not OSND"
    out, pos, n = bytearray(), 0, 0
    while pos < len(d):
        chunk = d[pos:]
        assert chunk[:4] == b"OSND", f"gap at {pos}"
        segs = chunk[26]
        hlen = 27 + segs
        plen = sum(chunk[27:27 + segs])
        assert hlen + plen <= len(chunk), f"truncated chunk at {pos}"
        pg = bytearray(chunk[:hlen + plen])
        (stored,) = struct.unpack("<I", pg[22:26])
        pg[22:26] = b"\0\0\0\0"
        assert oggcrc(pg) == stored, f"crc at {pos}"
        pg[0:4] = b"OggS"
        pg = bytearray(bytes(pg).replace(b"pmusic", b"vorbis"))
        pg[22:26] = b"\0\0\0\0"
        struct.pack_into("<I", pg, 22, oggcrc(pg))
        out += pg
        pos += hlen + plen
        n += 1
    return n, bytes(out)


if __name__ == "__main__":
    bad = 0
    counts = {}
    chunks = 0
    rebuilt = 0
    for pat, check in (("extracted/sfx/**/*.sfx", check_wav),
                       ("extracted/sfx/**/*.sfo", None),
                       ("extracted/sfx/**/*.mus", check_osnd)):
        for f in sorted(glob.glob(pat, recursive=True)):
            kind = sniff(f)
            counts[kind] = counts.get(kind, 0) + 1
            try:
                if check is not None:
                    r = check(f)
                    if (isinstance(r, int)):
                        chunks += r
                elif kind == "ogg":
                    check_ogg(f)
                elif kind == "osnd":
                    chunks += check_osnd(f)
                else:
                    raise AssertionError(f"unknown magic in {f}")
                if kind == "osnd":
                    n, ogg = rebuild_osnd(f)
                    assert ogg.startswith(b"OggS") and len(ogg) > 0
                    rebuilt += 1
            except AssertionError as e:
                bad += 1
                print("FAIL", f, e)
    print(f"audio: {counts}, {chunks} OSND chunks total, "
          f"{rebuilt} rebuilt to Ogg")
    sys.exit(1 if bad else 0)
