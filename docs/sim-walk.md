# Click-to-move, walk grid, input commands — verified analysis (clean-room)

Target: `extracted/ProPilkki2.exe`. Image VAs (`0047f530` = `0x0047f530`).
Dumps: `/tmp/opencode/sim-dec/` + `/tmp/opencode/walk/dump1-6.c` (Ghidra 12
headless, project `/tmp/opencode/ghidra-walk`). Jump table read from raw PE
bytes. Claims marked *(inferred)* or **open** are not decompiled-verified.

Companion code: `src/walkgrid.hpp` (rule + click decision), `src/test_walkgrid.cpp`.
Relevant deltas: docs/sim.md 2 (click-to-move), 3 (world grid), 20 (input).

---

## 1. Click-to-move semantics — verdict

**What exactly happens when the player clicks outside the walkable area?**

**The click is ACCEPTED (never rejected, never clamped), and the walk is then
blocked mid-walk by the walk grid.** The failing `test_game` expectation
(walk starts, stops at the wall at `(w-1)*600`, last legal step kept) matches
the original; `game::Click`'s out-of-bounds rejection was the bug and is
fixed. Chain of evidence:

1. **Order entry `0047f530(session, x, y)`** (raw disassembly): the only
   rejection is `x < 0 || y < 0` (two `js` guards). No upper bound, no grid
   test. It stores the target at `+0x324/+0x328`, frees the route list
   `+0x334`, and enqueues action `0xe` (`00471560`, gated by `+0x69c`).
   The 1500-unit minimum click distance (`00799154`, skipped when `+0x698`
   "arrived") gates only the voice/chat acks (`004993c0(session, 1|2)`),
   **not** the order *(refined: docs/sim.md delta 2 lists it as a click gate;
   kept as a gate in the port per that contract)*.
2. **Route check `004b0bb0(lake, x, y, holeId)`** returns 0 (reject) only
   when (a) the point lies inside an object's obstacle triangles (per-object
   triangle lists, `0059d450` point-in-triangle) or (b) within 300 units of
   an existing hole (`0059d8c0`) other than `holeId`. It never consults the
   walk grid or map bounds — a point far outside the lake is in no section
   bbox and passes. Callers: the `0063ca80` mouse paths, `00476980`
   (click-dispatch), `004b0a70` (spot validity), the router `004a9c50`.
3. **`004af230` at click time** (`0063ca80:801`) only selects the marker:
   not walkable → marker `0xe` + fishing latch `7c8794 = 0`; walkable →
   marker `0xf` + latch 1. The walk order is issued before this test and is
   not affected by it.
4. **Walk step `004770a0`** moves `pos += dir(heading°) · 0.34 · dt` with a
   `>= 0` clamp only — no grid check anywhere in the per-slice walk path.
5. **The grid stops the walk**: movement is routed (`004a9c50` planner →
   A* `004a9120` wave over `004a9710` expansions, LOS `004b0f60`, smoothing
   `004bb1b0`), and routes only exist between passable corners; the AI
   walk-target finder `004703f0` rejects candidates with `004af230 == 0`
   and discards empty routes (`+0x330 = 1` = no route). World extents are
   literally `(w-1)*600 × (h-1)*600` (recorded by `004b8b20` at
   `lake+0x15160/+0x15164`), so the walk cannot leave the grid — the
   walker stops at the last passable position. How the request handler
   reports an empty route (walk-to-closest vs no walk) is *(inferred)*:
   the observable bound "stops at the wall" holds either way.

Other verified click-path pieces:

* **Hole snap** (mouse path + `00476c00`): `004aeee0(x,y)` finds the nearest
  open hole (record `[3] == 0`) with squared-distance bound 90000 (300
  units); `004af1b0(hole)` returns record `+0x10` (busy/claimed) — nonzero
  drops the order (mouse path) or cancels (`0047f4a0`, action `0xc`);
  `004ac100` returns the hole position. `00476c00` then sets `+0x4bc = 1`,
  snap `+0x4b4/+0x4b8` = hole, registers callback `004aed70(hole, sprite,
  00471e10, session)`, and retargets `+0x4a0/+0x4a4` to
  `hole + unit(pos-hole) * 90` (stand-off, `006da098 = -90` with the sign
  worked through the subtraction). Clicking the session's own current hole
  (`+0x4c0`, `00476c00`) is a no-op.
* **Fishing-mode clicks** (modes `0x18/24/25/26/27/28`): `00476c00` stores a
  reel target `+0x4d8/+0x4dc`, may fire `00477880` (line stage, mode 0x25)
  and a "Nosto" timer (`00650fe0("Nosto",0,1000,...,8)`), and defers the
  walk (`+0x4d4 = 1`) until mode 5.
* **Arrival / guidance** (`0047f740`, modes 0/1/2 block): distance to the
  waypoint `+0x4ac/+0x4b0` `<= 360000` (600²) sets `+0x698 = 1` ("arrived",
  also unlocks the min-distance gate), else `+0x690 = 0`; distance to the
  target `+0x4a0/+0x4a4` `< 32400` (180²) fires arrival action 4 once
  (`+0x5e0` latch). The walk advances every slice regardless — arrival is a
  flag, not a pre-step gate (port `WalkStepToTarget` now matches).
* **`00476980` (click dispatch, used by mouse + AI)**: mode guards
  (`0x20-0x22`, `0x12-0x14` reject), `004b0bb0(x,y,-1)` + `00439940(x,y,300)`
  ("crowded" check), then enqueues action `2` with (x,y) (`00630040`).
  Action 2 is consumed by `00479000` (sets waypoint `+0x4ac/+0x4b0`,
  sends the walk request `00597740`); the reply route is applied by
  `00474ab0` (bearing `+0x45c = (deg+450)%360`, then `00476c00` on the
  first waypoint). `00474ab0` is also the net path (`00478d90` handler).
* **`004b0a70`** (spot validity, returns 0 crowded / 1 ok / 2 route-blocked):
  hole lookup first, else gather entities within `param_5` (default 500) via
  `00437ce0`/`00476120`, then `004b0bb0`.

### Failing-test resolution

`test_game` "walk step math / grid bounds stop the walk" encoded exactly the
verified behavior (OOB click accepted, walk stops at `(w-1)*600`). Fixed in
code: `game.hpp::Click` no longer rejects out-of-bounds targets (decision
now via `walkgrid::Decide`). Two further `test_game` asserts were broken but
masked behind the line-105 abort, and were re-pointed to verified semantics:

* Mode after line-out is `0x25` (line stage, `00477880`); the at-hole pose
  `0x1e` follows on the next slice — the test asserted `0x1e` at line-out.
* The "small click ignored" point sat 129 units from the just-drilled hole,
  inside the verified 300-unit snap ring (`004aeee0`) where the original
  snaps; the click was moved clear of the ring so it exercises the 1500-unit
  minimum-click gate it was written for. The on-hole snap assert is
  unchanged.

## 2. Walk grid

### Structure and build source

`004af230` addressing: `lake+0x15168` → owner object → grid struct
`{ +0x500 data, +0x504 w, +0x508 h }`; `data` is one byte per grid point,
row-major `[y*w + x]`. Byte bits: **`0x10` — corner passable when CLEAR**
(walkability); `0x20` — gate bit for the ground-height lookup `004b15d0`.

Built at lake load: `004b6d70` (the `.ves` section reader; "TYPEMAP" /
"DEPTHMAP" / "VIRTUAL_DEPTHMAP" strings at `0x6dbebc` / `0x6dbedc`) calls
**`004b8b20(depth.dat path, type.dat path, w, h)`**, which allocates the
grid via `0049e490(max(20,w), max(20,h))` when absent (dimensions come from
VIRTUAL_DEPTHMAP when that section exists, else the 20×20 floor), fills it
through **`004b6990`** from `data/type.dat` + `data/depth.dat`
classification tables (`0061f060` path build; neither file ships in the
installer — `extracted/data/` has only `botinfo.dat`, `nyky`, `objects.dat`,
so the defaults live inside `004b6990`), then records the world extents:

    lake+0x15158/0x1515c = w-1 / h-1        lake+0x1516c/0x15170 = 600 / 600
    lake+0x15160/0x15164 = (w-1)*600 / (h-1)*600     lake+0x15174 = 2000

Exact section-byte-to-flag binding inside `004b6990` is **open** (TYPEMAP is
the only byte-per-cell lake section and is the leading candidate for the
source cells *(inferred)*). The .ves payload itself is the already-parsed
RLE grid (docs/file-formats.md TYPEMAP/DEPTHMAP).

### Walkability rule (`004af230`, verified incl. jump table `0x004af4d4`)

`cellX = x/600`, `cellY = y/600` (truncating). Corners are sampled at
`clamp(cellX, 0, w-1)` / `clamp(cellX+1, 0, w-1)` and the same for y — each
corner clamps independently. `u = x - cellX*600`, `v = y - cellY*600`
(0..599). Mask bits: **8 = corner (0,0), 4 = (600,0), 2 = (600,600),
1 = (0,600)**; a bit is set when that corner's `0x10` bit is **clear**
(passable). Return 1 (walkable) per mask:

| mask | set (passable) corners | walkable iff |
|---|---|---|
| 0 | — | never |
| 1 | (0,600) | `u<=300 && v>=300 && u-v+600 < 300` |
| 2 | (600,600) | `u>=300 && v>=300 && u+v > 900` |
| 3 | (0,600),(600,600) | `v > 300` |
| 4 | (600,0) | `u>=300 && v<=300 && v-u+600 < 300` |
| 5 | (0,600),(600,0) | never (opposite pair) |
| 6 | (600,0),(600,600) | `u > 300` |
| 7 | all but (0,0) | `u>300 || v>300 || u+v >= 300` |
| 8 | (0,0) | `u<300 && v<300 && u+v < 300` |
| 9 | (0,0),(0,600) | `u < 300` |
| 10 | (0,0),(600,600) | never (opposite pair) |
| 11 | all but (600,0) | `u<300 || v>300 || v-u+600 >= 300` |
| 12 | (0,0),(600,0) | `v < 300` |
| 13 | all but (600,600) | `u<300 || v<300 || u+v <= 900` |
| 14 | all but (0,600) | `u>300 || v<300 || u-v+600 >= 300` |
| 15 | all | always |

Comparators are exact (verified against the disassembly): the 300 splits are
strict on the minority side (`setg`/`setl`), the 900 diagonals give the
boundary to the three-corner side. Layout: straight half-cell split for
2/3/6/9/12/15, triangles for single corners, `u+v`/`v-u` diagonals for the
mixed pairs. Note the *passable* side is the side of the passable corners —
`0x10` set means the corner is a blocked corner.

Port deviation (marked in `walkgrid.hpp`): cells outside `[0,w)×[0,h)` are
hard-blocked. `004af230` literally clamps the corner samples and would let a
far-out point extrapolate the edge corners; blocking OOB matches the world
extents and the wall the click path relies on.

### Where else the grid is consulted (32 call sites)

Bite generation validity (`004ba5e0`), depth `004a5700`, hole functions
(`004ad*`), zone test `004b08c0`, footstep flag `+0x2339` in `0047f740`,
the marker/latch pick in `0063ca80`, router family (`004abc40` edge test),
AI target picking `004703f0`. Never inside the walk step.

## 3. VK / command input

**There is no key→command map.** The two input layers are:

1. **Raw VK event queue** `7bcd60` (count `7bdd00`; `0049e310` non-empty,
   `0049e2c0` pop, `0049e320` push-back, `0049e2b0` clear). WndProc
   `005f3820` pushes key events through `0049e1e0(vk, isChar)` (OEM remaps
   `0xF6→0x94, 0xE4→0x84, 0xD6→0x99, 0xC4→0x8E, 0xE5→0x86, 0xC5→0x8F`) and
   mouse buttons/wheel through `0049e270(n)` as `n+1000`. Keys are consumed
   as raw codes (docs/sim.md §3.2 table):

   | event | effect |
   |---|---|
   | `0x21/0x22/0x23` PgUp/PgDn/End | `0047ae90` rod/lure slot release (slots 0/1/2) |
   | `0x31/0x32/0x33` keys `1`/`2`/`3` | `0047ae90` rod/lure slot press |
   | `0x409/0x40a` wheel ± | camera zoom `7a6b7c ± 100`, clamped |
   | `0x45c` F5 | chat/notes path |
   | `0x45f/0x460` F6/F7 | scoreboard / player cycle |
   | `0x20` **Space** | `004876d0` pops the queue and returns action bit `3` (primary/cancel cluster — same as commands 5/6) |
   | `0x1b` Esc | `0063ca80`: `7c8790 = 0x14`, state → 4 (pause) |
   | default | re-queued (`0049e320`) for other screens |

   *(Note: docs/sim.md §3.3 labels `0x20` "ESC"; VK `0x20` is Space — Esc is
   `0x1b`.)*

2. **`menuObj+0x28` "commands"** — `00495930` is a 4-byte getter
   (`mov eax,[ecx+0x28]; ret`) on the UI list widget: **`+0x28` is the
   currently selected item index (-1 = none)**. It is written by the widget's
   mouse handler when a drawn item is clicked (`00416150` lays out the item
   strings; `00594d60/00594d40` count/get items; writer in the `00594xxx`
   widget family — exact writer **open**), read through `00595220` (mouse
   held over item), `00595280`/`005952b0` (click state machine). The
   `004876d0` action-bitmask switch is:

   | cmd (`00495930`) | action bit | effect |
   |---|---|---|
   | 0 | — | (no mapping) |
   | 1 | `0x2000` (states 0x14/0x16/0x19) | else: pause menu (`0043a190/0043bd10`, `7c6904 = 0x14`) in states 1/2/0x1e/0x1f |
   | 2 | `8` | movement/line action A |
   | 3 | `0x10` | movement/line action B |
   | 4 | `0x20` | movement/line action C |
   | 5, 6 | `3` | primary/cancel cluster |
   | 7 | — | `0063be50()` (timers/scoreboard view) |
   | 8 | `0x200` | team/roster action |
   | 9 | `0x100` | team/roster action |
   | 10 | — | (no mapping) |

   Which labelled popup item each index is remains **open**; for the port,
   commands are mouse picks on an on-screen list, not keys. The candidate
   writer functions from docs/sim.md delta 20 (`00416150`, `00432920`,
   `0058bc80`) are confirmed READERS/renderers of the same widget state —
   `00432920` is menu navigation returning 3/4, `0058bc80` is a dialog
   button row that pops the raw queue for Esc (`0x1b`).

## 4. Verified vs inferred

*Verified (decompiled/disassembled):* `0047f530` guards and ack gating;
`004b0bb0` reject conditions; `004af230` full rule + jump table; grid
structure/extents and the `004b8b20`/`004b6d70` build path; walk step
`004770a0`; arrival flags `0047f740` (600²/180²); hole snap `00476c00`
incl. stand-off sign; `004af1b0`/`004ac100`/`004aeee0`/`004af040`;
`00476980` accept/reject; `00479000` action consumer; `00474ab0` route
apply; router/A* existence and empty-route handling in `004703f0`;
`00495930`/`005952xx` widget semantics; `004876d0` command→bit table;
queue API and event codes.

*Inferred / open:* min-distance as an order gate (verified only as an ack
gate in `0047f530`); empty-route outcome on the player click path;
`004b6990` section-byte→flag binding (type.dat/depth.dat defaults); the
exact `+0x28` writer and which popup label maps to which command id;
Hole record `+0x10` ("busy") is unported in `game.hpp` (`HoleView.busy`
defaults false).

## 5. Integration notes

* `game.hpp` (done in this change): `Click` runs `walkgrid::Decide`
  (accept/reject/snap) — accepts `x,y >= 0` including out-of-bounds;
  `Walkable` keeps the `(w-1)*600` wall (world extents `004b8b20`);
  `WalkStepToTarget` steps first and flags arrival after (0047f740 shape);
  `Step` keeps the last legal step (`walking_ = false` on wall).
* When lake corner bytes get loaded (type.dat classification or the TYPEMAP
  binding once resolved), pass them as `walkgrid::GridView` and make
  `game::Walkable` delegate to `walkgrid::Walkable`; the mask table needs no
  change.
* `main.cpp` input handling: map SDL keys to the raw event codes of the §3
  table (queue semantics `0049e2c0` pop / `0049e320` re-queue), and route
  mouse clicks to `Game::Click`. Do not build a VK→command layer: commands
  (`00495930`) are UI-list item selections — implement them as menu list
  picks in `ui.hpp`/`menu.hpp` if the popup menus are ever ported.
* Mouse pick order (verified `0063ca80`): world pick `004b9f20` → hole
  lookup `004aeee0` → busy test `004af1b0` → snap `004ac100` → route check
  `004b0bb0(x, y, holeId)` → order. The port folds these into
  `walkgrid::Decide` + `Game::Click`.
