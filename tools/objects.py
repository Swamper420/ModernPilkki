"""Parse data/objects.dat: NORMAL/SNOW blocks of 144 class rows.

Row: angle scale daylight shadow [file] [desc]; file is a gfx path,
<automatic>, disabled, or empty. Row index = OBJECT_POSITIONS class byte
(markers (000)..(140) every 10 confirm the mapping).
Usage: python3 tools/objects.py
"""
import glob
import re
import sys

MARK = re.compile(r"\((\d{3})\)")


def blocks(path):
    lines = open(path, "rb").read().decode("latin1").splitlines()
    out = []
    i = 0
    while i < len(lines):
        if lines[i].startswith("*"):
            assert lines[i + 2].startswith("*"), f"bad block hdr in {path}"
            name = lines[i + 1].strip()
            rows = []
            i += 3
            while i < len(lines) and not lines[i].startswith("*"):
                s = lines[i].strip()
                i += 1
                if not s or s.startswith("Angle") or s.startswith("---"):
                    continue
                toks = s.split()
                assert len(toks) >= 4, f"short row in {path}: {s!r}"
                angle = int(toks[0])
                scale = float(toks[1])
                dl, sh = int(toks[2]), int(toks[3])
                rest = toks[4:]
                if rest and (rest[0] == "<automatic>" or rest[0] == "disabled"
                             or rest[0].startswith("gfx")):
                    rows.append((angle, scale, dl, sh, rest[0],
                                 " ".join(rest[1:])))
                else:
                    rows.append((angle, scale, dl, sh, "", " ".join(rest)))
            out.append((name, rows))
        else:
            assert not lines[i].strip(), f"junk in {path}: {lines[i]!r}"
            i += 1
    return out


if __name__ == "__main__":
    bad = 0
    for f in sorted(glob.glob("extracted/data/objects.dat")):
        try:
            bs = blocks(f)
            assert [n for n, _ in bs] == ["NORMAL", "SNOW"], "blocks"
            for name, rows in bs:
                assert len(rows) == 144, f"{name}: {len(rows)} rows"
                for idx, r in enumerate(rows):
                    m = MARK.search(r[5])
                    if m:
                        assert int(m.group(1)) == idx, \
                            f"{name} row {idx}: {r[5]!r}"
        except AssertionError as e:
            bad += 1
            print("FAIL", f, e)
    print("objects: NORMAL+SNOW x144 rows, markers exact")
    sys.exit(1 if bad else 0)
