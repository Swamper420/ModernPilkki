# Opponent / AI analysis — `AI_STATE_*` machines + shared action queue

Companion to `docs/sim.md` §5. All VAs are image addresses in
`extracted/ProPilkki2.exe` (`00478b10` = `0x00478b10`). Decompiled with
Ghidra headless (`/tmp/opencode/ghidra-ai`), dumps in `/tmp/opencode/ai/`
and `/tmp/opencode/sim-dec/`. Analysis only — clean-room port code lives in
`src/botai.hpp`. Claims are **verified** (decompiled body) or *(inferred)*
(stitching of several bodies / role naming).

Two corrections to earlier notes (both verified against decompiled bodies):

1. **`00630790` is not a queue push.** It is a state-transition request:
   it logs `"Go to state: "` and writes `this+0x44 = id`, `this+0x30 = 3`
   (request pending). The queue **push** is `006303c0(state, action)`
   (`dlist` push-tail `0058ae40`, logs `"Input Event. id:"`); the **pop**
   is `00630740(state)` (front of the `dlist` at `state+0x50`, logs
   `"Event Consumed:"`, returns the 0x14-byte record; caller frees via
   `0066e3cf`).
2. **The "too many players close" guard is a time-budget guard.**
   `00472cf0(x, y)` computes a *travel cost* (seconds-like) from the
   anchor at `human+0x318/0x31c` *(inferred: home / captain point)* to
   `(x, y)`, sampling walkability (`004af230`) along the line; `004383c0`
   returns the round clock in seconds (`env+0x64 / 100`, a centisecond
   counter). The guards compare cost vs clock (below), not player counts.

---

## 1. Architecture: state objects, action queue, per-slice pump

Everything (menu screens and AI alike) derives from one `State` base class:

| offset | field |
|---|---|
| `+0x00` | vtable (slot 2 = `vft[+8]` per-slice handler) |
| `+0x04` | owner `human*` (AI states) |
| `+0x08, +0x0c, +0x10` | per-state scratch flags (each subclass reuses them) |
| `+0x30` | transition status (`3` = request pending) |
| `+0x44` | requested next state id ("Go to state: ") |
| `+0x50` | `dlist*` action queue (0x14-byte action records) |
| `+0x54` | log handle |

Per frame the entity slicer (`00471810`, sim.md §2) processes listA first
(sessions → `0047f740` = movement, mode edges, and the AI event pump
`004710e0`), then listB: each state object gets one `vft[+8](slice)` call.
The base implementation `006307c0` pops **one** action from `+0x50`, logs
`"Event Consumed: id"`, frees it, returns. AI subclasses override slot 2
with their switch handler (`00477b90`, `00477e20`, `00478210`, `00478b10`,
…). `00433f00` (human input override slot 2) is a no-op returning 0.
`006306f0`/`00630700` are vft[+8] thunks (`00630700` also copies
`+0x48 → +0x4c` before dispatch).

So the pump model is: **listA tick posts actions → the state drains at most
one action per slice → handlers call `00630790(id)` to request a state
change → the owner swaps state objects later in the tick** (the handler
finishes its current body first; `+0x30 = 3` is just a request).

### 1.1 Action record (0x14 bytes)

Ctors: `0062ffe0(id)` zeroes payload; `00630040(id, p1, p2, p4)` sets
`[0]=id, [1]=p1, [2]=p2, [3]=0, [4]=p4`; the 5-arg variant (used via
`00476b80`) sets `[1..4] = p1..p4`. Layout: `[0]` id, `[1..4]` payload
dwords.

### 1.2 Action id table (0..0x10)

Producers verified via `0062ffe0`/`00630040`/`00471560(id)` call sites.
`00471560(id)` = "post id as AI event" (gated on `human+0x69c == 1`,
AI enabled).

| id | payload | producers (VA) | meaning |
|---|---|---|---|
| 0 | `p4` = route object | `0047a000` (net route resp), `00478d90`/`0047a000` route swap, `004710e0` delayed edge | release/cleanup route (`00478d90` case 0 destroys `p4` as a `dlist`); the lake-edge timer in `004710e0` posts a payload-less one *(inferred: disturbance)* |
| 1 | — | `0047a000` (net route ack with empty/short route) | route rejected/short *(inferred from producer)* |
| 2 | `p1=x, p2=y` (from `00476980`) or — (`004710e0`) | `00476980` (walk order), `004710e0` mode-0x18 edge | walk order to (x, y); also "bore mode 0x18 entered" when posted by the pump — meaning is per current state |
| 3 | — | `00479ba0` (stop at hole, line ready), `004710e0` mode-5 edge | stopped at hole / "standing ready" (mode 5) |
| 4 | — | `0047f740` (walk-arrival latch `+0x5e0`, within 180 units), `004710e0` (`+0x630` timer fired) | **main verb**: "arrived, do your thing" (WALKING: drill) / "timer elapsed, advance" (FISH_TAKE_UP phases) |
| 5 | `p1=x, p2=y, p3=bearing, p4=routeList` | `00476b80` (click walk order); `004710e0` (`+0x62c` AI_bore_timer2 fired) | route walk (`00478d90` → `00474ab0`); in FISHING: "bore timer tick" |
| 6 | — | `004710e0` (`+0x628` AI_ei_syo_short fired) | "no fish short" elapsed |
| 7 | — | `004710e0` (`+0x624` AI_no_fish_long fired) | "no fish long" elapsed |
| 8 | — | `004710e0` strike roll failed | bite happened, bot did not strike |
| 9 | — | `004710e0` strike roll passed | **strike / take up** |
| 10 | — | `0047a000` (route ok), `00478d90` | route accepted (no-op in FISHING) |
| 0xb | — | `0047a000` (route short) | route retry hint (no-op in FISHING) |
| 0xc | — | `0047f4a0` (walk cancel `+0x4bc=0`, `00479c60`) | movement cancelled |
| 0xd | — | `004710e0` (cost > clock, latch `+0x620`) | **abort**: can't make it home before round end |
| 0xe | — | `00471060`, `0047f530` (click-to-move) | player/click walk order |
| 0xf | — | `004710e0` mode-0x35 edge | mode 0x35 entered |
| 0x10 | — | — | **open**: no producer found in the dumped set |

### 1.3 "Go to state:" id table (transition requests, `00630790`)

Ids 0..0x10 index the AI state class table (~17 `AI_STATE_*`/`HUM_STATE_*`
RTTI classes, sim.md §5). Mapping of the four in-scope machines is fixed by
edge semantics (marked ✔); the rest stay numeric in the port.

| id | class | evidence |
|---|---|---|
| 0 | route executor (≈ `00478d90`) *(inferred)* | WALKING ev4 fallback after `00476040` route probe |
| 1 | *(open)* | `00478d90` requests it after issuing a walk order |
| 2 | **AI_STATE_WALKING** ✔ | every machine returns here |
| 4 | **AI_STATE_DRILLING** ✔ | WALKING ev4 after `004764b0` drill start |
| 5 | **AI_STATE_FISHING** ✔ | DRILLING ev2 after bore prep; FISH_TAKE_UP phase 2 |
| 8 | **AI_STATE_FISH_TAKE_UP** ✔ | FISHING ev9 (strike) |
| 9 | *(open)* | WALKING ev0xc (cancel) |
| 10 | give-up / idle family *(inferred)* | every abort path |
| 0xc | *(open)* | `00472b00` when target search fails |
| 0xe | *(open)* | WALKING ev1 when shallow + role 1/2 |
| 3, 0xd | *(open)* | `00477d30` ev10 branches |

---

## 2. The AI event pump `004710e0` (per human, per slice)

Runs inside the session tick (listA), before the state handler (listB).
Posts at most one timer event (first fired in priority order wins, timer
killed + slot zeroed like `00471510`/`00651410`), then always evaluates the
mode-edge block (which may post one more).

Priority order (verified):

1. `human+0x624` **AI_no_fish_long** fired → post **7**.
2. `human+0x628` **AI_ei_syo_short / AI_no_fish_short** fired → post **6**.
3. `human+0x62c` **AI_bore_timer2 / AI_bore / AI nosto** fired → post **5**.
4. `human+0x630` **AI_korjaus / AI nosto / AI sinking** fired (`00471510`
   = fired?-and-kill) → post **4**.
5. `human+0x620 == 0` and `00472cf0(pos) > 004383c0()` (travel cost home
   exceeds round clock) → latch `+0x620 = 1`, post **0xd**.
6. `human+0x634` **AI_syoko_timer** fired → **bite roll** block:
   * builds the bite-request context (`00590640` on stack; `ctx+8 =
     skill*10` = activity, `00590e90(x, y, depth)`, `00590e20(0)`,
     `00590dc0(hole)`, `00590dd0(20)`, `00590db0(0)`), depth via
     `004a5700(x, y, 1)`, then the server-side roll `004ba5e0(ctx,
     human+0x6c8)` writes the bite record.
   * validators may discard it (`+0x52c`, `0042cb10() ∈ {0xb, 0xc}`,
     `human+0x6d0 != 0x1a`).
   * no bite → re-arm `AI_syoko_timer = 1000` ms ("AI syoko(human.cpp)").
   * bite stands → **strike decision** (the skill roll):
     `span = 0059fad0()` = `(int)(ctx+0x308 * ctx+0x30c)`;
     draw `skill+1` samples `q = 0059fbe0(rand() % span)` (byte-curve
     interpolation over the context's quality table, `006d0548 = 0.001`
     rounding epsilon); `best = max(q)`;
     `rand() % 100 < best` → post **9**, else post **8**.
7. Mode-edge block (latches `+0x5fc / +0x600 / +0x604 / +0x608`, one-shot
   per mode entry — the flags are refreshed to `(mode == X)` by `0047f740`):
   * mode `0x18` (bore) entered → post **2**;
   * mode `5` (standing) entered → post **3**;
   * mode `0x35` entered → post **0xf**;
   * lake-edge trigger `004b15c0()` (`human+0x608 == 0`, lake present):
     stamps `+0x60c = now`, `+0x610 = rand(100)`; once
     `now − +0x60c > +0x610` → post **0**, clear `+0x60c`.

Other event producers outside the pump: `0047f740` posts **4** on walk
arrival (within 180 units of target, latch `+0x5e0`), `00476980` posts
**2(x,y)** on a walk order, `00476b80` posts **5(x,y,bearing,route)**,
`00479ba0` posts **3** (stopped at hole), `0047a000` (net route reply)
posts **0/1/10/0xb**, `0047f4a0` posts **0xc**, `0047f530`/`00471060`
post **0xe**.

---

## 3. AI timers (all verified; `004715e0(slot, ms, name)` = kill + arm
`00650fe0(name, 0, 1000, ms, 0, 8)`, `00471620(slot)` = kill + zero)

| timer | slot | formula | arm sites |
|---|---|---|---|
| AI_ei_syo_short | `+0x628` | `((rand() % (11 − skill)) − skill + 20) * 1000` | FISHING ev5/6/8/0xe, `004728b0` |
| AI_no_fish_short | `+0x628` | same formula | FISHING ev7 |
| AI_no_fish_long | `+0x624` | `rand(5000) + 35000` (no lake model) / `rand(10000) + 60000` (lake, `+0x528` and `004aeeb0`) | FISHING ev7/8/0xe, `004728b0` |
| AI_syoko_timer | `+0x634` | `1000` | FISHING ev7/8/0xe, `004728b0`, syoko miss retry |
| AI_bore_timer2 | `+0x62c` | `(rand() % 100 + 20) * 100` | FISHING ev7/8/0xe, `004728b0` |
| AI_korjaus | `+0x630` | `(14 − skill) * 1000` | FISH_TAKE_UP ev4 phase 0 tail |
| AI nosto | `+0x630` | `5000` | `00472a40` (take-up entry) |
| AI sinking | `+0x630` | `3000` | FISH_TAKE_UP ev4 phase 1 |
| AI_Temp | `+0x630` | `500` | WALKING ev3 (blocked) |
| AI_bore | `+0x62c` | `2000` | `00472ca0` *(entry wiring inferred)* |

`rand()` above = `00676d2c` raw 15-bit; `rand(n)` = `0059db20` (two raw
draws → 30-bit `u = (r2 << 15) | r1`, value `(int)(u * 2⁻³⁰ * n)`, clamp
`n−1`, `n = 0 → 0`; `00777a20 = 0.0` addend, verified).

Entry/exit helper family (bodies verified, wiring between states
*(inferred)*): `00472a40` arm AI nosto 5000 + `00477880` line stage +
clear `+8/+0xc/+0x10`; `004728b0` arm the four no-fish timers;
`00472ca0` arm AI_bore 2000 + clear `+8`; `00472830` reset stats
`+0x354/358/35c` + flags; `00472f30`, `00472f60` (kill syoko),
`00472f90` (kill korjaus + clear flags), `00473010` (clear `+8`, kill
`+0x62c/+0x630`); `00472780` decrements `human+0x5f0` (clamp 0).

---

## 4. State machines

Human fields used: `+0x168` (team flag; `== 3` branches), `+0x170`
(role; `1/2` checked), `+0x350` consecutive-miss counter, `+0x354` catch
count, `+0x358` total weight, `+0x35c` biggest, `+0x5f0` boredom counter,
`+0x620` abort latch, `+0x6a0` skill (0..10), `+0x6c0` foot/motion byte,
`+0x6c8` bite record (`+0x6d0` type, `+0x6e8` fail p, `+0x6f0` catch p,
`+0x71c` outcome byte, weight read back through the obfuscated field
`00590c10`, XOR `0x0E5FAF5F`). State scratch: `+8/+0xc/+0x10` per class.

### 4.1 AI_STATE_WALKING — `00478b10`

Scratch: `+8` = blocked flag.

| ev | action |
|---|---|
| 1 | if `human+0x170 ∈ {1, 2}` **and** `00472480()` (depth at `pos + vel*75` < 30, `004a5700(...,1)`) → `GoTo(0xe)`; else `+8 = 1` (blocked) |
| 3 | if `+8 == 1` → arm AI_Temp 500 ms at `+0x630` |
| 4 | time-budget guard first: `00472cf0(pos) > 004383c0() − 25` → `GoTo(10)`. Else drill chain: `c1 = 00472480()` (shallow), `c2` = area gate (`human+0x52c == 0` OR `00439900()` (env mode ∉ {0x1c, 0x1d}) OR `00437a40(0)` empty → pass; else `00439a80` quad contains drill point `pos + vel*75`). If `!c1 && c2 && 004764b0() == 1` (drill start ok) → `GoTo(4)` = DRILLING. Else if `human+0x168 != 0` and `00476040()` (route probe via `004381e0/004b08c0`) → `GoTo(0)`. Else `00472780()` (boredom−−) and `GoTo(2)` (stay walking) |
| 5 | if `00476270()` (mode == 5) → fall through to ev 0xe |
| 0xe | `GoTo(2)` |
| 0xc | `GoTo(9)` |
| 0xd | `00471620(+0x630)` (kill korjaus/temp), `GoTo(10)` |
| other | consume only |

Note the drill gate polarity: the bot drills where the drill point is
**not** shallow (`c1 == 0`, i.e. depth ≥ 30) and the area gate passes.

### 4.2 AI_STATE_DRILLING — `00477b90`

Scratch: `+8` = abort flag (ev 0xd), `+0xc` = bore-seen flag (ev 0xe).

| ev | action |
|---|---|
| 2 | `00477310()` (bore begin: `+0x438 = 1`, mode → `0x18`, net `0x17`). Then: `+8 != 0` → `GoTo(10)`; `+0xc == 0` → HUD/chat event 10 (`004993c0`, gated by `DAT_007c8484`) + `GoTo(5)` (FISHING); else `human+0x6c0 = 0`, `GoTo(2)` |
| 3 | if `+0xc != 0`: `human+0x6c0 = 0`, then `GoTo(+8 ? 10 : 2)` |
| 0xd | `+8 = 1` |
| 0xe | `human+0x6c0 = 1`, `+0xc = 1` |
| other | consume only |

### 4.3 AI_STATE_FISHING — `00477e20`

| ev | action |
|---|---|
| 5, 6 | `004771d0()` (jig: mode → `0x24`, net `0x14` when server), arm AI_ei_syo_short |
| 7 | kill the four timers (`+0x628/+0x624/+0x634/+0x62c`). If `human+0x168 != 3` → chat event 6 + `GoTo(2)`. Else `004771d0()`, arm AI_no_fish_short, AI_no_fish_long, AI_syoko 1000, AI_bore_timer2, chat event 5 |
| 8 | if `+0x624` armed and `006510d0` elapsed > 29999 → chat event 4. Re-arm all four timers (fresh no-fish-long roll) + `004771d0()` |
| 9 | kill the four timers, `GoTo(8)` (FISH_TAKE_UP) |
| 10, 0xb, 0xc | consume, no effect |
| 0xd | kill four timers, `GoTo(10)` |
| 0xe | kill four timers, `GoTo(2)`, then the ev-8 tail (re-arm four + jig) |
| other | consume only |

Bite cadence therefore = the pump's 1 s AI_syoko_timer rolls; "no fish"
cool-downs 35–40 s / 60–70 s come from AI_no_fish_long via ev 7.

### 4.4 AI_STATE_FISH_TAKE_UP — `00478210`

Scratch: `+8` = phase 0/1/2, `+0xc` = abort (ev 0xd), `+0x10` = special
(ev 0xe). Entry (`00472a40`, *(inferred wiring)*): AI nosto 5000 ms at
`+0x630` (the fight time), `00477880` line stage, flags cleared.

Non-ev-4 events: **0xd** → `+0xc = 1`. **0xe** → if phase 0, chat event
`0x14`; `+0x10 = 1`. Others consume.

**ev 4, phase 0 — fight resolution** (bite record `human+0x6c8`):

* `outcome == 2` → `00473280` (success path: chat event 8, restart bite
  chain) ; `outcome == 3` → `004732d0` (escape path: chat event 7);
  `human+0x350 = 0`. No rolls, no stats.
* else if `type == 3` (`human+0x6d0`): two rolls **in this order**:
  `caught = rand(10000) < (int)(p_catch * 10000)` with `p_catch` =
  `human+0x6f0` (depth-band catch p, sim.md §4.4.7) and
  `escaped = rand(10000) < (int)(p_fail * 10000)` with `p_fail` =
  `human+0x6e8`. Both draws always happen (RNG order matters).
  `caught` → `00473280`; `escaped` → `004732d0`; both clear `+0x350` and
  return weight 0.
* else (or both rolls fail) → `w = 00473320(bite, 0, 0)` (normal land:
  weight, hole counter, effects; returns > 0 on land).

Then, when `+0xc == 0` (not aborting):

* `+0x10 == 0` path: `w > 0` → `+0x350 = 0`, stats block; else if
  `+0x350 < 1` → `+0x350 += 1` (one more chance); else `+0x350 = 0` and
  (team `== 3` → chat event 0xd; else chat event 0xe + `GoTo(2)`).
* `+0x10 != 0` path: `+0x350 = 0`, `GoTo(2)`, then the stats block.

**stats block** (only when `w > 0`; weight from `00590c10()`):
`+0x354 += 1`; `+0x358 += weight`; `+0x35c = max(+0x35c, weight)`.
At `+0x354 == 5` the **5-catch review** runs:
`if (+0x35c < 50 || +0x358 / 5 < 30)` → give up: `+0x350 = 0`, chat
event 0xf (team 3) or 0x10 + `GoTo(2)`, counters `+0x354 = +0x358 = 0`;
else just reset `+0x354 = +0x358 = 0`. Either way `+0x35c = 0` after.
Then `00472780()` (boredom−−) and `weight > 100` resets `+0x5f0 = 0`.

Then (still `+0xc == 0`): `+8 = 1` (phase 1), arm **AI_korjaus =
(14 − skill) * 1000** ms at `+0x630`, `00477220()` (bore end, mode →
`0x27`). When `+0xc != 0` → `GoTo(10)` instead.

**ev 4, phase 1**: `+0x10 != 0` → `+0x350 = 0`, `GoTo(2)`; else if
`+0xc != 0` → `GoTo(10)`; else arm **AI sinking 3000** ms at `+0x630`,
`+8 = 2`, `00477310()` (bore begin).

**ev 4, phase 2**: `+0x10 != 0` → `+0x350 = 0`, `GoTo(2)`; else if
`+0xc != 0` → `GoTo(10)`; else `+8 = 0`, `GoTo(5)` (back to FISHING).

So one catch cycle = strike (ev 9) → nosto 5 s → resolve → korjaus
(4–14 s) → sinking 3 s → FISHING again.

### 4.5 Adjacent states (reachable from the four, partial)

* `00478d90` — route executor *(likely `HUM_STATE_WALKING_ROUTE` or
  `AI_STATE_GOING_TO_CAPTAIN`)*: ev 5 → `00474ab0(human, route, x, y,
  bearing)` (heading `+0x520`, velocity dir, waypoint `+0x4ac/+0x4b0`,
  route list `+0x694`) + `GoTo(2)`; ev 2 → net walk msg (`005970d0(1,
  0x20000)`, seq `+0x5e8 & 0xff`, hole `+0x4c0`), `GoTo(1)`; ev 0 →
  destroy route object in `p4`.
* `00472b00` / `00477a40` — target search states: `00473070(out)` picks
  the next target: current buddy from list `human+0x23c` (`00438200`),
  only when its entry flag `+0xa4` and distance to self > `00799164 =
  1200`; then up to **10 tries** of `r = rand(500) + 100`
  (`006d09c0 = 100`), angle `rand(360) * π/180` (`006d06b0`), around the
  buddy position; first walkable point (`004b0bb0(x, y, −1)`) wins.
  `00472b00`: found → `00476980(x, y)` walk order + `+8 = 0`; else
  `GoTo(0xc)`. `00477a40` counts attempts (`+8`), every 7th re-picks mode
  via `00479f80`, re-arms `+0x62c` with `DAT_00798f48` ms.
* `004779f0` — ev 0 → `GoTo(2)`, ev 5 → `00479f80()` + `GoTo(1)`.
* `00477d00` — ev 0xd → `GoTo(10)`. `00477d30` — ev 10/0xb/0xe/0xd
  home/captain logic (`00470890`, `00476980`).
* `00472bb0` — arms `+0x62c` 3000 ms, `GoTo(2)` when `004b0bb0` says the
  current cell is unwalkable. `00472ca0`/`00472f30`/`00472f60`/
  `00472f90`/`00473010` — entry/reset helpers (§3).

---

## 5. Skill effects (skill `human+0x6a0`, 0..10)

| effect | formula / site |
|---|---|
| AI_ei_syo_short / AI_no_fish_short wait | `((rand() % (11 − skill)) − skill + 20) * 1000` ms (10 s at skill 10, 20–30 s at skill 0) |
| AI_korjaus (post-catch pause) | `(14 − skill) * 1000` ms (4 s … 14 s) |
| strike probability | `skill + 1` draws of the quality curve `0059fbe0(rand() % span)`, max vs `rand() % 100` (`004710e0`) — more draws ⇒ higher max ⇒ likelier strike |
| bite-request activity | `ctx+8 = skill * 10` fed to `004ba5e0` (activity band thresholds 125/250/500/always, sim.md §4.4) |
| give-up thresholds | **not** skill-dependent: fixed `biggest < 50` g or `avg < 30` g over 5 catches |

No skill effect on target selection or on the rolls in FISH_TAKE_UP
(`p_catch`/`p_fail` come from the bite record only).

---

## 6. Integration notes (for the orchestrator)

* Port module: `src/botai.hpp` (namespace `botai`, header-only, no SDL),
  tests `src/test_botai.cpp`. It implements the action record + queue
  (`00630040`/`006303c0` push, `00630740` pop), the transition request
  (`00630790` semantics: deferred swap applied after the handler returns),
  the five AI timer slots with the verified formulas, the pump's
  timer→event mapping and strike decision (`004710e0`), and the four
  machines 1:1 (`00478b10`, `00477b90`, `00477e20`, `00478210`),
  including the 5-catch review and the two-roll fight resolution.
* **Replacing `offline.hpp`'s `TickBot`**: keep `Comp`'s roster/chat/leader
  board, replace the wander/drill body with:

  ```cpp
  struct GameHooks : botai::Hooks {
    game::Game &g; int me;               // opponent index
    int  DrillStart() override { return game::DrillStart(g, me); }  // 004764b0
    void BoreBegin() override { game::BoreBegin(g, me); }           // 00477310
    void BoreEnd()   override { game::BoreEnd(g, me); }             // 00477220
    void LineStage() override { game::LineStage(g, me); }           // 00477880
    void Jig()       override { game::Jig(g, me); }                 // 004771d0
    int  LandFish(botai::Bot &b, const botai::Bite &bt) override {
      return game::LandFish(g, me, bt); }                           // 00473320
    bool BiteRoll(botai::Bot &b, botai::Bite &bt) override {
      return game::BiteAttempt(g, me, bt); }                        // 004ba5e0
    // … world queries wired to the walk grid / depth map / round clock …
  };
  // per opponent, per frame (or per slicer slice, dt ≤ 1000 ms):
  botai::Tick(bot, hooks, dtMs);
  ```

  The seam is the **action queue + `Hooks` primitives** so the player
  controller (`0048af00` port) and bots both call the same drill/walk/
  strike/reel/bite primitives; external game events (arrival, mode edges,
  click orders) enter through `botai::Post(bot, action)` exactly like
  `004710e0`/`0047f740` post into the original queue.
* RNG is `botai::MsvcRand` (MSVC `rand`, sim.md §4.7) seeded per bot — the
  whole machine is deterministic given a seed; do not mix in `std::mt19937`
  draws inside the machines.
* Keep the toy `TickBot` behind a flag until the walk grid, hole objects
  and bite records exist in `game.hpp`; `botai` only needs the hooks to be
  meaningful.
* Unported state ids (0, 1, 3, 9, 10, 0xc, 0xe) park in the base consumer
  (`006307c0`) and record the request in `Bot::pending` — the dispatcher
  maps only 2/4/5/8 to machines.

## 7. Open items

* Event id **0x10**: no producer found in the dumped function set.
* Exact class-name mapping for transition ids 0, 1, 3, 9, 0xc, 0xe
  (needs RTTI vtable walk).
* `004383c0` return unit: seconds of round clock *(inferred)* — confirm
  against a captured run (`env+0x64` centisecond counter).
* Entry wiring of `004728b0`/`00472ca0`/`00472830` family to specific
  states *(inferred from bodies only)*.
* `00472cf0` anchor (`human+0x318/0x31c`) = home/captain point
  *(inferred)*.
