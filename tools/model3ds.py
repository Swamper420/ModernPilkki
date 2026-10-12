"""Parse .3ds model-assembly scripts (custom text, NOT raw 3DS).

STARTSTRUCTURE = tree of .obj parts: <indent>path p0 p1 p2 p3 index.
Indent depth = tree depth. STARTMOVES (99/111) = FUNCTION blocks:
<FUNCTION=id=name>, <NEXT=a[,b]>, <LENGTH=ms>, <DISABLE=i,..|->, then
<PART=idx=name> + keyframe rows (time x y z; short rows keep previous
trailing values, extras ignored). STARTSHADOWS (10/111) = obj shadow pairs.
Usage: python3 tools/model3ds.py
"""
import glob
import re
import sys

RX = re.compile(r"^( *)(\S+\.obj)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)"
                r"\s+(-?\d+)\s*$")
TAG = re.compile(r"<(FUNCTION=\d+=[^>]*|NEXT=[\d,\-]+|LENGTH=\d+"
                 r"|DISABLE=[\d,\-]+|PART=\d+=[^>]*)>\s*")


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


def moves(path):
    d = open(path, "rb").read().decode("latin1")
    m = re.search(r"<STARTMOVES>(.*?)<ENDMOVES>", d, re.S)
    assert m, f"no MOVES in {path}"
    nfunc = npart = nkf = 0
    for ln in m.group(1).splitlines():
        s = ln.strip()
        if not s or s.startswith("*"):
            continue
        if s.startswith("<"):
            assert TAG.fullmatch(s), f"bad moves tag in {path}: {ln!r}"
            nfunc += s.startswith("<FUNCTION=")
            npart += s.startswith("<PART=")
        else:
            toks = s.split()
            assert toks and all(t.lstrip("-").isdigit() for t in toks), \
                f"bad keyframe in {path}: {ln!r}"
            nkf += 1
    return nfunc, npart, nkf


def importmoves(path):
    d = open(path, "rb").read().decode("latin1")
    m = re.search(r"<IMPORTMOVES>(.*?)<ENDIMPORTMOVES>", d, re.S)
    assert m, f"no IMPORTMOVES in {path}"
    s = m.group(1).strip()
    assert s.endswith(".3ds") and "\n" not in s, \
        f"bad import in {path}: {s!r}"
    return s


def shadows(path):
    d = open(path, "rb").read().decode("latin1")
    m = re.search(r"<STARTSHADOWS>(.*?)<ENDSHADOWS>", d, re.S)
    assert m, f"no SHADOWS in {path}"
    pairs = []
    for ln in m.group(1).splitlines():
        s = ln.strip()
        if not s:
            continue
        t = s.split()
        assert len(t) == 2 and all(p.endswith(".obj") for p in t), \
            f"bad shadow pair in {path}: {ln!r}"
        pairs.append(tuple(t))
    return pairs


if __name__ == "__main__":
    bad, nparts = 0, 0
    nfunc = npart = nkf = npair = nmove = nshad = nimp = 0
    for f in sorted(glob.glob("extracted/**/*.3ds", recursive=True)):
        try:
            nparts += len(structure(f))
            try:
                a, b, c = moves(f)
                nmove += 1
                nfunc += a
                npart += b
                nkf += c
            except AssertionError as e:
                if "no MOVES" not in str(e):
                    raise
                importmoves(f)
                nimp += 1
            try:
                npair += len(shadows(f))
                nshad += 1
            except AssertionError as e:
                if "no SHADOWS" not in str(e):
                    raise
        except AssertionError as e:
            bad += 1
            print("FAIL", e)
    print(f"model3ds: 111 files, {nparts} parts, "
          f"{nfunc} funcs/{npart} mparts/{nkf} keys in {nmove} MOVES "
          f"(+{nimp} IMPORTMOVES), "
          f"{npair} shadow pairs in {nshad} SHADOWS")
    sys.exit(1 if bad else 0)
