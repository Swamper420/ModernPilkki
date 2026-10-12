# File formats — ProPilkki 2

Tool: `tools/ves.py`. All claims verified against 48/48 `extracted/lakes/*.ves`
unless marked conjecture.

## .ves (lake)

Header: `Pro Pilkki datafile v002\nCopyright Procyon Products\n` + blank line.
Then sections; file ends with a trailing u32 (file checksum, algo unknown).

Section framing (all except OBJECT_POSITIONS):

    <TAG> u32-LE len  payload[len]  u32-trailer

Trailer algo unknown (not zlib-crc32/adler/FNV over payload or tag+payload —
`ponytail:` loader decomp will name it; writer can emit 0 until then).

OBJECT_POSITIONS framing: `<OBJECT_POSITIONS> u32 slen u32 count records…`
with `DEPTH_TABLE` landing at exactly `slen+8` after the tag end (48/48).
Record layout unknown (variable-length; meri: count=256 tiny recs,
Surrinlampi: count=36 ~669 B recs — likely polygons/markers).

Core section order (stable; INFOBLOCK_V2 variant in some lakes):

    OBJECT_POSITIONS DEPTH_TABLE DEPTHMAP TYPEMAP INFOBLOCK GAMETYPES
    VERSIONS STRINGINFO_FI STRINGINFO_EN START_AREAS DATA1 DATA2
    POWERLINES VISUAL_MAP_INFO VISUAL_MAP VISUAL_DEPTH SAVE_COUNTER

Decoded payloads (meri.ves):

- DEPTH_TABLE (68 B): 17×u32 thresholds
  `0 30 60 130 160 200 250 300 350 400 500 700 900 1000 1500 2000 2500`.
  Identical in all 48 lakes.
- VERSIONS (16 B): `9a 99 c9 40 | 6d e7 8b 3f | 8×0` = float 6.3, float ~1.09,
  u64 0 (conjecture: editor/data version).
- INFOBLOCK (27 B): NUL-separated latin1 text:
  `V0.9` `9.7.2009` `MH` `Nimetön` + flags `00 01`.
- GAMETYPES (100 B): per-type flag bytes + `c` padding.
- START_AREAS (42 B): u16 count=2 + coordinate u32s (unverified field split).
- STRINGINFO_FI/EN: NUL-separated latin1 strings, `\n` line breaks.
  FI: name, region, description. EN: `No name` placeholder in some lakes.
- SAVE_COUNTER (8 B): two u32s (counter + ?).
- DEPTHMAP/TYPEMAP: binary grids (dims in first bytes, RLE-ish).
- VISUAL_MAP / VISUAL_DEPTH: large blobs (compressed image data;
  contain false-positive `<…>` byte runs — scanners must skip by length).

## Other formats (magic bytes only, not yet parsed)

| ext | magic | note |
|---|---|---|
| .spr | `01 02 01 00 …` | custom sprite |
| .obj | `Pro Pilkki Object\0` + texture path (`gfx\txt\…\*.PCX`) | custom model |
| .sfo | `OggS` | plain Ogg Vorbis, renamed |
| .mus | `OSND\0` + `pmusic` | Ogg payload, custom header |
| .pcx | `0A 05` | standard PCX |
| .3ds | `//   Copyright Procyon Products` text header | custom-wrapped |
| data/*.dat | text (`********`, key/value) | configs |
| data/botinfo.dat | `plhplkhk…` | obfuscated binary |
