# Simulation analysis — Propilkki 2 gameplay core (clean-room)

Target: `extracted/ProPilkki2.exe` (x86, VS2022). All addresses are image VAs
(`0047f740` = `0x0047f740`). Decompilations produced with a headless Ghidra
project (`/tmp/opencode/ghidra-sim`), dumps in `/tmp/opencode/sim-dec/`.
Constants read straight from `.rdata` with `tools/peconst.py`.
This document is analysis only — no `src/` was modified.

Where a claim is inference rather than a verified decompilation it is
marked *(inferred)* or **open**.

---

## 0. Executive map of one frame

`00605eb0` (tick, already documented in decomp-notes) drives:

```
UPDATE (clocks: 7c695c game-time, 7c6a0c frame base):
  0062e230  sprite anim timers
  00471810  entity slicer  (this = *[007c6944])
  00467e80  anim/timeline pass under CS 7c69a8 (this = *[007c6950])
  00651620  timer/fade engine (twice: game-time and raw ms)
  00606bd0  mouse edges
  00658570  small state machine
RENDER: 6499e0, 6440b0, 640370 (incl. 7c6904 game-mode state machine), …
```

Two global objects matter for gameplay:

* `*[007c6944]` — the **entity slicer** (ctor `0046fb20`): `+0` last timestamp,
  `+4` → listA (`dlist`, count at `listA+0`), `+8` → listB, `+0xc` = enabled (1).
* `*[007c6950]` — the **timeline pass** (ctor `00463440`, size `0x34`): `+0`
  gate (skip pass when ≠ 0), `+4` last timestamp, `+8` → list.

Both lists are plain intrusive doubly-linked lists (`dlist`, size `0x18`):

| offset | field |
|---|---|
| `+0` | count |
| `+4` | current node (node `+4` = object, `+8` = next, `+0xc` = prev) |
| `+8` | current index |
| `+0xc` / `+0x10` | head / tail sentinel nodes |
| `+0x14` | locked flag |

List API (all `thiscall` on the list): `005895e0` ctor, `0058aaf0(list, idx)`
seek+return object, `0058ab20(list)` return current object,
`0058ab40(list)` advance, `0058b510(list, idx)` seek,
`0058b640(list)` reset to head, `0058aea0`/`0058ae40` push (front/tail),
`0058af40` push at random position, `0058b080(list, idx)` remove,
`0058a9d0(list, obj)` index-of, `0058b450(list, idx, val)` **replace object
(deregistration writes NULL; slicer skips NULL slots)**, `00589730` destroy.

---

## 1. Function / struct table

| addr | role | key fields / notes |
|---|---|---|
| `00605eb0` | frame tick (WinMain) | order above; passes wall clock as slice arg |
| `00471810` | **entity slicer**: `delta = now - last`; first frame delta = 0; per listA element `FUN_0047f740(obj, slice)`; per listB element `obj->vft[+8](slice)`. Slice split: single call if delta < 1000 ms, else loop `slice = (remaining > 60000) ? 5000 : 1000` | this `*[007c6944]`; listA ptr at `this+4`, listB ptr at `this+8`, enabled at `+0xc` |
| `0046fb20` | slicer ctor: two `dlist`s allocated, `+0xc = 1` | |
| `00467e80` | **timeline pass**: CS `7c69a8`; `elapsed = 0059ae70(last, now)`; if gate `*this == 0` iterate list (`count = *(*this+8)`): `00467210(currentObj, elapsed)`; writes back last time | this `*[007c6950]`; gate `+0`, time `+4`, list `+8` |
| `00467210` | **timeline player / move sequencer + keyframe evaluator**. Advances `this+0x1a4` (elapsed-in-move) by delta; move data at `this+0x1a8`: `+0x118[move*4]` length ms, `+0x52aec[move*4]` speed scale (f32), `+0x53a44+move` per-move byte flag, `+0x29158+move*30` move name (15 chars), next-move table `*(u32*)(script + move*4)`, keyframes at `+0x228+…` (strides `(track + move*40)*15*8`). On move end: pending move `this+0x1b0` if ≥ 0, else default next; hops ≤ 5 per call. Interpolates keyframes into targets held in `this+0x58..` (writes `*target+0x1b0` triples) and results in `this+0x1b4..+0x1c8`, applied by `00467c30` / `00467f60` | `+0x1a0` track count, `+0x1a4` time, `+0x1a8` script, `+0x1ac` current move, `+0x1b0` pending move |
| `00464470(this, outbuf)` | return current move (= **game mode**); with nonzero arg copies move name (15 chars) to buffer | reads `this+0x1ac` |
| `00464600` | return pending mode (`+0x1b0`) | |
| `004644d0` | elapsed in current move, clamped to `duration-1` | duration = `(int)(script+0x118+m*4) * (float)(script+0x52aec+m*4)` |
| `00464510(mode)` | duration of mode, ms (same formula) | |
| `00465d50(mode)` | queue next mode (`+0x1b0`, bounds-checked `< moveCount`) | |
| `00464410(mode)` | queue next mode **and** `+0x1ac = -1` (restart current) | |
| `0047f740(this, delta)` | **per-session (per human) game update**, called once per slice from slicer listA. Body: ambient timer, screen driver `006304e0(this+0x18)`, mode queries via `this+0x5d4` (timeline sub-object), camera/mode branches, calls `004770a0(delta)` (walk), `00480820()` (visual/mode bookkeeping), HUD flags, bobbing offsets | session object size **`0x2340`** (pool free in `00436420`); bite ring `+0x738 .. +0x2338` (64 × `0x70`), bite ring index `+0x734`, counter `+0x730`, last time `+0x72c` |
| `00471650(listOwner, session)` | **register session**: `push_back`, stores index into `session+0x6a8` | called from `00445970` (join player) |
| `00471760(session)` | deregister: index-of → `0058b450(idx, NULL)` (lazy delete; slicer null-checks) | |
| `004717c0(slicer)` | clear listA and listB | |
| `00436420` | session teardown: per owning entity frees the `0x2340` via `owner+0xa8`, vtable `+0xc/+4` destroy hooks, frees `0x68`-byte objects, destroys six `dlist`s at `this+0x44..0x78` | |
| `00445970` | **add player** (skill at config `+0xb4`, 0 → `rand(22)+1`, clamp rules), allocates `0x2340` via `0046fd20`, registers via `00471650`, sets lure grid (`0058e5d0/0058f760`, 4 bait types × 16 lures) | |
| `004770a0(this, delta)` | **walk step**: `dir = sincos((double)this+0x520 * π/180)` via `0066e410` (returns `(sin, cos)` pair in XMM0 — verified: small-angle branches compute `x` and `1-x²/2`); `vel(+0x508,+0x510) = dir`; step `s = delta * 0.34` (`006d9f80`); `pos.x += dir.x*s`, `pos.y += dir.y*s`, clamp each ≥ 0; `this+0x4c8/+0x4cc = (int)pos`; `this+0x4d0 = 004b15d0(x,y)` (ground height); sprite place `004ba200(sprite this+0x530, x, y, height, (int)heading, 0, 180)` | inline copy in `0047f740` states `0x1e/0x1f` uses `0.725` (`006d9fa0`) instead of `0.34` |
| `00476c00(this, x, y, snap)` | **walk/click order**: guards by mode; sets target `+0x4a0/+0x4a4`; if a hole (`004aeee0`) exists at target and `snap` and `004af1b0(hole)==0`: `+0x4bc = 1`, snap `+0x4b4/+0x4b8` to hole, register callback `004aed70`, and target = hole + `unit*(−90)` (stand 90 units behind hole, `006da098 = -90`). In fishing modes `0x18/24/25/26/27/28` it instead stores `+0x4d8/+0x4dc` (reel target) and may fire `00477880` + “Nosto” timer (`00650fe0("Nosto",0,1000,…,8)`) | toggles mode pairs `0↔1`, `0x1e↔0x1f`; mode 5/−1/`00476290` → `00465d50(2)` |
| `00474ab0(this, routeList, …, bearing)` | set heading `+0x520 = bearing`, velocity dir, next waypoint `+0x4ac/+0x4b0`, computed bearing `+0x45c = (deg + 450) % 360`; starts `00476c00`. Guarded: only when current & pending modes are outside `6..0x17` and `0x2e..0x33` and outside `0x20..0x22` | |
| `004764b0(this)` | **drill start**: requires mode == 5, `+0x528` lake present, `+0x4bc==0`, and `004af040(drillPoint, 300) == -1` (no hole within 300 units of `pos + vel*75`, `006da090=-75` means `pos + vel*75`). Else returns 4 (“hole already here”). Sets `+0x724/+0x728 = pos`, `+0x720 = now`, `+0x4e8=0`, `+0x4e4=2`, `+0x450=0`, `+0x470=now`, `00465d50(6)`; sends net msg `0xd` with hole id `+0x4c0`, pos, heading, `(duration-200)/1000` | |
| `00477310(this)` | **bore/drill begin**: msg `0x17`, `+0x438 = 1`, mode → `0x18` (stays if `0x25/0x26/0x18`) | |
| `00477220(this)` | **bore end**: if `+0x438 == 2` (guarded by mode range) msg `0x16`, mode → `0x27` | |
| `00477880(this)` | line/lure stage: mode → `0x25`, msg `0x15` | |
| `00480820()` | per-frame mode visuals: hole create at mode `0x13` (`004aeee0` → `+0x4c0` hole id, `004ac150` → `+0x4c4`, `004bbd90` open; log `HOLE CREATION FAILED!`), sprite/rot `+0x458/+0x460..+0x468` per mode (constants `0.03`, `230`, `270`, `4000`, `2000`, `28`, `10`, `3`, `4`, `4000*0.002`), countdown chain (below), release-anim at `0x33` | |
| `0048af00` (2088 lines) | **player controller**: `uVar5 = 004876d0()` action bitmask, switch on `this+0x3b8` (controller state 1,2,4,5,6,7,8,9,10,0xb…0x16,0x19); handles walk (`gen0..gen3` timers `00650fe0` with names `006dab0c/14/1c/34`, easing modes 8/9/0xb/0xc/0x1d), drilling sub-state machine (drill depth counter `+0x414`, target `+0x418`, per-step depth array at `+0x5b8`, globals `7bcc90` remaining ms, `7bcca0` power, `7bcc9c` stop depth, `7bcc94/95` outcome flags, `rand` rolls vs `+0x2f8/+0x308/+0x310` probabilities ×10000), HUD events `0049b9c0` | this = the `0x2340` session; controller state at `+0x3b8`, prev at `+0x3bc`, substate `+0x3c0` |
| `004876d0` | **input → action bitmask** (see §3) | |
| `0063ca80` | state-20 (in-game) event pump (see §3) | |
| `00479700(this, biteRec?, …)` | **bite attempt**: needs `+0x52c`, `+0x528`, `+0x640`; validates `+0x348` hole via `004adad0/004c1300`; `roll = rand(1000)`; attempt A (`arg==0`, ambient/catch-again): success iff `roll ≥ 500`; attempt B (action): thresholds by activity value (obfuscated `+0xc`, XOR `0x0E5FAF5F` via `00590c10/00590e70`): `<500 → 125`, `500..999 → 250`, `1000..1699 → 500`, `>1699 → always`. On success: filter candidates from list `this+0x338` through `00475c70(...)`, `pick = rand(count)`, `0058aaf0(pick)`, `candidate+0x6c -= 1`, release old `+0x348`, `+0x348 = 004c1390(candidate+0xac, avatarX, avatarY, …)` (fish attached to hole), kill “Ambient” timer `+0x638`, return 1 | |
| “Ambient” timer | created in `0047f740` when `+0x640 != 0` and `+0x638 == -1`: `00650fe0("Ambient", 0, rand(75000)+20000, 1000, &+0x638, 8)` — repeats every 1000 ms, duration random **20 000…94 999 ms**; on fire calls `00479700(this,0,0,0,-1)` (50 % bite attempt) | name string `006d9e10` |
| `004ba5e0(context, out)` | **server-side fish/bite roll** (see §4.4) | |
| `004538a0` | GameServer `IS_BITING_REQ` handler (see §4.4): rate limit, depth buckets 10/20/30/40 → `0..3`, bait/lure validation `0058fcd0`/`0058fbf0`, builds bite, reply msg `0x22` with outcome `0..4` | `Bite too often! %s (count %d)` when `avg gap ≤ 900 ms` and `last gap > 1000 ms` |
| `00453620` | GameServer `IS_BITING_CATCH_MSG` (catch report): validates weight `biteW/s16(+0x60) − s16(+0x5e) ≥ s16(+0x5c)` else `Client reported catch, but it is not a catch!`; resolves lure (`Lure not found!`), consumes bait `0058f7c0`, success → `00473280`, escaped → `004732d0` | |
| `00473320(this, bite, …)` | **catch processing (landed fish)**: `+0x720 = now`, `00479700(bite…)` restart, weight vs threshold `470` (type 3: `670`), hole counter `hole+0x18 += 1` for types `0xf,0x11,0x12,0x14,0x15,0x1d`, catch camera block when `+0x544` (`0049dc50/004634c0` anim, `00465d50(0)`), score/voice `00438a00(+0x150)` → `00438a00` returns `*(double*)(client+0x150)`; swim-away release effect `004a0780(x,y,…)` at `heading ± rand(70)°`, distance `rand(70)+150` units, net msg `0x1f` | |
| `00478210` | **AI_STATE_FISH_TAKE_UP** action handler (see §5): probability rolls `rand(10000) < p*10000` with `p` from bite record `+0x6e8/+0x6f0`; success/fail/normal paths `00473280 / 004732d0 / 00473320`; stats `+0x354` count / `+0x358` total / `+0x35c` max, 5-catch review (`max < 50 or avg < 30` → give up), timer `AI_korjaus = (14 − skill)*1000 ms`, skill `+0x6a0` (0..10) | |
| `00477b90` / `00477e20` / `00478b10` | AI_STATE_DRILLING / FISHING / WALKING handlers (see §5) | |
| `004af230(this, x, y)` | walkability test: cell `(x/600, y/600)`, grid `lake+0x15168 → {+0x500 data, +0x504 w, +0x508 h}`, corner bytes, bit `0x10` per corner, jump table on the 4-bit corner mask | world cell = **600 units** |
| `004af040(this, x, y, r)` | nearest open hole within radius `r` (cell prefilter ±300 bounds at `lake+0x240c8`, hole lists `lake+0x240c0`), requires hole record `piVar2[3] == 0`; returns `(cellIdx<<16)|idx` or −1 | used with `r=300` (drill check) |
| `004aeee0(this, x, y)` | nearest open hole at/nearest position: same walk as `004af040` but initial squared-distance bound `DAT_006dc6c8 = 90000.0` (no radius argument) | hole id used by `00476c00` snap |
| `004a5700(this, x, y, flag)` | **depth at position** (int x,y in, int out): `004af230` gate first (0 when not walkable); cell = `x/600` (trunc); value = `lake+0x1efd8` **16-entry i32 table** indexed by the `lake+0x15168` grid byte's low nibble (index 0 when bit `0x10` set; each sample clamped by `004a56a0`) — not a per-cell grid; flag 0 or exact cell corner → raw table value (`004beb70`); flag != 0 → two-triangle linear blend split at `tx == ty`, `tx/ty = (x/y % 600) * (006d9f10 = 1/600)`: `tx<ty` `(V3-V2)*(1-ty) + ((V2-V1)*(1-tx) + V1)` with V1..V3 = (x1,y1),(x0,y1),(x0,y0), else `(V3-V2)*ty + ((V2-V1)*tx + V1)` with V1..V3 = (x0,y0),(x1,y0),(x1,y1); result truncated to int (`cvttsd2si`) — NOT bilinear | |
| `004b15d0(this, x, y)` | **ground height at position**: same 600-cell grid, gate byte bit `0x20`, bilinear through `004aedc0` | written to session `+0x4d0` each walk step |
| `004b9f20` | screen→world last pick: returns `lake+0x15134/+0x15138` (x,y) + `+0x1513c` valid flag | |
| `00676d2c` | **RNG**: MSVC `rand()` on TLS state `s`: `s = s*0x343FD + 0x269EC3`, return `(s>>16)&0x7FFF` | |
| `0059db20(max)` | **`rand(max)`**: two `rand()` → 30-bit `u`, return `(int)(u * 2^-30 * max)`, clamp to `max-1`; 0 → 0 | `0073fd80 = 9.313225746e-10` |
| `00590c10 / 00590e70` | get/set obfuscated i32 field `+0xc` (XOR `0x0E5FAF5F`) — used as fish weight/tamper value | |
| `00650fe0 / 00651220 / 00651410` | timer create(name, from, to, durationMs, outSlot, mode) / fired? / kill — the game’s timer engine (modes 8=repeat, 9/0xb/0x1d/… = easing) | |
| `00630740 / 00630790` | action-queue peek / push (0x14-byte actions) on state objects | |
| `005cfca0` | MENU_PLAY screen event handler (case 3 = pause: loads `gfx\pic/menu/pause.pcx` (+1280x720/1920x1080 variants), `7c4448 = 0x3f`) | vtable `00741750` |
| `005cf200` | MENU_PAUSE screen frame (409 lines, consumes events itself) | vtable `00741a6c` |
| `004c2ce0` | giant results/report builder — refs `No bites.`, `Kalojen yhteispaino`, `Initial drill`, `Bite indicator flag is up!`; too big for Ghidra decompiler (flow limit) | **open** (walk manually) |

---

## 2. Per-frame update order (verified)

1. `0062e230` sprite anim timers (39 ms gate, ping-pong, per decomp-notes).
2. `00471810(delta)`:
   - listA (sessions, count = `*listA` at `session-slicer+4`): each non-NULL
     element → `0047f740(session, slice)` with slice ≤ 1000 ms (5000 ms when
     remaining > 60 000 ms).
   - listB: each element → `vft[+8](slice)`. Verified slot-2 implementations:
     `State` base `006307c0` (drain one action from queue `obj+0x50`, log
     `Event Consumed:`), `AI_STATE_DRILLING 00477b90`, `AI_STATE_FISHING
     00477e20`, `AI_STATE_FISH_TAKE_UP 00478210`, `AI_STATE_WALKING 00478b10`,
     `human` overrides slot 2 with `00433f00` (return 0, no-op).
3. `00467e80(delta)` under CS `7c69a8`: for each timeline object →
   `00467210(obj, elapsed)` — advances the move/mode clock, performs mode
   transitions, evaluates keyframes.
4. `00651620` timer/fade (game time), `00651620` (raw ms), `00606bd0` mouse,
   `00658570`.

`0047f740` internal order (per slice): ambient timer gate (`+0x640/+0x638`) →
`iVar7 = mode` → `006304e0(this+0x18)` screen driver → mode-change hooks
(`+0x5f8` prev mode) → per-mode blocks (arrival tests `360000` (600²) and
`32400` (180²), walk via `004770a0`, camera wrap `±180`, HUD
`00479db0(elapsed*100/270)` …) → `00480820()` visuals → HUD flags
(`+0x5fc = (mode==0x18)`, `+0x600 = (mode==5)`, `+0x604 = (mode==0x35)`).

---

## 3. In-game input (state 20)

Two layers feed gameplay:

### 3.1 Event queue (keyboard/mouse → events)

WndProc `005f3820`:
* keys → `0049e1e0(vk, isChar)` — remaps OEM VKs (`0xF6→0x94, 0xE4→0x84,
  0xD6→0x99, 0xC4→0x8E, 0xE5→0x86, 0xC5→0x8F`) and pushes **raw VK code**
  into queue `7bcd60` (max 1000, count `7bdd00`).
* mouse buttons/wheel → `0049e270(n)` pushes `n + 1000` (so `0x409`/`0x40a`
  = 1000+9/1000+10 = zoom in/out; `0x45c` = 1000+0x74 F5, `0x45f/0x460` =
  F6/F7).
* key bitmap `DAT_007bcd5c` set/cleared by `0049e3c0(vk, down)` (bit =
  `vk & 0x1f`) — write-only in this build; gameplay consumes the event queue.

Queue API: `0049e310` non-empty, `0049e2c0` pop-front, `0049e320` push-front
(re-queue), `0049e2b0` clear.

### 3.2 `0063ca80` (state 20 handler, one event per frame)

| event | action |
|---|---|
| `0x21 / 0x22 / 0x23` (PgUp / PgDn / End) | `0047ae90(slot 0/1/2, 0, 0)` — rod/lure slot release; may `00465d50(1)` / `00464410(3)` |
| `0x31 / 0x32 / 0x33` (`1`/`2`/`3`) | `0047ae90(slot 0/1/2, 1, 0)` — rod/lure slot press, `00465d50(1)` on change |
| `0x409 / 0x40a` (mouse wheel ±) | camera zoom `7a6b7c ± 100`, clamped `[-0x1194 or -3000 … -FUN_0063c980()]` (floor −20000 in some modes) |
| `0x45c` | chat path `0045a540/0045c330` (notes) |
| `0x45f / 0x460` | scoreboard / player cycle UI |
| default | re-queue via `0049e320` (other screens consume) |
| mouse L press (latch `7c6915`, speed gate `7a6b70/7a6b74`, slow-drag `≤ 15.0` (`006d1670`) double-click counter) | pick `004b9f20` → world (x,y) → object pick `004aeee0/004af1b0`; sets markers `004a5550`(type 0xe) / `004a5620`(type 0xf) at lake `+0x13464`, and `0047f530(x, y)` walk order (min click distance **1500** units, `00799154`; route check `004b0bb0`) or hole action; `7c8794` latch = fishing-target mode |
| Esc (`0x1b`) / back | `7c8790 = 0x14`, game state → 4 (pause); `MENU_PAUSE 005cf200` renders `pause.pcx` (per decomp-notes + `005cfca0` case 3) |

Movement feed-through: `0047f530` → route → `00476c00(x,y,snap)` /
`00474ab0(route,…,bearing)` → heading `+0x520` → walk applied every slice by
`004770a0`. While a walk target is beyond 600 units the session sets
`+0x690=0`; inside 180 units it spawns guidance arrows; arrival within 600
units sets `+0x698 = 1`.

### 3.3 `004876d0` action bitmask (consumed by controller `0048af00`)

Return bits (verified set): `1,2` (primary/cancel group; ESC (`0x20`) and
commands 5/6 → `3`), `4` (release-hold path), `8` (cmd 2), `0x10` (cmd 3),
`0x20` (cmd 4), `0x40`/`0x80` (HUD rect buttons in mode `0x1e`, mouse
rectangles sized by resolution: `0xf7/0x1e0/0x2d0 × 0x6e/0x8f/0xd6`),
`0x100` (cmd 9), `0x200` (cmd 8), `0x400`/`0x800` (`forceRise` timer
`00650fe0("forceRise",0,100,3000,0,8)` — 3 s), `0x1000` (**auto-bite roll**:
every ≥1000 ms when `session+0x428 > 0` or wiimote drift, `00590a40(bait,…)`
→ percent `p`, `rand(100) < p` → action), `0x2000`, `0x4000` (external serial
controller). Command ids come from `00495930()` = `menuObj+0x28`.
*(inferred)* commands 2/3/4 are the movement/line keys — exact VK→command map
lives in the menu/input layer (`00416150`, `00432920`, `0058bc80`) — **open**.

Controller state machine `0048af00` (`session+0x3b8`, substate `+0x3c0`)
notable edges: bit2 in state1 → drill prep (`gen0..gen4` timers, state 7);
bit `8` → state `0x14` (or `0x15` with HUD `0049b9c0(0,7000)` when lure busy);
bit `0x10` → state `0x16`; bit1 in state6 → cancel (state 10); state `0xd` =
**drilling** (progress array, `rand` events, `7bcc90` countdown, breakthrough
`7bcc9c`); states 4/5 = walk (`gen0/gen1` ease timers `+0x4f0→+0x4a8`,
`+0x4f8→+0x4b0`, `+0x4fc→+0x4b4`, duration `= (0x17−players)*25` ms if
`players < 8` else 400 — (inferred) player-count-scaled pause).

---

## 4. Core gameplay rules and constants

### 4.1 Movement

* World is integer coordinates on a **600-unit cell grid** (DEPTHMAP/walk
  grid). Depth and height bilinear-interpolated within a cell
  (`004a5700`, `004b15d0`, `006d9f10 = 1/600`).
* Heading `session+0x520` is a **double in degrees** (wrapped to
  `(−180,180]` when set from `0067d0ca`, wrapped `(deg+450)%360` in
  `00474ab0`).
* Every slice: `Δ = dir(heading°) * K * dt_ms`, `K = 0.34` normally
  (`006d9f80`) and `K = 0.725` (`006d9fa0`) in the at-hole states
  `0x1e/0x1f`; position clamped `≥ 0` per axis. Integer position written to
  `+0x4c8/+0x4cc`, sprite placed with heading as int and ground height
  `+0x4d0`.
* Drill/stand point = `avatar + vel * 75 ms` (`006da090 = -75`,
  expression `pos − vel*(−75)`); behind-hole stand point uses `−90`
  (`006da098`); approach marker `−20` (`006da088`).
* Click-to-move: target must be ≥ 1500 units away (`00799154`) unless
  already arrived (`+0x698`); hole snap radius 300 (`004af040`), arrival
  radius 600 (`006da068 = 360000` sq), guidance radius 180
  (`006da060 = 32400` sq).
* Walkability: 4-corner bit `0x10` mask per cell (`004af230`).

### 4.2 Hole drilling

* Start (`004764b0`): game mode must be **5**; no hole within 300 units of
  the drill point; net msg `0xd`; session records drill start
  `+0x720/+0x724/+0x728`; mode → 6.
* `00477310` (bore): mode → **0x18**, `+0x438 = 1`, msg `0x17`.
* `00477220` (bore stop): mode → **0x27**, msg `0x16`.
* `00477880` (line stage): mode → **0x25**, msg `0x15`.
* Hole object creation at controller transition to mode `0x13` inside
  `00480820`: `004aeee0(x,y)` lookup/create, `+0x4c0` = hole id, `+0x4c4 =
  004ac150(hole)`, `004bbd90(hole)` opens it; failure logs
  `HOLE CREATION FAILED!` plus three position dumps.
* Hole records carry `+0x18` = **number of fish landed through it**
  (incremented in `00473320` for catch types
  `0xf,0x11,0x12,0x14,0x15,0x1d`).
* Drilling progress lives in controller state `0xd` of `0048af00`: power
  counter `+0x414` (min 0x46), target `+0x418 = +0x414+20` via
  `004ac620`, step array `session+0x5b8` (length `7bcc98`,
  `7bcca0 = power`, ramps `power*0.04` (`006db0c8`), 25/−25 ms steps
  `006db138/006db200`, 70 (`006db150`)), remaining `7bcc90`, breakthrough
  depth `7bcc9c`, outcome flags `7bcc94/95` from `rand(10000)` vs
  `session+0x310*10000` and `+0x308*10000`; per-step 200 ms timers with
  jitter `rand(13)+... *200`.
* No “cooldown” constant per se — the guard is *mode == 5* plus the 300-unit
  hole-exclusion test.

### 4.3 Line / jig / strike

* Line length is a **discrete bucket ∈ {10, 20, 30, 40} metres**
  (`IS_BITING_REQ` field `+0x21` maps `10→0, 20→1, 30→2, 40→3`; `004ba5e0`
  `switch(context+0x50)` errors with `Unsupported line:` otherwise). Slot
  keys: PgUp/PgDn/End + `1`/`2`/`3` (§3.2), rod/lure slot storage =
  3 × 7 dwords at session `+0x564`.
* Lure/bait: **4 bait types × 16 lures** active grid (`0058e5d0` set,
  `0058f760` clear, `0058f7c0` consume on catch — “bait usage” message);
  active lure read through `0058ea90`, presence checks in
  `00488060/004872e0` gate line-out state (mode `0x1e` if `lure+0x234 != 0`
  else mode 1).
* There is **no 1.5 s bite window and no “jig key catches instantly”** in the
  original. Strike = an action (controller → `00473320` / AI
  `AI_STATE_FISH_TAKE_UP` action 4) resolved against the bite record’s
  stored probabilities (§4.4).
* Ambient auto-strike chance: `004876d0` rolls `rand(100) < p` at most once
  per 1000 ms (`p` from `00590a40`), gated by `session+0x428`.

### 4.4 Bite generation (`004ba5e0`, server side, runs locally in SP)

1. Position validity `004af230(x,y)` — invalid ⇒ out`[0] = 0`, no bite.
2. **Candidate search**: seed `004a9490(x,y)`; 9-step deterministic walk over
   depth cells. Per step `i` (0..8), with `w = lake+0x1517c` (grid width):
   `cx = dx[i] + seed % w`, `cy = dy[i] + seed / w` where the `(dx,dy)`
   pair table (stack copy from `006dd290, 006dc790, 006dc7a0, 006dc7f0`
   plus literal `1,1`) is:
   `(-1,-1) (0,-1) (1,-1) (1,0) (1,-1) (1,0) (-1,1) (0,1) (1,1)`, and the
   seed for the next step becomes `cy` (i.e. `seed = cy`, `cx` re-derived).
   In-bounds cells (`0 ≤ cx < w`, `0 ≤ cy < h`, h = `+0x15180`) contribute
   their fish list (`lake+0x240c4` grid → list `+0xc`): for each entry,
   `rand(count)` picks are copied to a selection list (`0058acc0`);
   first selection passing `005f8ff0(context, out)` wins.
3. **No-candidate fallback**: rare roll (`0049de50(2, lake+0x150fc, …) > 0.01`)
   plus depth gates (depth at pos via `004a5700(x,y,1)`,
   thresholds `0x82=130`, `0xfb=251`, min `0x51=81`) can still synthesize a
   type-2 fish.
4. **Weight**: `W = min(v * k_line, cap_line)` where `v = 00590c10()`
   (obfuscated `context+0xc`, set from FishEntry weight
   `field+0x14 − s16(field+0x24)`, grams — `Invalid weight for fish!` if
   negative):

   | line | k (`006dc5xx`) | cap |
   |---|---|---|
   | 10 m | `1/60` (`006d09b8`) | **300** (`006da020`) |
   | 20 m | `1/115` (`006dc5e8`) | 30.0 |
   | 30 m | `1/160` (`006dc5d8`) | 30.0 |
   | 40 m | `1/230` (`006dc5d0`) | 30.0 |

   Second timing value at record `+32`: `v ≥ 1000 → 0.16666`,
   `300 < v < 1000 → 0.05 + (v−300)*0.0001666571` (`006dc650/006dc5b0`),
   else `0.05`.
5. **Species roll (type-2 fallback fish)**: pick a band table by `v`:

   | band | table | values (species idx) |
   |---|---|---|
   | `v < 30` | `006dc820` | `1 1 1 2 9 1 1 2` |
   | `v < 200` | `006dc840` | `1 1 2 2 0 1 1 3` |
   | else | `006dc8b0` | `6 7 7 7 2 2 3 8` |

   type 3 → `006dc8a0 (6 6 6 6 6 7 7 7)`, type 5 → same, type `0xf` →
   `006dc870 (3 4 5 5 3 9 5 5)`, type `0x16` → `006dc880 (3 9 5 5 1 4 5 6)`.
   Species id resolved at `lake+0x28a94 + idx*4`.
6. **Outcome byte record+0x15** (only when `005f77d0(type, v) != 100`):
   * success ⇒ `1`;
   * else threshold by line: 10 m → `0x5f` (95), 20 m → `0x3c` (60),
     30 m → `0x1e` (30), 40 m → `0` (type 3: `10`); `out = (rand ≥ thr) + 2`
     (so 2 or 3);
   * else `0`.
   `005f77d0` itself interpolates `100 → 0` between two per-type int
   thresholds (`00746278/0074627c` and `00746280/00746284`) — a depth/type
   difficulty ramp.
7. **Depth-band catch/fail probabilities** stored in the bite record
   (`out+0x30/+0x38` region as doubles), selected in `004538a0` by `v`:

   | v (depth) | catch p | fail p |
   |---|---|---|
   | `< 150` (`006dd0b0`) | 0.7 | 0.1 |
   | `150..300` (`006dd0c0`) | 0.9 | 0.4 |
   | `≥ 300` (`006dd0d0`) | 0.95 | 0.5 |

8. Server rate limit: rolling window (`00592160/005922e0`) of request gaps;
   avg must be ≥ **900 ms** (`006d6b80`), else count++ and
   `Bite too often!` when the latest gap > 1000 ms.
9. Bite record allocation: circular **64 × 0x70** ring at `session+0x738`
   (`00471940`), lookup by seq at `+0x58` (`00473a70`).

### 4.5 Catch / scoring

* Server catch validation (`00453620`): claimed weight
  `w = bite+0x10 / s16(bite+0x60) − s16(bite+0x5e)` (+1 if flag byte) must
  be `≥ s16(bite+0x5c)` or the report is rejected.
* Landing (`00473320`): minimum weight to trigger release effect
  **470** (type 3: **670**); hole counter `+0x18++` for the type set above;
  release effect at `heading ± rand(70)°`, distance `rand(70) + 150` units,
  spawn `004a0780(x, y, rand(360), 0, 180, 9, 1, 0)`; net msg `0x1f`.
* Score value read back with `00438a00(client) = *(double*)(client+0x150)`.
* Per-player tally (AI path, mirrored for player in shared code): count
  `session+0x354`, total `+0x358`, biggest `+0x35c`; after **5** catches a
  review runs: if `biggest < 50` or `total/5 < 30` the AI gives up
  (HUD events `0xf/0x10`) else resets counters.
* Round reset: `00436a30(1, 0)` zeroes `entry+0xb4, +0xb8, +0x94, +0x98,
  +4, +8, +0xc` on every entry of `this+0x6c` (bag/player list); full mode
  (`param_3`) also rebuilds teams.
* Player bag = list `session+0x6c`, entries sized ~`0xd8+`, id at entry
  `+0xa8` (`00437f60` lookup by id); host/client bag compare strings at
  `004c58bc..` (`Player %s (%s) reported fish bag mismatch:`).
* Results screen: giant builder `004c2ce0` (string xrefs verified; body
  exceeds Ghidra flow limit — **open**).

### 4.6 Round timer, mode timeline, round end, pause

* The round is a **move timeline** on the object `session+0x5d4` (a
  `00467210`-style sequencer): each mode’s duration =
  `(int)script[0x118 + m*4] * float(script[0x52aec + m*4])` ms; next mode
  from `*(u32*)(script + m*4)` unless overridden by pending `+0x1b0`
  (`00465d50/00464410`). Time advances only in the `00467e80` pass.
* Verified mode transitions:

  ```
  9 → 10 → 0xc → 0xe → 0x10 → {0x12 | 0x31 | 0x33}   (countdown chain,
      each hop when elapsed ≥ session+0x46c; duration local_8 = 00464510(m),
      HUD scale dVar11 = 30/local_8, extra ×3 (006d9fc0) / ×4 (006d1640)
      multipliers for modes 0xf/0x11)
  5 → 6          action / drill start (004764b0, 00476c00 via 00465d50(2))
  0x25 → 0x18    bore begin (00477310)
  0x18 → 0x27    bore end (00477220)
  ? → 0x25       line stage (00477880)
  0x13           hole creation pass (00480820)
  0x14           teardown of handles (+0x344), sets session+0x640 = 1
  0x1e ↔ 0x1f    at-hole pose toggle (00476c00)
  0 ↔ 1          free-walk pose toggle
  ```
* “Game over → Results”: `00476180()` is true in modes
  `{0x18, 0x24, 0x27, 0x28}`; the state-20 handler ends the round when the
  end timer `7c896c` (created `00650fe0(&0074c150, 0, (2000 or 0),
  0x8fc, &7c895c, 0xb)` — 2000 ms hold) expires, `00483890(2,0)` runs and
  `00632880()` approves → `DAT_007c6904 = 0x15` (**Results state 21**).
  Alternate path in command case 1/0: modes `{1,2,0x1e,0x1f}` → state `0x14`
  directly.
* Pause: Esc leaves state 20 with `7c8790 = 0x14`, state → 4 (session hub);
  `MENU_PAUSE` enters via action 3 (`005cfca0` case 3 loads `pause.pcx`,
  `7c4448 = 0x3f`), exit resumes via `7c8790` (decomp-notes). `004876d0`
  also maps ESC (event `0x20`) and commands 5/6 to action `3`.
* HUD countdown in modes 3/4: `progress% = min(elapsed_in_move, 270−1) *
  100 / 270` (`0x10e`, `00479db0`).

### 4.7 RNG (exact)

```
00676d2c():  s = s * 214013 + 2531011      // per-thread TLS +0x18 (MSVC rand)
             return (s >> 16) & 0x7FFF
0059db20(n): u = ((r2 & 0x7fff) << 15) | (r1 & 0x7fff)   // 30-bit
             return (int)(u * 9.313225746154785e-10 * n)  // clamp n-1; n=0 → 0
```

---

## 5. Opponent / AI in single player

RTTI classes: `human`, `HUM_STATE_IDLE/WALKING_FINDING/WALKING_ROUTE`,
`AI_STATE_{IDLE, AT_HOME, AT_HOME_TEAM, DRILLING, EMPTY_TAKE_UP,
FINDING_TARGET, FISHING, FISH_TAKE_UP, GOING_HOME, GOING_TO_CAPTAIN,
TARGET_LOST, WALKING, PERA_SPECIAL_*, SITTING_PUUHASTELU,
STANDING_PUUHASTELU}`.

Verified:

* AI lives on a `human` object: skill `human+0x6a0` (0..10), current AI
  state object + action queue, stats `+0x354/358/35c`.
* AI and player share the **same action system**: AI states pull actions
  with `00630740()` (0x14-byte records, `+0` = action id 0..0x10) and push
  with `00630790(id)`; the player’s input path produces equivalent actions
  (`004876d0` bitmask → `0048af00`). The slicer’s listB `vft[+8]` per-slice
  call is what pumps these handlers.
* AI uses the player’s own functions: `AI_STATE_WALKING` action 4 calls the
  very same `004764b0()` drill start (with a “too many players close”
  distance guard `00472cf0(pos) > players−0x19` → action 10), then action 4
  → `00630790(4)`; `AI_STATE_FISH_TAKE_UP` action 4 resolves the catch
  exactly like §4.4/§4.5.
* AI timers are named and readable: `AI_ei_syo_short` =
  `((rand % (11−skill)) − skill + 20) * 1000` ms; `AI_no_fish_short` same
  formula; `AI_no_fish_long` = `rand(5000)+35000` ms (no lake/model) or
  `rand(10000)+60000` ms (lake); `AI_syoko_timer = 1000` ms;
  `AI_bore_timer2 = (rand%100 + 20) * 100` ms; `AI_korjaus =
  (14 − skill) * 1000` ms; `AI_Temp = 500` ms; `AI sinking = 3000` ms.
* Bot chat roster comes from `botinfo.dat` (already ported).

What the 00471810 slices drive per frame, concretely: (a) every session’s
`0047f740` (movement, camera, ambient bite attempts, HUD/mode reactions),
(b) every queued state object’s action drain (`vft+8`), and (c) — through
`00467e80` — every timeline’s move clock + keyframe evaluation (round
countdown, intro/outro sequences, model animation).

---

## 6. Deltas vs current `src/game.hpp` (fix list for port agent)

1. **Frame model.** Original sim runs on wall-clock slices ≤ 1000 ms
   (5000 ms catch-up when > 60 s behind) from `00471810`; `game.hpp` steps
   once per render frame with raw `dt`. Feed `game::Update` pre-sliced
   deltas ≤ 1000 ms (reuse `tick::Slices`).
2. **Movement is click-to-move, not WASD.** Replace direct key velocity
   (24 u/s, diagonal 0.7071 normalize, clamp ±128) with: target point from
   click (`≥ 1500` units, hole snap at 300, arrival 600, guidance 180),
   heading in degrees, `Δ = dir(heading°) · K · dt`, `K = 0.34` (`0.725`
   at-hole), position `≥ 0` clamp, integer pos = `(int)` of double pos.
3. **World grid.** Add 600-unit cell grid with corner flag `0x10`
   (walkability) and bilinear depth/height per cell (`004a5700`,
   `004b15d0`). Delete `kWorldMax ±128` clamp and `DepthFracAt`’s
   `(wx+128)/256` mapping.
4. **Drill model.** Replace “rising-edge Enter key within 1.5 of a hole”
   with the mode machine: mode 5 → `004764b0` (300-unit hole exclusion) →
   mode 6 → `00477310` (mode 0x18, drilling progress `+0x414/+0x418` with
   step array and `00480820` visuals) → `00477220` (mode 0x27). There is no
   drill cooldown constant; the guards are mode + proximity.
5. **Holes are objects, not points.** Give holes an id (`cellIdx<<16|idx`),
   open flag (`record[3] == 0` required for lookup), per-hole land counter
   `+0x18`, and a 90-unit stand-off point behind them.
6. **Line depth = discrete {10,20,30,40} m**, set by PgUp/PgDn/End and
   `1`/`2`/`3` rod slots (3×7-dword slots at `+0x564`). Remove continuous
   `lineDepth 0..30 at 8·dt` and PgUp/PgDn rate.
7. **Bite timing.** Remove `NextWait() = (4−2.8·depthFrac)(1.2−0.4·type)`
   and the 1.5 s `biteWindow`. Original: repeating “Ambient” timer
   (`rand(75000)+20000` ms, period 1000) calling a 50 % roll
   (`rand(1000) ≥ 500`), action-driven rolls with activity thresholds
   125/250/500/always, server avg-gap ≥ 900 ms limiter, and fish selection
   by 9-cell random walk from `004a9490(pos)` + `rand(count)` picks.
8. **Catch is probabilistic.** Remove “jig key ⇒ instant catch”. Store
   catch/fail probabilities in the bite record by depth band
   (0.7/0.1, 0.9/0.4, 0.95/0.5) and resolve with `rand(10000) < p·10000`
   at strike time (as `00478210` does).
9. **Weight formula.** Replace `0.2 + Rnd·1.8` (and the `newMechanics`
   depth scaling) with `W = min(v·k_line, cap_line)` (k = 1/60, 1/115,
   1/160, 1/230; caps 300/30/30/30) using the tamper-encoded weight field
   (XOR `0x0E5FAF5F` is optional to keep, but the value semantics
   — grams from FishEntry `+0x14 − s16(+0x24)` — must match).
10. **Species generation.** Replace the invented `SpawnFish()` (8 tries over
    depth cells, `c·17/256 > 1`) with the band tables of §4.4.5 and lake
    species ids `lake+0x28a94+idx*4`. The `kDepthTable` 17-entry lookup is
    a `.ves` display table, **not** the runtime species selector.
11. **RNG.** Replace the `1664525/1013904223` LCG with MSVC `rand`
    (`s·214013+2531011`, 15-bit) and `rand(max) = (int)(u30 · 2⁻³⁰ · max)`.
12. **Fish entities.** The original has **no wandering fish list**. Fish
    live in per-cell lists under `lake+0x240c4` (evaluated only at bite
    time) plus the 64-slot bite ring at `session+0x738`. Keep port fish
    dots as pure presentation (gated), but drive logic from cell
    populations / bite records; delete `StartBite`’s “nearest fish
    teleports to player” behavior.
13. **No free `Flee` state.** Missed bites in the original are outcome
    rolls (records `0/2/3`) and AI “no fish” cooldowns (35–40 s / 60–70 s),
    not fish teleporting `±10`.
14. **Bait/lure inventory.** Add the 4×16 bait grid with consume-on-catch
    (`0058f7c0`) and lure-empty gating of line-out
    (`00488060`: no active lure → mode 1).
15. **Round structure.** `game.hpp` has none. Add the move-timeline model:
    mode enum, durations `(int)len[m]·scale[m]`, next-table + pending
    override, countdown chain 9→10→0xc→0xe→0x10→0x12/0x31/0x33, round-end
    hold 2000 ms → Results, pause via Esc/`7c8790`/state 4.
16. **Tally semantics.** Keep `catches`/`totalKg` but add `biggest`
    (`+0x35c`) and per-hole counter; reset via the `00436a30` field set on
    round restart; results rows are `004c2ce0`’s (`Kalojen yhteispaino`,
    `Isoin kala`, `No bites.`).
17. **Null-slot deregistration.** If the port models the slicer lists,
    remember removals write NULL (`0058b450`) and iteration skips NULL.
18. **Timing engine.** Original gameplay delays are `00650fe0` timers
    (named, mode-tagged, some repeating) rather than per-frame float
    accumulators — port should at least reproduce the periods/durations
    (Ambient, forceRise 3000 ms, Nosto, AI_* set, end-hold 2000 ms).
19. **Keep as-is (already matching):** slice splitting in `tick.hpp`,
    elapsed wrap rule, screen-fade timing references, `ForceBite` test hook
    (mark clearly as non-original), `newMechanics` gate (original has no
    such flag — once items 1–16 land, the gate should wrap only *extra*
    mechanics, never base behavior).
20. **Known open items before a 1:1 claim:** VK→command map feeding
    `00495930` (input layer `00416150/00432920/0058bc80`), the
    results-screen builder `004c2ce0` (decompiler flow limit), full walk
    of `0048af00` drilling sub-states, and confirmation of world-unit scale
    (cell 600 vs `.ves` 8.24-fixed coordinates) against a captured run.

---

## 7. Verified constants cheat-sheet

| value | addr | meaning |
|---|---|---|
| 0.34 | `006d9f80` | walk step per ms (K) |
| 0.725 | `006d9fa0` | at-hole step per ms |
| 0.01745329251993889 | `006d9f40` | π/180 (deg→rad) |
| −75 / −90 / −20 | `006da090/006da098/006da088` | stand-off look-aheads (ms·vel) |
| 32400 / 360000 | `006da060/006da068` | 180² / 600² sq distances |
| 600 | grid divisors | world cell |
| 1/600 | `006d9f10` | cell interpolation |
| 1500 f32 | `00799154` | min click distance |
| 900 / 10000 | `006d6b80/006d6b90` | bite min avg gap (ms) / prob scale |
| 2⁻³⁰ | `0073fd80` | rand(max) scale |
| 0.05 / 0.16666 / 0.0001666571 | `006d1618/006dc608/006dc5b0` | bite timing value ramp |
| 1/60, 1/115, 1/160, 1/230 | `006d09b8/006dc5e8/006dc5d8/006dc5d0` | weight k per line |
| 300 / 30 | `006da020` / 30.0 | weight caps per line |
| 0.7/0.1, 0.9/0.4, 0.95/0.5 | `006dd0b0/c0/d0` | catch/fail p by depth band |
| 470 / 670 | in `00473320` | min weight for release effect (type 3: 670) |
| 125/250/500 | `00479700` | bite attempt thresholds by activity |
| 270 | `0x10e` in `0047f740` | HUD progress divisor (modes 3/4) |
| 20000..94999 | `0047f740` | Ambient timer duration (`rand(75000)+20000`) |
| 214013 / 2531011 | `00676d2c` | MSVC rand constants |
| 0x0E5FAF5F | `00590c10/00590e70` | weight-field XOR key |

---

## 8. Errata (2026-10-14) — claims in this document superseded by the
## per-system passes. Authority: docs/sim-{ai,timeline,bite,controller,walk,stock}.md.

This document was the first-pass analysis. The follow-up passes (each
with its own decomp walk + port) disproved or refined the items below.
Where both disagree, the per-system doc wins.

### §1/§2 object model
- `00630790` is a **state-transition request** (writes `+0x44`, `+0x30 = 3`,
  logs `Go to state:`), not a queue push. Push = `006303c0`/`0062ffe0`/
  `00630040` (0x14-byte record = id + 4 payload dwords), pop = `00630740`.
  (docs/sim-ai.md §1)
- `session+0x3c0` is the **next-state latch** (epilogue `prev <- state;
  state <- pending`), not a "substate"; state entry is `prev != state`,
  init `-1 -> 1`. (docs/sim-controller.md §1)

### §3 input
- **There is no VK→command map.** `00495930` returns `menuObj+0x28`, a
  **UI list selection index** (mouse-picked, getters `00595220/80/b0`).
  Keys enter raw through the queue `7bcd60`; `004876d0` maps VK `0x20` =
  **Space** -> action 3 (this doc's "ESC (`0x20`)" is wrong — Esc is
  handled one level up in the 00640370 state-20 block). Command→bit
  table 0..10: docs/sim-walk.md §3. (docs/sim-walk.md)
- Click orders: `0047f530` rejects only `x<0||y<0`; **out-of-bounds
  clicks are accepted** and the walk stops at the wall (world extents
  `(w−1)*600` from `lake+0x15160/64`). `004b0bb0` = obstacle triangles +
  300-unit hole exclusion only. (docs/sim-walk.md §1)

### §3.3/§4.2 controller
- Controller state `0xd` is the **fish fight** (kala_force timer,
  forceRise 3000 ms hold, abort re-inits at power·0.75), not "drilling".
  The drill run is its own path: `stepMs = (rand(4)+13)*200` (not
  `rand(13)`), `power = max(depth, 70)`, `ticks = ((w*0.5+0.8)*power +
  w*70)*0.04`, depth ladder `004acc40/004acbf0/004abf20`, both stop
  rolls run (second overwrites), type-3 & weight>1000 & `rand(3)<2` =
  forced full run. (docs/sim-controller.md §2/§3)
- Walk pause duration is `(players<8 ? (23−players)*25 : 400)` ms,
  doubled in controller state 8. (docs/sim-controller.md §4)
- `00479700`'s thresholds (125/250/500/always) key off the **record
  weight grams** (`00590c10(rec)`) — "activity" was a mislabel; ambient
  attempt (`rec == 0`) is `rand(1000) <= 500`. `00651220(0)` reads the
  timer **fired** flag and bootstraps fight timers. "Nosto" duration =
  `00487560(+0x340)`, not 1000 ms. (docs/sim-controller.md §5)

### §4.3/§4.4 bite + fish stock
- Fish populations are built **at lake load** from the `.ves` DATA1
  section (deobfuscation `00430f50`: byte-reverse + XOR key
  `fkjsafdBLKJBbljbefwlkj`, `006dbfa8`; DATA2 key
  `hjdfHGFAJKGghafkjhg`), `0xfc0`-byte entries (`005f58b0`) placed per
  cell by `004a9490`; species = 0x310 ids at `lake+0x28a94` from
  `data/nyky/data0.dat` (9 species); names pool `0070af30` (1 Ahven ..
  26 Made). Migration `004bc340`. (docs/sim-stock.md, docs/sim-bite.md)
- The candidate search is a **static 3×3 walk around the seed cell**;
  this doc's "seed = cy" advance rule is wrong (seed reloaded per
  iteration, `004ba71e`). (docs/sim-bite.md §2)
- Bite record `0x70`: weight v at **rec+0x0c** (XOR `0x0E5FAF5F`);
  outcome byte from the 33-row `00746278` **type** ramp + two
  `rand(101)` rolls (thr 95/60/30/0/10); depth bands selected by v
  against the line-rule u32 (300/700/1200/1500 bonus); timing ramp
  boundary `v >= 1001`; `00590ce0` ring params. (docs/sim-bite.md §3)
- Species band tables: the second halves in §4.4.5 are wrong (real
  values dumped in docs/sim-bite.md §4); selection is
  `004acc80(type, v)`. (docs/sim-bite.md §4)
- Rate limiter = exact sliding gap-sum window (`00592160`), avg ≥ 900.

### §4.6 round structure
- The `9 → 10 → 0xc → 0xe → 0x10 → {0x12|0x31|0x33}` chain is the
  **drill animation chain** (KAIRAAMINEN1..5 → LAPI_MENI /
  POHJAAN_MENI), not a pre-round countdown; stages 1000 ms, hop hold
  `0047a1c0: (v<=100 ? v*1000 : 100000)+200`. Mode names verified:
  0x18 = JUST_SITTING (fishing), 0x1e/0x1f = JUOKSU (run), 0x25/0x26 =
  NOSTO (lift), 0x27/0x28 = KORJAILU (mending), 5 = PAIKALLAAN.
  (docs/sim-timeline.md §2)
- Round end hold is **2300 ms** (`0x8fc` ramping `7c895c` to 2000),
  not 2000; sequence: end-mode rising edge (`00476180|00476160`) →
  end timer `00650fe0` → `00483890(2,0)` freeze → `00632880()` → state
  0x15. Round time limit = `min(minutes,60)*60000` ms.
  (docs/sim-timeline.md §5)
- Move script: heap block built by `AddMove 00463b50` (max 70 moves);
  names at `script+0x522b0+move*30` (not `+0x29158`). Source = the
  `<STARTMOVES>` block of `gfx/3ds/human2.3ds` (parsed by
  `model3ds.hpp`). (docs/sim-timeline.md §1/§3)

### §4.5 results
- `004c2ce0` is the **string-table init** (6288 entries), not per-round
  logic. Verified rows by id: 0x96 total catch weight, 0x24c biggest
  fish, 0xa7 `No bites.`, 0xad bite-flag, 0x2bc `Initial drill`.
  (docs/sim-results.md)

### §5 AI
- Event pump `004710e0` (timer→event map: no_fish_long→7, ei_syo→6,
  bore→5, korjaus/nosto/sinking→4; syoko 1 s bite roll; strike decision
  = `skill+1` quality draws vs `rand%100`) and the four AI_STATE_*
  machines are ported in `src/botai.hpp`. The "too many players close"
  guard is `00472cf0` travel-cost-vs-round-clock (`004383c0`), not a
  distance test. (docs/sim-ai.md)
