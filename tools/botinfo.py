#!/usr/bin/env python3
"""Verify data/botinfo.dat obfuscation decode (FUN_0059b070).

Cipher: each text-mode line (CR/LF stripped) is nibble pairs:
  pair0: lo=s[0]-'a', hi=s[1]-'i' -> dh=(hi<<4)|lo (chain key)
  mid k: lo=s-'h', hi=s-'c' -> byte=((hi<<4)|lo)^dh
  last pair: (((hi<<4)|lo)^dh) == (dh^0x11) XOR all bytes (checksum)
Nibbles must be <0x10 (unsigned), length even and >=4, else line rejected.

Checks: 505/505 lines checksum-OK, UTF-8 BOM + 'Bot chat' header,
NEW/NAME:/FI:/EN:/ID:/NEXT: script structure, valid UTF-8.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "extracted" / "data" / "botinfo.dat"


def decode(s: bytes) -> bytes:
    assert len(s) >= 4 and len(s) % 2 == 0, f"bad length {len(s)}"
    lo, hi = s[0] - 0x61, s[1] - 0x69  # 'a', 'i'
    assert 0 <= lo < 16 and 0 <= hi < 16, "bad header pair"
    dh = (hi << 4) | lo
    chk = dh ^ 0x11
    out = bytearray()
    for k in range((len(s) - 4) // 2):
        lo, hi = s[2 + 2 * k] - 0x68, s[3 + 2 * k] - 0x63  # 'h', 'c'
        assert 0 <= lo < 16 and 0 <= hi < 16, f"bad pair {k}"
        b = ((hi << 4) | lo) ^ dh
        out.append(b)
        chk ^= b
    lo, hi = s[-2] - 0x68, s[-1] - 0x63
    assert 0 <= lo < 16 and 0 <= hi < 16, "bad checksum pair"
    assert (((hi << 4) | lo) ^ dh) == chk, "checksum mismatch"
    return bytes(out)


def main() -> None:
    raw = SRC.read_bytes()
    lines = raw.split(b"\n")
    assert lines[-1] == b"", "missing trailing newline"
    lines = lines[:-1]
    dec = [decode(l.rstrip(b"\r")) for l in lines]
    print(f"decoded {len(dec)}/{len(lines)} lines checksum-OK")
    assert len(dec) == 505, len(dec)
    assert dec[0] == b"\xef\xbb\xbf" + b"*" * 30 + b"\n", dec[0]
    assert dec[1] == b"* Bot chat conversation file *\n", dec[1]
    text = b"".join(dec).decode("utf-8")  # raises if not valid UTF-8
    toks = re.findall(r"^(NEW|END|NAME:|ID:|DELAY:|FI:|EN:|RU:|UA:|NEXT:)",
                      text, re.M)
    assert toks.count("NEW") == 31 and toks.count("NAME:") == 98, toks
    assert "NEXT:1,2" in text and "Per\xe4j\xe4rvi CUP" in text
    print("header + NEW/NAME:/FI:/EN:/ID:/NEXT: structure sane, UTF-8 valid")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as e:
        print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)
    print("botinfo verify pass")
