"""Verify game audio: .sfx = WAV, .sfo = 2x Ogg + 194x OSND, .mus = OSND.

OSND framing (FUN_00401870 demuxer, full page rebuild TBD):
file = OSND chunks back-to-back, first chunk 58 B header.
Chunk payloads carry Vorbis packets with 'pmusic' in place of 'vorbis'.
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


if __name__ == "__main__":
    bad = 0
    counts = {}
    chunks = 0
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
            except AssertionError as e:
                bad += 1
                print("FAIL", f, e)
    print(f"audio: {counts}, {chunks} OSND chunks total")
    sys.exit(1 if bad else 0)
