# File formats — ProPilkki 2

Tool: `tools/ves.py`. All claims verified against 48/48 `extracted/lakes/*.ves`
unless marked conjecture.

## .ves (lake)

Header: `Pro Pilkki datafile v002\nCopyright Procyon Products\n` + blank line.
Then sections; file ends with a trailing u32 (file checksum, algo unknown).

Section framing (all except OBJECT_POSITIONS):

    <TAG> u32-LE len  payload[len]  u32-trailer

Trailer/checksum (reverse-engineered from `FUN_00430770`, verified):
`H = XOR_j ((byte_j ^ 0x5A) << (j mod 24))` (u32, shifts mod 32).
`tools/ves_hash.py` matches 768/768 section trailers across 48 lakes.
File trailer = H(entire file except last 4 bytes) — also verified.
Reader statuses: 0 = hash OK, 5 = mismatch (data still used, flagged via
`local_1300`), 1/2/3/4 = hard fail. Legacy `Pro Pilkki lakefile` format
(mode 1, `FUN_00430d10` sniff): alternating-`NOT` de-obfuscation + byte-sum
hash instead. Unknown sections skipped by embedded length (`len + 4` —
skip covers payload + trailer).

OBJECT_POSITIONS framing: `<OBJECT_POSITIONS> u32 slen u32 groupcount`,
then groups: `u32 nitems` + `nitems` × 25-byte records
(`u8 0x00 | u8 class | 23 B fields`). Exact-consume on 48/48 lakes.
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
| .spr | `01 02 01 00 …` | custom sprite |
| .obj | `Pro Pilkki Object\0` + texture path (`gfx\txt\…\*.PCX`) | custom model |
| .sfo | `OggS` | plain Ogg Vorbis, renamed |
| .mus | `OSND\0` + `pmusic` | Ogg payload, custom header |
| .pcx | `0A 05` | standard PCX |
| .3ds | `//   Copyright Procyon Products` text header | custom-wrapped |
| data/*.dat | text (`********`, key/value) | configs |
| data/botinfo.dat | `plhplkhk…` | obfuscated binary |
