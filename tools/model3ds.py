"""Parse .3ds model-assembly scripts (custom text, NOT raw 3DS).

STARTSTRUCTURE = tree of .obj parts: <indent>path p0 p1 p2 p3 index.
Indent depth = tree depth. 99/111 also carry STARTMOVES keyframes,
10/111 STARTSHADOWS pairs -- not parsed yet (open).
Usage: python3 tools/model3ds.py
"""
import glob
import re
import sys

RX = re.compile(r"^( *)(\S+\.obj)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)"
                r"\s+(-?\d+)\s*$")


def structure(path):
    d = open(path, "rb").read().decode("latin1")
    m = re.search(r"<STARTSTRUCTURE>(.*?)<ENDSTRUCTURE>", d, re.S)
    assert m, f"no STRUCTURE in {path}"
    parts = []
    for ln in m.group(1).splitlines():
        s = ln.strip()
        if not s or s.startswith("/") or s.startswith("<"):
            continue
        g = RX.match(ln)
        assert g, f"bad struct line in {path}: {ln!r}"
        parts.append((g.group(2), tuple(map(int, g.group(3, 4, 5, 6))),
                      int(g.group(7)), len(g.group(1))))
    return parts


if __name__ == "__main__":
    bad, nparts = 0, 0
    for f in sorted(glob.glob("extracted/**/*.3ds", recursive=True)):
        try:
            nparts += len(structure(f))
        except AssertionError as e:
            bad += 1
            print("FAIL", e)
    print(f"model3ds: 111 files, {nparts} parts")
    sys.exit(1 if bad else 0)
