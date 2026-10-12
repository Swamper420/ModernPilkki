#!/usr/bin/env python3
# Dump constants (f32/f64/i32) from the original PE by VA.
# Usage: python3 tools/peconst.py 0x6d0590 0x777878 ...
# Clean-room: reads extracted/ProPilkki2.exe (gitignored), prints values only.
import struct
import sys

P = "extracted/ProPilkki2.exe"
d = open(P, "rb").read()
pe = struct.unpack_from("<I", d, 0x3C)[0]
nsec = struct.unpack_from("<H", d, pe + 6)[0]
optsz = struct.unpack_from("<H", d, pe + 20)[0]
opt = pe + 24
imgbase = struct.unpack_from("<I", d, opt + 28)[0]
secs = []
for i in range(nsec):
    o = opt + optsz + 40 * i
    vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", d, o + 8)
    secs.append((d[o:o + 8].rstrip(b"\0").decode(), vaddr, vsize, roff, rsize))


def off(va):
    rva = va - imgbase
    for _, vaddr, vsize, roff, rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return roff + (rva - vaddr)
    return None


for a in sys.argv[1:]:
    va = int(a, 0)
    o = off(va)
    if o is None:
        print(hex(va), "oob")
        continue
    print(hex(va), "f64=%r f32=%r i32=%d" % (
        struct.unpack_from("<d", d, o)[0], struct.unpack_from("<f", d, o)[0],
        struct.unpack_from("<i", d, o)[0]))
