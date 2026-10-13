# Fish stock + bite generation (clean-room analysis)

Slice of `docs/sim.md` §4.4 expanded and **corrected**. Target
`extracted/ProPilkki2.exe` (image VAs). Sources: Ghidra headless dumps in
`/tmp/opencode/sim-dec/` and `/tmp/opencode/bite/dump*.c`, direct
disassembly with `objdump`, constants read with `tools/peconst.py`.
Every table below is byte-verified from `.rdata` unless marked *(inferred)*
or **open**. This document is analysis; the port lives in `src/bite.hpp`.

Corrections vs `docs/sim.md` §4.4 are called out inline. The three big
ones: the 9-step walk does **not** advance its seed (§2), the species
tables' second halves were wrong (§4), and the bite record carries its
own obfuscated weight at **record+0x0c**, not in a separate context (§3).

---

## 1. Fish stock: where the per-cell populations come from

**Answer: fish are loaded from the lake file's `DATA1` section and placed
into the 600-unit cell grid at lake load; nothing is synthesised at bite
time.** The port's current synthetic populations (docs/sim.md delta 20)
are a stand-in for exactly this machinery.

### 1.1 Lake object layout (verified offsets)

| lake offset | meaning |
|---|---|
| `+0x24` | fish id table: rows of 100 `u32` per fish type (`lake+0x24 + (type*100 + i)*4`), read by `004c1390` |
| `+0x125e4 + type*4` | per-type count into that table (0..100) |
| `+0x136d0` | 0x630 (=1584) `u32` RNG seed table (filled with `rand()` unless a seed blob is passed) |
| `+0x150fc / +0x15100 / +0x15108` | month (int), day (int, clamped ≤ 30 in use), clock (double) — inputs to the activity curve `0049de50` |
| `+0x15128` | "stock present" flag; set to 1 by the loader when a fish of kind 2 exists (`FUN_004b6d70`) |
| `+0x15168` | walk/corner grid ptr |
| `+0x1517c / +0x15180` | fish-grid width `w` / height `h` (cells) |
| `+0x1efd8` | depth grid (i32 per cell, `004a5700`) |
| `+0x240c0` | hole lists grid |
| `+0x240c4` | **fish grid**: `w*h` pointers to cell objects; each cell object has the fish `dlist*` at `cell+0xc` (count at `list+0`) |
| `+0x240c8` | cell-range table: `w*h` records of **7 ints**; `rec[0] < x ≤ rec[1]`, `rec[2] < y ≤ rec[3]`, byte `rec+0x10`, flags `rec+0x14`. Also used as the hole grid |
| `+0x240dc` | master `dlist` of all fish entries |
| `+0x28a94 .. +0x28b30` | species table: ~39 pointer slots to 0x310-byte species objects |

### 1.2 Load path (`FUN_004b6d70`, lake loader)

Section reads in order (all `FUN_00430770(name)`): `INFOBLOCK(_V2)`,
`DEPTH_TABLE`, `TYPEMAP`, `DEPTHMAP`, `VIRTUAL_DEPTHMAP`,
`OBJECT_POSITIONS`, `GAMETYPES`, `STRINGINFO_FI/EN`, `START_AREAS`,
`SAVE_COUNTER`, **`DATA1`** (fish), `DATA2`, `POWERLINES`, `VISUAL_*`.

`DATA1` is the fish population. Verified shape of the loading loop at
`004b7f00..004b8340`:

1. `FUN_00430f50(buf, len, key, keylen, flags)` de-obfuscates the payload:
   flag 2 = **byte-reverse**, flag 1 = **XOR with repeating key**
   (`buf[i] ^= key[i % keylen]`) — verified in the 116-byte body. The
   de-obfuscated stream is length-prefixed records
   (`stride = *(u32*)p + 4`) parsed as text lines (`FUN_00431080`,
   100-char token buffer, `"PU_%d"` name pattern). Key and full record
   grammar **open** (record fields not yet decoded; meri.ves `DATA1` =
   68344 B obfuscated, `DATA2` = 4 B).
2. Per record: `operator_new(0xfc0)` = the **fish entry** (4032 B),
   ctor `FUN_005f58b0`, flags via `FUN_005f6da0`, name copy
   `FUN_005fa070` (name → `entry+0x1d8`, count → `entry+0xd90`),
   extra fills `FUN_005fa0a0/FUN_005fa040`.
3. Position slots `entry+0xe54/+0xe58/+0xe5c` (x/y/z, `FUN_005f82f0`).
   Five generic int slots at `entry+0xe40 + i*4` (`FUN_005f9e90`):
   slot 0 = cell index (`FUN_004a9490(x,y)`), slot 3 = fish count.
4. Entry pushed to `lake+0x240dc` and to the cell's fish list under
   `lake+0x240c4[cell] + 0xc`.
5. Fish **move between cells** at runtime: `FUN_004bc340` (per-fish
   migration: new position `FUN_005f82f0`, re-cell via `FUN_004a9490`,
   list splice) and `FUN_004be330` (cell bookkeeping, writes
   `cell+8` float and `cell+4` index through the grid). So per-cell
   populations are dynamic, not static.

### 1.3 Fish entry fields (verified)

| entry offset | meaning |
|---|---|
| `+0x6c` | catch quota counter — decremented (floor 0) in `00479700` when a bite is attached *(inferred: per-fish catch allowance)* |
| `+0x190` | fish **type** (species class id, feeds the type tables) |
| `+0x1d8` | name buffer (`"PU_%d"`) |
| `+0xd90` | count field |
| `+0xd9c` | float used by `005f8ff0` in the special mode branch |
| `+0xe28` | divisor in `005f8ff0`'s encoded ratio `((+0xe2c ^ 0x8220) >> 5) / +0xe28` |
| `+0xe2c` | tamper word; `005f8ff0` requires `(+0xe2c ^ 0x109d) == *(obj+0xe30)` and `(+0xe78*3 ^ 0xffffff) == *(+0xe7c)`, else sets `DAT_007c689c = 1` (anti-cheat trip) |
| `+0xe38` | per-entry factor in `005f8ff0` |
| `+0xe40..+0xe50` | 5 generic int slots (0xe4c = slot 3, copied to record+0x18) |
| `+0xe54/+0xe58/+0xe5c` | x/y/z |
| `+0xac / +0xb0` | type / flag read by `00479700` when attaching to a hole |
| `+0x14`, `s16 +0x24` | weight grams = `+0x14 − s16(+0x24)` (`Invalid weight for fish!` if negative) — written from DATA1 fields **open** |

### 1.4 Species objects and names

`lake+0x28a94 + idx*4` is a **pointer** to a 0x310-byte species object,
not a scalar id. Built in the loader by
`FUN_0059f870(handle, n)` for n = 1,2,3,… where the handle comes from
`FUN_0061e600()` — the data file is `extracted/data/nyky/data0.dat`
("Pro Pilkki datafile v002", same section framing as `.ves`): 9 species
(`<1_x>`..`<9_x>`), 4 sections each (`_1` = 8 B params, `_2`/`_3` =
255-byte u8 curves, `_4` = 255 B constant `0x66`). Curve semantics
**open**; they plausibly feed `0049de50`-style activity.

Species **names** are a string pool in the exe at `0070af30` (8-byte
aligned), in order: `Ahven, Kiiski, Hauki, Kuha, Kirjolohi, Taimen,
Rautu, P.nieriä, H.nieriä, Lohi, Harjus, Siika, Muikku, Kuore, Särki,
Seipi, Lahna, Pasuri, Sulkava, Säyne, Sorva, Salakka, Turpa, Toutain,
Ruutana, Made, Kivinilk., Isosimppu, Miek.särki…`. Name pointers appear
embedded in code at ~`00504100` with stride 128 B (one switch case per
species). The **idx → name mapping is open** *(inferred: the first 10
names map to species idx 0..9 used by the pick tables; data0.dat has
species 1..9, idx 0 unused)*.

## 2. Candidate search (`004ba5e0`, step 2) — walk is a static 3×3

Seed cell = `FUN_004a9490(x, y)`: scans the `lake+0x240c8` cell-range
table (7-int records, strict `rec[0] < x ≤ rec[1]`, `rec[2] < y ≤
rec[3]`), returns `cx + w*cy`, or −1 if no cell contains the point.

The 9-step walk copies a 2×9 (dx,dy) table to the stack and loops i=0..8:

| i | (dx,dy) | source |
|---|---|---|
| 0 | (−1,−1) | `006dd290` |
| 1 | (0,−1) | `006dd294` |
| 2 | (−1,0) | `006dc790` |
| 3 | (0,0) | `006dc794` |
| 4 | (1,−1) | `006dc7a0` |
| 5 | (1,0) | `006dc7a4` |
| 6 | (−1,1) | `006dc7f0` |
| 7 | (0,1) | `006dc7f4` |
| 8 | (1,1) | literals (`local_54 = 1, local_50 = 1`) |

Per step: `cx = dx[i] + seed % w`, `cy = dy[i] + seed / w` — i.e. the
plain **3×3 neighbourhood of the seed cell** (row-major, centre at i=3).

**Correction:** `docs/sim.md` §4.4.2's "the seed for the next step
becomes `cy`" is wrong. The decompiled loop stores the seed in a stack
slot and reloads it at the end of every iteration (`iVar5 =
local_18._4_4_`; asm `mov -0x10(%ebp),%esi` at `004ba71e`), so the seed
is **constant** across the 9 steps. There is no wrap semantics: `seed%w`
and `seed/w` always re-derive the same base cell; only the (dx,dy) pair
changes. The same constant-seed 3×3 walk is used by `004bafa0` and
`004bb280`, which corroborates the reading.

In-bounds cells (`0 ≤ cx < w`, `0 ≤ cy < h`) contribute **every** fish
of the cell list (`grid[w*cy+cx] + 0xc`, count `list+0`). Each fish is
inserted into a selection list at index `rand(current_size)`
(`0059db20` then `0058acc0(list, idx, obj)`), producing a shuffled
selection. Then the selection is scanned in order; for each fish
`005f8ff0(fishEntry, context, out)` is consulted — first acceptance
wins (break). Every *failed* candidate whose `out+0x40` is 0 sets the
"soft-reject seen" flag which suppresses the fallback synthesis (§5).

## 3. Bite record layout (0x70 bytes, ring at `session+0x738`)

Out-buffer of `004ba5e0`, copied to the ring by `004538a0` (words
`0x00..0x15` and `0x1b` only; `0x58..0x68` keep the ring-alloc init).

| rec offset | type | set by | meaning |
|---|---|---|---|
| `+0x00` | u32 | `004ba5e0` | `lake+0x28a94 + idx*4` — species object pointer. Nonzero = "bite happened" flag |
| `+0x04` | i32 | `004ba5e0` | species table index (0..9) |
| `+0x08` | i32 | `005f8ff0` / fallback | fish **type** (`entry+0x190`; fallback forces 2) |
| `+0x0c` | u32 | `00590e70` | **weight v, tampered**: stored `v ^ 0x0E5FAF5F` (`00590c10` reads it back). Candidate: `005f8ff0` rolls `v = max(8, N × 005f8490())`. Fallback: `v = rand(7) + rand(7) + 9` (9..22 g) |
| `+0x10` | f64 | `004ba5e0` | `W = min(v * k_line, cap_line)` |
| `+0x18` | i32 | `005f7680` | `entry+0xe40+3*4` (asm pushes 3); −1 on fallback |
| `+0x20` | f64 | `004ba5e0` | second timing value (§4.2) |
| `+0x28` | f64 | `004ba5e0` | fight bonus (§4.3); 0 on fallback |
| `+0x30` | f64 | `004ba5e0` | catch p (depth band, §4.4) |
| `+0x38` | f64 | `004ba5e0` | fail p (depth band, §4.4) |
| `+0x44/48/4c` | i32 | `004ba5e0` prologue | context x / y / z (ctx `+0x3c/+0x40/+0x44`) |
| `+0x50` | i32 | `004ba5e0` | context `+0x60` |
| `+0x54` | u8 | `004ba5e0` | **outcome byte**: 0 = none, 1 = clean, 2/3 = miss classes (§4.5) |
| `+0x58` | u32 | `00471940` | sequence = ring counter |
| `+0x5c` | u16 | `00590ce0` + `0058ed70` | catch threshold: `rand(80)` + (5 for lines 10/20, −20 for 30/40) |
| `+0x5e` | u16 | `00590ce0` | `rand(400)` |
| `+0x60` | u16 | `00590ce0` | `rand(5) + 1` |
| `+0x64` | i32 | `00590ce0` / copy | `rand(101)` at ring alloc, then overwritten by out word `0x19` (garbage on the fallback path — quirk, see §6) |
| `+0x68..+0x6b` | bytes | `004538a0` | strike-resolution flags (1/2/3/4 outcomes) |
| `+0x6c` | i32 | `004538a0` | outcome class: copy of `+0x54` (handler word 0x1b), zeroed on outcomes 1/2. Read back by `00453620` (1/2/3 branches) |

Catch validation (`00453620`):
`claimed = msg_value / s16(rec+0x60) − s16(rec+0x5e)` (+1 if msg flag
byte) must be `≥ s16(rec+0x5c)` else `Client reported catch, but it is
not a catch!`.

## 4. Record formulas (all constants verified)

### 4.1 Weight `W` (rec+0x10)

`W = min(v * k, cap)` with `v` from rec+0x0c, by `context+0x50`
(the line metres 10/20/30/40 = the `IS_BITING_REQ` `+0x21` value):

| line | k | VA | cap | VA |
|---|---|---|---|---|
| 10 | 0.016666666666666666 (=1/60) | `006d09b8` | 300.0 | `006da020` |
| 20 | 0.008695652173913044 (=1/115) | `006dc5e8` | 30.0 | `007778d8` |
| 30 | 0.00625 (=1/160) | `006dc5d8` | 30.0 | `007778d8` |
| 40 | 0.004347826086956522 (=1/230) | `006dc5d0` | 30.0 | `007778d8` |
| other | 0.011111111111111112 | `006dc5f0` | 30.0 | `007778d8` |

The `other` row logs `Error. Unsupported line:` (`004319e0`) and should
not occur.

### 4.2 Second timing value (rec+0x20)

| condition | value | VA |
|---|---|---|
| `v ≥ 1001` | 0.16666 | `006dc608` |
| `300 < v ≤ 1000` | 0.05 + (v−300) × 0.00016665714285714285 | `006d1618`, `006dc5b0`, `006da020` |
| `v ≤ 300` | 0.05 | `006d1618` |

(Asm: `v < 0x3e9` enters the ramp branch; `299 < v` guards the ramp.
docs/sim.md's `v ≥ 1000` boundary is really `v ≥ 1001`.)

Special case after the outcome block: **line 40 and type ≠ 3**:
`rec+0x20 = 1 − (1 − rec+0x28) * (1 − rec+0x20)`, then `rec+0x28 = 0`
(`00777888 = 1.0`).

### 4.3 Fight bonus (rec+0x28)

`uVar3` and per-line base/scale, by line (and forced to the 40 m row when
`context+8 ≠ −1`):

| line | uVar3 | base (type 3) | scale (type 3) | base (other) | scale (other) |
|---|---|---|---|---|---|
| 10 | 300 | 0.4 (`006d9f88`) | 5.0 (`006db128`) | 0.0 | 6.0 (`006d1648`) |
| 20 | 700 | 0.2 (`006d0568`) | 3.0 (`006d9fc0`) | 0.0 | 1.5 (`006d1638`) |
| 30 | 1200 (0x4b0) | 0.05 *(inferred, stale slot)* | 2.0 (`007778a8`) | 0.0 | 1.0 *(inferred, stale slot)* |
| other (40) | 1500 (0x5dc) | 0.0 | 0.5 (`00777878`) | 0.0 | 0.5 (`00777878`) |

`if (v > uVar3) rec+0x28 = (int)((v − uVar3) / 100) * 0.01 * scale;`
(integer division, `006d0550 = 0.01`), then `rec+0x28 += base`.

### 4.4 Depth-band catch/fail p (rec+0x30 / rec+0x38)

Selected at record-build time by **v** against `uVar3` (not a fixed
150/300 — those are the 10 m row; docs/sim.md table is the line-10
slice of this rule):

| condition | catch p | fail p | VA |
|---|---|---|---|
| `v < uVar3/2` | 0.7 | 0.1 | `006dd0b0` |
| `uVar3/2 ≤ v < uVar3` | 0.9 | 0.4 | `006dd0c0` |
| `v ≥ uVar3` | 0.95 | 0.5 | `006dd0d0` |

### 4.5 Outcome byte (rec+0x54), candidate path only

Ramp `005f77d0(flag, type, v)` = `100 − (int)(frac*100 + 0.5)` clamped
0..100, where `flag = *(context+0x80)` (`00590c00`) selects column A/B
and `frac = (v − lo)/(hi − lo)` over the row for **fish type**
(table `00746278`, stride 16 = `{A_lo, A_hi, B_lo, B_hi}`):

| type | A=[lo,hi] | B=[lo,hi] |
|---|---|---|
| 0 | [0,0] | [0,0] |
| 1, 2 | [800,1450] | [2000,6500] |
| 3 | [1200,2250] | [4200,16500] |
| 4 | [1100,1500] | [2000,8500] |
| 5..10 | [1200,2150] | [3500,7400] |
| 11..16 | [1650,2300] | [4200,16500] |
| 17..19 | [750,1200] | [1750,4200] |
| 20, 21 | [1200,1850] | [3500,7000] |
| 22 | [1650,2300] | [4200,16500] |
| 23, 24 | [1200,1850] | [3500,7000] |
| 25 | [850,1400] | [1750,5200] |
| 26, 27 | [1250,1900] | [2600,7200] |
| 28 | [800,1450] | [2000,6500] |
| 29 | [1650,2300] | [4200,16500] |
| 30 | [850,1400] | [1750,5200] |
| 31, 32 | [1650,2300] | [4200,16500] |

Rules (asm `004baa93..004bab45`, both rolls are **`rand(101)`**):

* `ramp == 100` → byte = **0**.
* else `r = rand(101)`; if `(flag == 1 && v < 16500)` **or** `r < ramp`
  → byte = **1**.
* else threshold by line: 10 → **95** (`0x5f`), 20 → **60** (`0x3c`),
  30 → **30** (`0x1e`), other → **10** if type 3 else **0**.
  `r2 = rand(101)`; byte = `(r2 ≥ thr) + 2` (so 2 or 3).

Fallback synth path: byte = **0**.

### 4.6 Species pick (004acc80, both paths)

`idx = table(type, v)[rand(8)]`, then `species = lake+0x28a94 + idx*4`.
Band by **weight v** for types other than 3/5/0xf/0x16 (verified
`006dc8x0` pairs — each 8-entry table is 4 ints at base ++ 4 ints at the
second VA shown; docs/sim.md §4.4.5's second halves were mis-read as
contiguous memory and are wrong):

| select | table | values (species idx) |
|---|---|---|
| type 3 or 5 | `006dc8a0` ++ `006dc860` | 6 6 6 6 7 7 4 4 |
| type 0xf | `006dc870` ++ `006dc8d0` | 3 4 5 5 9 9 3 8 |
| type 0x16 | `006dc880` ++ `006dc8d0` | 3 9 5 5 9 9 3 8 |
| default, `v < 30` | `006dc820` ++ `006dc8c0` | 1 1 1 2 2 2 3 8 |
| default, `v < 200` | `006dc840` ++ `006dc8e0` | 1 1 2 2 3 4 7 8 |
| default, else | `006dc8b0` ++ `006dc830` | 6 7 7 7 9 1 1 2 |

Fallback synth sets type 2 and rolls `v ∈ [9,22]`, so it always takes
the `default, v < 30` row.

## 5. Fallback synthesis (no candidate), exact gates

Entered only when the candidate search found nothing **and** every
failed candidate was a hard reject (out+0x40 = 1). Asm order
(`004bab4a..004baca4`):

1. `lake+0x15128 != 0` (stock present).
2. `context+0x58 == 0 || == 1`.
3. `!(context+8 < 1 && (context+0x60 < 1 || context+0x68 > 1))`.
4. `rand(110) == 0` (≈0.9 %).
5. `0049de50(2, lake+0x150fc, lake+0x15100, lake+0x15108) > 0.01`
   (`006d0550`). `0049de50` = seasonal/activity curve: 48 half-hour
   slots × day/night interpolation from signed-char tables at
   `0079fef0..007a02b0`, selected by a species/selector id (2 here) and
   a month-ish int (clamp 30). Returns 0..1. (Table contents **open**;
   injected in the port.)
6. Depth gates, `d = 004a5700(x, y, 1)` (bilinear depth at lure),
   `z = context+0x44`:
   * `d ≤ 130` → pass.
   * `130 < d ≤ 250`: `t = 100*(z − 0.25d)/(0.6d)` via `004aba00`
     (clamped 0..100, out-params (100−t, t)); pass iff `t ≥ 81`.
   * `d > 250`: if `z ≤ 0.35d` → reject; if `z ≤ 0.75d` → reject;
     else `t = 100*(z − 0.75d)/(0.1d)`; pass iff `t ≥ 81`.
   (`006dc618 = 0.25`, `006dc628 = 0.35`, `006dc650 = 0.85`,
   `006d1630 = 0.75`.)
7. Synth: `out+8 = 2`, `v = rand(7) + rand(7) + 9`,
   species = §4.6 default/`v<30` row, `out+0x18 = −1`,
   `timing = 0.05`, `bonus = 0`, `W = min(v*k, cap)`, `out+0x50 =
   context+0x60`, outcome = 0. rec+0x30/0x38 are **not written** on
   this path (§6 quirk).

## 6. Quirks worth reproducing (or consciously not)

* rec+0x64 (`00590ce0` writes `rand(101)`) is overwritten by the
  handler's word copy from out+0x19, which `004ba5e0` never fills —
  the ring record carries stack garbage there on both paths.
* Fallback records leave rec+0x30/0x38 untouched; the handler still
  rolls `rand(10000)` against them. The port writes the candidate-path
  initialisers (1.0/1.0) instead of stack garbage; outcome behaviour
  for a fallback fish is then deterministic (always outcome 0 from the
  byte, and the handler rolls resolve to the "no flag" branch).
  `ponytail: deterministic garbage, revisit if a captured original run
  shows different fallback flag rates`.
* `005f8ff0`'s internal probability chain (`005f6210`, `005f7ee0`,
  `00590670`, `005f8a00`, `005f8810`, `005f7900`, `005f76a0`,
  `005f8490`, `00590bf0`, `00590c20`, `0042be90`) is **not ported** —
  it models fish-shyness, lure match, boat/special modes and the size
  roll. It is the per-candidate `bite::Stock::Evaluate` seam.

## 7. Bite attempt (`00479700`) and the server handler

`00479700(session, action, …)`: requires `session+0x52c`, `+0x528`
(lake), `+0x640` set and a valid attached fish (`+0x348` via
`004adad0/004c1300`; then `+0x348 = −1`). Then:

* `roll = rand(1000)`.
* action == 0 ("Ambient" timer): success iff `roll ≥ 500`.
* action != 0: thresholds on the activity value (obfuscated session
  `+0xc`, `00590c10`): `< 500 → 125`, `500..999 → 250`,
  `1000..1699 → 500`, `≥ 1700 → always`. Success iff `roll ≥ thr`.
* On success: filter candidates from list `session+0x338` through
  `00475c70` (per-candidate predicate over min/max ranges at
  candidate+0x24..+0x2a-style fields, −1 = wildcard; details in the
  dump), `pick = rand(count)` of the passers, `candidate+0x6c -= 1`
  (floor 0), old attached fish released, new one from
  `004c1390(candidate+0xac, x, y, candidate+0xb0)`, "Ambient" timer
  killed (`+0x638 = −1`), return 1.

`004c1390(lake, type, x, y, flag)` (fish attached to hole): builds a
record whose `+8` = `lake->fishTab[type*100 + rand(lake->typeCount[type])]`
(`lake+0x24` / `lake+0x125e4`), `+0x10/+0x14` = x/y, `+0x18` = 1.0;
allocates and registers it. Session stores it at `+0x348`.

"Ambient" timer: `00650fe0("Ambient", 0, rand(75000)+20000, 1000,
&+0x638, 8)` created in `0047f740` while `+0x640 != 0 && +0x638 == −1`;
each fire calls `00479700(this, 0, 0, 0, −1)` — the 50 % ambient roll.

Auto-bite percent (`00590a40(line, p2, &out3)`, rolled in `004876d0`
every ≥ 1000 ms as `rand(100) < p` when `session+0x428 > 0`):

| line | return (by p2) | out3 (by p2) |
|---|---|---|
| 10 | p2<5 → 1.6 (`006d6b78`), p2<9 → 1.3 (`006db108`), else 1.0 | >20 → 80, >19 → 70, >17 → 60, >15 → 50, >13 → 40, >12 → 30, >9 → 20 |
| 20 | 1.17 (`0073f500`) | 0 |
| 30 | p2<3 → 0.3 (`006d9f70`), p2<4 → 0.5, p2<5 → 0.7 (`006db0f8`), p2<7 → 0.8 (`006d0580`), else 1.0 | 0 |
| other | p2<5 → 0.05, p2<8 → 0.2, p2<10 → 0.4, else 1.0 | 0 |

## 8. Ring + rate limiter (exact)

* Ring alloc `00471940`: `idx = (idx+1) % 64` (asm: `(x+1) & 0x8000003f`
  sign-fix = MSVC `% 64`), `counter++`, `last = game clock
  (DAT_007c695c)`, record = `session + 0x738 + idx*0x70`, then
  `00590ce0`: `+0x5c = rand(80)`, `+0x5e = rand(400)`,
  `+0x60 = rand(5)+1`, `+0x64 = rand(101)`, `+0x68 = 0`,
  `+0x58 = counter`. Handler then adds `0058ed70(line)` (+5 for 10/20,
  −20 for 30/40) to `s16(+0x5c)`.
* Lookup `00473a70(session, seq)`: newest slot first
  (`session+0x734`), then scan all 64 comparing `rec+0x58`.
* Rate limiter (`004538a0` + `00592160/005922e0`): request gap
  (now − `conn+0x58`) is pushed into a **fixed-capacity sliding sum**
  (struct: `[0] buf, [2] head, [3] cap, [4] count, [5] sum`;
  `Add: sum += gap − buf[head]; buf[head] = gap; head = (head+1) % cap;
  count = min(count+1, cap)`; `Avg = count ? sum/count : 0`). If
  `avg ≥ 900` (`006d6b80`) the bite is built; else `conn+0x60` (violation
  count) increments and `Bite too often! %s (count %d)` is logged when
  the latest gap > 1000 ms. Capacity **open** (set at window init;
  *(inferred: 16)*).

---

## Integration notes (for the orchestrator)

`src/bite.hpp` (namespace `bite`) ports the verified pieces. Proposed
wiring into `src/game.hpp` — exactly what replaces what:

* `game::MakeBiteRecord` → `bite::BuildRecord(ctx, stock, rng, rec)`.
  The 9-cell walk, selection shuffle, record fields, timing, bonus,
  bands, outcome byte and the fallback synth all move there. Delete
  `MakeBiteRecord`'s inline walk and `NextWait`-era leftovers.
* `game::CellCount` / `game::CellWeight` (synthetic per-cell
  populations) → a `bite::Stock` provider. Tonight ships
  `bite::SyntheticStock` (`ponytail: synthetic stock, swap in
  lake-built populations when the loader is ported`). The real provider
  wraps DATA1-built per-cell lists: `Count/TypeAt/WeightAt` from the
  cell fish lists (`lake+0x240c4`), `SpeciesId` from `lake+0x28a94`,
  `DepthAt` from `004a5700`, `CellOf` = `004a9490`, `Evaluate` = the
  `005f8ff0` chain (open).
* `game::SpeciesTableFor(type, depth)` → `bite::SpeciesTable(type, v)`.
  Current code indexes the tables by **depth**; the original indexes by
  **weight v** (and the table values themselves were wrong — §4.6).
* `game::WeightG` → `bite::Weight(v, line)` (same formula, keep).
* `game::Timing32` → `bite::Timing32(v)` (fix boundary: ramp branch is
  `300 < v ≤ 1000`, top value at `v ≥ 1001`).
* `game::CatchBand(depth, …)` → bands are chosen at record build by `v`
  vs `uVar3` (§4.4) and stored in the record; the strike path should
  read `rec.catchP/rec.failP` only (which `StartBite` already does — but
  the values must come from `BuildRecord`, not from a depth lookup).
* `game::BiteThreshold(act)` → `bite::AttemptThreshold(act)` (same
  numbers: 125/250/500/−1 at 500/1000/1700).
* `game::StartBite(action)` → first the `bite::Attempt` roll
  (`rand(1000) ≥ thr`), then `BuildRecord` on success. The Ambient
  timer keeps its `rand(75000)+20000` period and calls
  `Attempt(false, …)`.
* `game::Strike` / `ResolveStrike` → resolution should follow the
  handler's 4-way roll order (fail → escape `004732d0`, catch → land
  `00473320`, else keep hooked), i.e. `rand(10000)` vs
  `rec+0x28/+0x20/+0x38/+0x30` in that order with the `+0x68..0x6b`
  flag writes; current 2-way is an approximation.
* `game::RateGate` → `bite::RateWindow` (exact sliding sum of gaps,
  `avg ≥ 900`).
* New record fields to surface in `game::Bite`: `minW (+0x5c)`,
  `rollOff (+0x5e)`, `rollDiv (+0x60)`, `outcome (+0x54)`,
  `bonus (+0x28)`, `tag (+0x18)` — the catch report
  (`00453620`) validates `msg/rollDiv − rollOff ≥ minW`; `game.hpp`
  currently has none of these.
* `LandFish` (release effect 470/670, hole counter, msg 0x1f) is out of
  this slice and stays as-is.

## Open items

1. `DATA1` record grammar (post-`00430f50` text fields → fish entry
   fields, incl. `+0x14 − s16(+0x24)` weight) — needs the key bytes and
   one decoded sample (a tiny follow-up: dump `FUN_004b6d70`'s parse
   loop operands, already in `/tmp/opencode/bite/dump2.c` lines
   3298-3420).
2. `005f8ff0` probability chain (fish shyness / lure match / size roll
   `005f8490`) — the per-candidate `Evaluate` seam.
3. `0049de50` activity tables (`0079fef0..007a02b0`, 48-slot char
   curves) and the `lake+0x150fc/0x15100/0x15108` clock fields.
4. Species idx → name mapping (string pool `0070af30` found; the
   dispatch around `00504100` not walked).
5. Rate-window capacity constant *(inferred 16)*.
6. `00475c70` candidate predicate field map (session/candidate struct
   semantics of the min/max ranges).
