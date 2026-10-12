# File formats — ProPilkki 2

Tool: `tools/ves.py`. All claims verified against 48/48 `extracted/lakes/*.ves`
unless marked conjecture.

## .ves (lake)

Header: `Pro Pilkki datafile v002\nCopyright Procyon Products\n` + blank line.
Then sections; file ends with a trailing u32 (file checksum, algo unknown).

Section framing (ALL sections, uniform — verified byte-exact 48/48):

    <TAG> u32-LE len  payload[len]  u32-trailer

Trailer/checksum (reverse-engineered from `FUN_00430770`, verified):
`H = XOR_j ((byte_j ^ 0x5A) << (j mod 24))` (u32, shifts mod 32).
`tools/ves_hash.py` matches 816/816 section trailers across 48 lakes.
File trailer = H(entire file except last 4 bytes) — also verified.
C++ port: `src/ves.hpp` + `src/test_ves.cpp` (synthetic round-trip +
corruption check; real-lake check = ves_hash.py).
Reader statuses: 0 = hash OK, 5 = mismatch (data still used, flagged via
`local_1300`), 1/2/3/4 = hard fail. Legacy `Pro Pilkki lakefile` format
(mode 1, `FUN_00430d10` sniff): alternating-`NOT` de-obfuscation + byte-sum
hash instead. Unknown sections skipped by embedded length (`len + 4` —
skip covers payload + trailer).

OBJECT_POSITIONS payload: `u32 groupcount`, then groups: `u32 nitems` +
`nitems` × 25-byte records (`u8 0x00 | u8 class | 23 B fields`). Exact-consume
on 48/48 lakes.
Class byte: 0–13 common (< 0x1e: indexed object table), up to 130 (< 0xfb).
Record fields per `FUN_004b6d70`: `[u8 class][u32 x][u32 y][u32 z?][u64 coords?]` —
offsets `param_1+0x207a4` (class < 0x1e) and `+0x1f01c` (class < 0xfb) tables.

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
| .spr (magic `01`, 1122 files) | `u32 0x00010201, u16 w,h, u16 transkey
  (0xF81F/0xF81B), u16 0, u16 w, u16 0`, then `(u16 RGB565, u8 count)` triplets,
  `sum(count)==w*h` — `tools/spr.py` exact (1122/1122);
  C++ port `src/spr.hpp` + `src/test_spr.cpp` | data-derived, no Ghidra |
| .spr (other magics, 91: fonts/frames) | atlas metadata (rect tables) | open |
## .obj (mesh, `FUN_0040d320`, `tools/obj.py` 843/843 exact)

Dual magic: `Pro Pilkki object\0` (v1) then `Pro Pilkki Object\0` (v2, shipped).
18 B magic, then: `u32 nvert, u32 ?, u32 nface, u32 ntex`,
`ntex` × 50 B texture names (`gfx\txt\…\*.PCX`, NUL-terminated),
`0x300` B block, `nvert` × 12 B (xyz float), `nface` × `0x34` B,
`u32 nf`, `nf` × 12 B, footer: `u32` + 6 × `u64` + `0x68` B block
(counts at +8/+12/+16, each × `0x24` B extra records if block starts `U`).
Fooler: `fread(field, 2, 4)` = 8 bytes, not 4 — read decomp carefully.
| .sfo | `OggS` | plain Ogg Vorbis, renamed (`005a0030` voice loader) |
| .sfx | `RIFF/WAVE` | plain PCM16 WAV |
| .mus | `OSND` | chunked Ogg transport (`00401870` demuxer), Vorbis inside |
| .mus | `OSND\0` + `pmusic` | Ogg payload, custom header |
| .pcx | `0A 05` | standard PCX |
| .3ds | `//   Copyright Procyon Products` text header | custom-wrapped |
| data/*.dat | text (`********`, key/value) | configs |
| data/botinfo.dat | `plhplkhk…` | obfuscated binary |
