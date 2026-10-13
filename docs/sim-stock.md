# DATA1 fish stock loader (clean-room analysis)

Slice of `docs/sim-bite.md` §1 expanded: the **DATA1** deobfuscation, the
fish-entry record grammar, per-cell placement, the species tables from
`data0.dat`, migration, and what `005f8ff0` reads. Target
`extracted/ProPilkki2.exe` (image VAs). Sources: Ghidra headless dumps in
`/tmp/opencode/bite/dump*.c` and `/tmp/opencode/stock/helpers*.c`,
constants read with `tools/peconst.py`, and byte-level verification against
all 48 `extracted/lakes/*.ves`. This document is analysis; the port lives
in `src/fishstock.hpp`.

Marking: verified = traced in decompilation **and** confirmed on real data;
*(inferred)* = reasoned from evidence; **open** = not established.

---

## 1. DATA1 deobfuscation `00430f50` (verified)

Section framing is the standard `.ves` grammar (`docs/file-formats.md`):
`<DATA1> u32-len payload[len] u32-trailer`. `FUN_00430770` verifies the
trailer and returns the payload. The payload is then **deobfuscated
in place**:

```
FUN_00430f50(buf, len, key, keylen, flags)   // 00430f50, 116 B
  if (flags & 2) buf[i] = buf[len-1-i]       // full byte reversal first
  if (flags & 1) buf[i] ^= key[i % keylen]   // then repeating-key XOR
```

Caller for DATA1 (`004b8012..004b8064` in `FUN_004b6d70`) pushes
**flags = 3** (`push 3` at `004b805f`) — byte-reverse **and** XOR, in that
order. The key is the null-terminated string copied from `.rdata`
`006dbfa8` to a stack buffer, keylen = its strlen:

| section | key VA | key bytes |
|---|---|---|
| DATA1 | `006dbfa8` | `fkjsafdBLKJBbljbefwlkj` (22 B) |
| DATA2 | `006dbfd0` | `hjdfHGFAJKGghafkjhg` (19 B) |

The obfuscation is its own inverse only through the flag order: encode =
XOR then reverse; decode = reverse then XOR. Verified by decoding DATA1 of
all 48 lakes: every payload yields a coherent record stream (§2), and
re-encoding reproduces the file bytes exactly.

The same framing/checksum applies to `data0.dat` ("Pro Pilkki datafile
v002", no extra obfuscation — `FUN_0059f870` reads its sections
straight through `FUN_00430770`).

## 2. DATA1 stream framing (verified, 48/48 lakes)

After deobfuscation:

```
u32 count
count x { u32 len; u8 rec[len] }
```

The parse loop (`004b7f00..004b8340`) walks records with
`next = off + *(u32*)(buf+off) + 4`, content at `buf+off+4`, and stops on
count or on a length that runs past the payload. Record length in the wild
is **332 + 44×(area count)**: 332 B (0 spawn areas) or 376 B (1 area);
`rec+0x18` (the area count) agrees with the length in every observed
record. Entry counts per lake: 44..1331 (kuopionlahti 1331, telkanlampi
44); species histograms differ per lake (checked — no two lakes share a
histogram).

Each record is the parameter block of `FUN_005f6da0` (the fish entry
"flags" fill). One record = **one fish** (one `operator_new(0xfc0)`
entry, one position, one cell).

## 3. Record → fish entry field map (verified)

`FUN_005f6da0(this=entry, param_2=record, param_3=1)`, plus
`FUN_005fa2b0` (called from it) and the loader loop. Record offsets, all
`u32` unless noted; entry offsets from `docs/sim-bite.md` §1.3:

| rec offset | entry offset | meaning |
|---|---|---|
| `+0x00` | — | record version (5 in all 48 lakes; versions <3 remap the type through `0049e1d0`, version 4 zeroes `+0x25..0x28`) |
| `+0x04` | — | flags (0x1c observed); bit 0x10 required when version ≥ 6 |
| `+0x08` | `+0x190` | **fish type** 1..32 = species (005fa2b0 writes it if ≠ −1) |
| `+0x0c` | `+0xe78` (+ tamper copy `+0xe7c`) | **attraction radius**, map units (005f7ee0: reach = dist × 0.8333/(r×1.3), reject > 1.2). 200..10500 observed |
| `+0x10` | `+0xe28` | ratio denominator |
| `+0x14` | `+0xe2c = v<<5^0x823f`, `+0xe30 = v<<5^0x92a2` | ratio numerator; 005f8ff0 decodes `((e2c^0x8220)>>5) = v` and uses `v/e28` as a bite factor |
| `+0x18` | — | spawn-area count n (0 or 1 observed) |
| `+0x1c..0x24` (9 B) | `+0xec4..0xee4` (3×3, transposed) | behaviour matrix; version 4 clamps each byte to ±3 of table `00746018` (33 types × 9 B) or ≥ 0x5b, else reseeds from `00746148` |
| `+0x25` | `+0xfb4` (u8) | flag |
| `+0x26` | `+0xfb5` (u8) | flag |
| `+0x28` | `+0xfb8` | int |
| `+0x30 + 32*g`, g=0..4 (3×f64) | `+8 + 24*g` (3×f64) | **weight mixture component g**: (mean, sd, share). 5 components, 8 B pad per record slot. All-zero mixture makes `FUN_005f67b0` fail (−1) |
| `+0x140 + 44*i`, i<n | spawn area i (005fa120) | see §5 |
| `+0x140 + 44*n .. +12` | `+0xe60 / +0xe64 / +0xe68` | 3 trailing ints (meaning **open**; a coordinate-like pair + 0 observed) |

The mixture is a **Gaussian mixture in grams** *(inferred from the per-type
defaults below matching known fish sizes)*: `005f8320(x)` = Σ w·N(x; mean,
sd) / Σ w (`0059c070` = Gaussian kernel), `005f67b0` builds three
inverse-CDF u16 tables at `+0x1d8 / +0x5c0 / +0x9a8` (counts `+0xd90 /
+0xd94 / +0xd98`), and the size roll `005f8490` samples one of the three
(5 % / 90 % / 5 % via `rand(550)` vs 27.5/522.5) and interpolates
neighbouring u16s. That roll is part of the unported `005f8ff0` chain
(`docs/sim-bite.md` §6) — the loader exposes the mixture, the roll stays
in the `bite::Stock::Evaluate` seam.

**Per-type default mixtures** (ctor `005f58b0` copies a 33-row table of
(mean, sd) from `.rdata` `007465c0`+ into the entry, share 1.0; DATA1's
mixture overrides). Row values as pairs, type 0 = (0, 0):

| type | (mean, sd) | type | (mean, sd) | type | (mean, sd) |
|---|---|---|---|---|---|
| 1 | (100, 50) | 12 | (150, 70) | 23 | (100, 50) |
| 2 | (10, 5) | 13 | (30, 10) | 24 | (1000, 300) |
| 3 | (500, 300) | 14 | (20, 10) | 25 | (100, 50) |
| 4 | (500, 300) | 15 | (100, 50) | 26 | (1000, 300) |
| 5 | (1000, 200) | 16 | (100, 50) | 27 | (15, 5) |
| 6 | (800, 300) | 17 | (500, 300) | 28 | (30, 10) |
| 7 | (600, 300) | 18 | (100, 50) | 29 | (100, 50) |
| 8 | (400, 200) | 19 | (100, 50) | 30 | (1000, 300) |
| 9 | (600, 300) | 20 | (100, 50) | 31 | (100, 50) |
| 10 | (1000, 300) | 21 | (100, 50) | 32 | (15, 5) |
| 11 | (150, 70) | 22 | (20, 10) | | |

## 4. Species: type ↔ name and `data0.dat` (verified structure)

**The fish type IS the species** (1..32): the type selects the default
weight row above, the behaviour-matrix row (`00746018`), the species-pick
table class (`004acc80`) and the 29-name switch near `00504100` (string
pool `0070af30`, one name per case, stride 128 B). Name order = pool
order: 1 Ahven, 2 Kiiski, 3 Hauki, 4 Kuha, 5 Kirjolohi, 6 Taimen,
7 Rautu, 8 P.nieriä, 9 H.nieriä, 10 Lohi, 11 Harjus, 12 Siika, 13 Muikku,
14 Kuore, 15 Särki, 16 Seipi, 17 Lahna, 18 Pasuri, 19 Sulkava, 20 Säyne,
21 Sorva, 22 Salakka, 23 Turpa, 24 Toutain, 25 Ruutana, 26 Made,
27 Kivinilk., 28 Isosimppu, 29 Miek.särki… The mapping is corroborated by
the default weight rows (type 2 Kiiski 10 g, 13 Muikku 30 g, 26 Made
1000 g) *(inferred: name alignment, structure verified)*.

Separate from "type", the bite record's species **pick index** (1..9,
`004acc80`) indexes `lake+0x28a94 + idx*4` — pointers to 0x310-byte
species objects built from `extracted/data/nyky/data0.dat` by
`FUN_0059f870`. `data0.dat` = the standard section framing, 9 species × 4
sections `<n_1>`..`<n_4>` (n = 1..9):

| section | payload | species object field | init (0059f870) |
|---|---|---|---|
| `<n_1>` | 8 B, two u32 verbatim | `obj+0x304`, `obj+0x308` | 0 |
| `<n_2>` | 255 B u8 curve | `obj+0x000` (255 B) | 0x18 |
| `<n_3>` | 255 B u8 curve | `obj+0x0ff` (255 B) | 0x4d |
| `<n_4>` | 255 B u8 curve (mostly 0x66) | `obj+0x1fe` (255 B) | 0x66 |
| — | — | `obj+0x30c` | 1.0f |

`<n_1>`'s two u32 semantics **open** (observed `0x83ce83ce, 104` for
species 1; the four bytes look like a repeating pattern but no
deobfuscation is applied on this path). Curve semantics **open**
(plausibly feed the activity machinery, `docs/sim-bite.md` §4.6/§5). How
the 9 species objects relate to the 32 types is **open**; the bite path
only uses the pick tables (values 1..9).

## 5. Placement `005fa100`/`005fa390` and cell mapping (verified)

Spawn area record at `rec+0x140 + 44*i` (`FUN_005f6da0` version ≥ 3 path,
then `FUN_005fa120`):

| area offset | meaning |
|---|---|
| `+0x00` | kind: 1 or 3 = centred shape; other = axis-aligned box |
| `+0x04 / +0x08` | rx / ry (kind 1/3 only; clamped ≥ 50 = 0x32 in 005fa120) |
| `+0x0c / +0x10` | x0 / y0 (box corner 1; kind 1/3: centre) |
| `+0x18 / +0x1c` | x1 / y1 (box corner 2; kind 1/3 unused) |
| `+0x14`, `+0x20..0x27` | unused in this path (**open**) |

`FUN_005fa100` stores the placement budget at `entry+0xfb0` and calls
`FUN_005fa390`: candidate positions from `FUN_005f83d0` (a point in one of
the entry's areas; −1 when none) with fallback `FUN_004ad180`
(lake-wide random), gated by walkable `004af230` and `004a5700(x,y,0) > 30`
(depth), scored by `FUN_005f8580` (best-of up to 5×100 tries); on total
failure the area point is forced. The chosen (x, y, z) lands in
`entry+0xe54/0xe58/0xe5c`, and the loader sets slot 0 = `004a9490(x, y)`
cell index and slot 3 (`+0xe4c`) = master-list index (the bite record's
`tag`).

Verified placement consequences: fish are spread by their record's spawn
area (haapolahti's first fish: box x 41242..54310, y 8621..20361), and
the cell assignment is the strict 600-unit grid lookup (`004a9490`,
`docs/sim-bite.md` §2).

## 6. Migration `004bc340` (verified behaviour, mechanics simplified)

Incremental pass over the master list (`lake+0x240dc`, cursor
`lake+0x28a88`, time-budgeted: stop after > 3000 ms of work or with
< 20 ms left). Per fish: read position (`005f82f0`), and if the stored
cell (slot 0) no longer contains the point (checked against the cell-range
records at `lake+0x240c8`), recompute the cell (`004a9490`) and splice the
fish between cell lists. Then, for every fish in the 3×3 neighbourhood,
`FUN_005f5d60` runs a pair interaction (attraction/repulsion via
`005f8580`/`005f5cd0`, plus kind-specific effects for kinds 2/9/0xb —
presentation-leaning). Positions can therefore drift between cells over
time. **Ceiling for the port:** populations are static after placement
(no pair interactions); `ponytail:` marked in `src/fishstock.hpp` —
upgrade by porting `005f5d60`'s displacement if bite locality over time
matters.

## 7. What `005f8ff0` reads from the entry (verified field list)

Beyond the tamper words (`docs/sim-bite.md` §1.3): `+0x190` (type, out
word `+8`), `+0xe78` (radius, via `005f7ee0`'s distance gate:
`scaled = dist × 0.8333333 / (radius × 1.3)`, reject when `scaled > 1.2`),
`+0xe28 / +0xe2c` (decoded ratio `num/den` multiplier), `+0xe38`
(per-entry factor), `+0xd9c` (special-mode float), the CDF tables via
`005f8490` (size roll, floor 8, max of 1..3 rolls), `+0xe54/0xe58` (x/y
for the distance), `+0xfb0` (placement budget, gates the modifiers).
The rest of the chain (`005f6210`, `005f7ee0` probability shaping,
`00590670`, `005f8a00`, `005f8810`, `005f7900`, `005f76a0`, `00590bf0`,
`00590c20`, `0042be90`) remains the unported `Evaluate` seam.

---

## Integration notes (for the orchestrator)

`src/fishstock.hpp` (namespace `fishstock`) provides `fishstock::Stock`,
a `bite::Stock` implementation built from DATA1 + `data0.dat`:

* `Stock::LoadLake(path)` — reads the `.ves` (via `ves::Parse`),
  deobfuscates `DATA1`, parses entries, and places each fish in its spawn
  area (deterministic given `SetSeed`). Missing/corrupt files degrade to
  an empty stock (returns false, never throws).
* `Stock::LoadSpecies(path)` — `data0.dat` → 9 species objects
  (curves + `<n_1>` params), `SpeciesId(idx)` returns idx 1..9.
* Replacing `bite::SyntheticStock` in `game.hpp`: construct
  `fishstock::Stock` in `SetLake`, call `LoadLake(lakePath)` +
  `LoadSpecies("data/nyky/data0.dat")`, `SetDepth(...)` with the lake's
  depth grid (`004a5700` stand-in), `SetSeed(...)`, `Place()`. Then hand
  it to `bite::BuildRecord` exactly like `SyntheticStock`.
* `WeightAt` = the mixture mean of the first non-zero component (grams),
  falling back to the per-type default mean (§3 table). The bite-time size
  roll should move to `Stock::Evaluate` (it currently rolls the mixture
  there) once the `005f8ff0` chain is ported.
* Fields the `bite::Stock` interface cannot express, to surface later if
  needed: attraction radius (`rec+0x0c`), the num/den ratio
  (`rec+0x10/0x14`), the 9-byte behaviour matrix, the full mixture (only
  its mean fits `WeightAt`), spawn areas, and the trailing `+0xe60..`
  triple. Do not change `bite.hpp` for these on this slice's account.

## Open items

1. `005f8ff0` probability chain and `005f8490` size roll (the
   `Evaluate` seam) — mixture units *(inferred grams)* should be pinned
   there when the chain is ported.
2. `<n_1>` species params (`obj+0x304/+0x308`) semantics; curve
   (`_2/_3/_4`) semantics; the 9 species objects ↔ 32 types relation.
3. `FUN_005f83d0`'s exact area sampling (box uniform vs ellipse for kinds
   1/3) and `FUN_005f8580`'s placement scoring.
4. Trailing entry fields `+0xe60/+0xe64/+0xe68` meaning.
5. `005f5d60` displacement rule (migration pair interaction).
6. Species idx → display-name wiring for the bite record (type names are
   verified; the pick-table idx names remain the §4 mapping question).
