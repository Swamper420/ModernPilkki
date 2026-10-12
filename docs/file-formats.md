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

OBJECT_POSITIONS payload: `u32 ngroups`, then groups: `u32 nitems` +
`nitems` × 25-byte records (`u8 flag(0) | u8 class | u16 pad |
i32 x | i32 y | i32 z | u64 tail`). x/y = 8.24 fixed world units
(±128, verified 290431/290431 in range); z small int 0..3750; loader
splits tail u64 into 3×i16. Groups are mixed-class batches (semantics
open). Exact-consume 48/48 lakes (`tools/objpos.py`: 290431 items,
54 classes), C++ port `ves::ParseObjPos` (`src/ves.hpp`).
Class byte: row index into objects.dat NORMAL/SNOW tables (markers
(000)..(140) confirm; all lake classes < 144, 0 oob in 48/48 scenes).

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
- DEPTHMAP/TYPEMAP: `0a 05 01 08` + u32 0 + u16 w,h + u16 200,200 +
  RLE runs (byte < 0xc0 literal, else `(count=b&0x3f)` of next byte;
  encoder overruns last run, decode clips) + 768 B palette (256 RGB;
  gray ramp, or EGA-ish on 16 DEPTHMAPs). 96/96 exact (`tools/ves.py`,
  `ves::ParseGrid`). Cell/palette use open.
- VISUAL_MAP / VISUAL_DEPTH: large blobs (compressed image data;
  contain false-positive `<…>` byte runs — scanners must skip by length).

## Other formats (magic bytes only, not yet parsed)

| ext | magic | note |
|---|---|---|
| .spr (1212 files) | `u8 nframes, bpp(1/2), enc(0/1/2), flags, u16 w,h,key, u16 0` + `u16[nframes]` ×2 tables, runs `(value:bpp, count:u8)*` (`sum==w*h*nframes`), 256×RGB palette if bpp==1 — `tools/spr.py` + `src/spr.hpp` exact (1212/1212, `FUN_0062f750`) |
| .spr (frames.spr only) | enc 2 + 53 B extra header, per-frame dims inside | RLE-interleaved, file unreferenced by binary — open |
## .obj (mesh, `FUN_0040d320`, `tools/obj.py` 843/843 exact)

Dual magic: `Pro Pilkki object\0` (v1) then `Pro Pilkki Object\0` (v2, shipped).
18 B magic, then: `u32 nvert, u32 ?, u32 nface, u32 ntex`,
`ntex` × 50 B texture names (`gfx\txt\…\*.PCX`, NUL-terminated),
`0x300` B block, `nvert` × 12 B verts (3×i32 xyz, NOT float),
`nface` × `0x34` B faces (`u32 idx[3]` + `u32 aux[3]` + `u32 uv[6]`
pixel coords + `u32 flags`; filler uv on untextured faces),
`u32 nf`, `nf` × 12 B, footer: `u32` + 6 × `u64` + `0x68` B block
(counts at +8/+12/+16, each × `0x24` B extra records if block starts `U`).
Fooler: `fread(field, 2, 4)` = 8 bytes, not 4 — read decomp carefully.
C++ port `src/obj.hpp` + `src/test_obj.cpp` (843/843 real files match,
all face indices < nvert). Per-face texture select + aux open;
viewer `src/objview.cpp` draws all faces with one texture.
| .sfo | `OggS` (2: Tuuli*.sfo) or `OSND` (194) | voice: plain Vorbis ×2, rest OSND-chunked like .mus (`005a0030` voice loader / `00401870` demuxer) |
| .sfx | `RIFF/WAVE` | plain PCM16 WAV (18/18) |
| .mus | `OSND\0` + `pmusic` | Ogg pages with magic swapped (`OSND`->`OggS`), `pmusic` for `vorbis`, Ogg CRC32 @22; rebuild verified 199/199 ffmpeg-clean. C++ `audio::OsndToOgg` (`src/audio.hpp` + `src/test_audio.cpp`, `tools/audio.py`) |
| .pcx | `0A 05` | custom variant, fully decoded (`tools/pcx.py` 348/348, `src/pcx.hpp`): box zeroed, dims obfuscated `xmax=s16(@0x42)+0x80D ymax=s16(@0x46)+0xBFF` (`FUN_00600aa0`, `'['@127` wrapper check), RLE threshold `0xC1` (not standard `0xC0`), planes 1 = paletted, 3 = planar RGB, 4 = interleaved RGBA (`.32.pcx`); loader chain `004070e0→0064d920→0064d260→005fff10→005fefd0→005fb040/005fb7c0/005fc360` |
| .3ds | `//   Copyright Procyon Products` text header | model-assembly script (NOT raw 3D): `STARTSTRUCTURE` tree of `.obj` parts (`<indent>path p0 p1 p2 p3 index`, indent = depth; 858 parts) + `STARTMOVES` keyframe funcs (`<FUNCTION=id=name>`, `<NEXT=a[,b]>`, `<LENGTH=ms>`, `<DISABLE=…|->`, `<PART=idx=name>` + `(time,x,y,z)` rows, 768 funcs/20498 keys; 12 files use `<IMPORTMOVES>` path instead) + `STARTSHADOWS` obj/shadow pairs (122) — `tools/model3ds.py` + `src/model3ds.hpp` 111/111 exact; open: `STARTSCALING` (6), `STARTLOWRES`/`FORCEADVPOLY` (1 each) |
| data/*.dat | text (`********`, key/value) | configs |
| data/botinfo.dat | `plhplkhk…` | obfuscated binary |
