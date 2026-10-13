# Player controller + drill/fight mini-game (clean-room analysis)

Target: `extracted/ProPilkki2.exe` (x86, VS2022). All addresses are image VAs.
Sources: Ghidra dumps `/tmp/opencode/sim-dec/`, follow-up headless dumps
`/tmp/opencode/ctrl/helpers.c`, raw disassembly `/tmp/opencode/ctrl/asm48af00.log`,
constants via `tools/peconst.py`. Analysis only plus the port notes at the end.
Claims are marked *(verified)* (directly read from decompilation/disassembly) or
*(inferred)* (interpretation of meaning, not of code).

Primary functions:

| addr | role |
|---|---|
| `0048af00` | player controller: switch on `session+0x3b8` (state), 2089 lines |
| `004876d0` | input → action bitmask consumed by `0048af00` |
| `00489220` | action dispatcher used from states 2/4/5 (drill entry `0xc`, fight `0x23`) |
| `0048ace0` | "can force-fish here" gate for the `0xc` entry ("kala_force" = fish force) |
| `004873c0` | depth-ease duration formula |
| `004875f0` | walk/stand-up timer duration formula |
| `004ac620`/`004acc40`/`004acbf0`/`004abf20` | line-depth bucket ladder |
| `00590a40` | auto-bite percent table (action bit `0x1000`) |
| `00479700` | bite attempt (50 % path / weighted threshold path) |
| `00473280`/`004732d0`/`00473320` | catch-success / escape / landed-fish processing |
| `004764b0`/`00477310`/`00477220`/`00477880` | game-mode edges of the hole-drill chain |
| `00650fe0`/`00651220`/`00651410` | timer create(name, from, to, durMs, outSlot, mode) / fired? / kill |

---

## 1. Controller state model *(verified)*

`0048af00` end-of-function epilogue (`0048f5xx`):

```
if (session+0x3b8 != 0) {
    session+0x3bc = session+0x3b8;   // prev  <- state
    session+0x3b8 = session+0x3c0;   // state <- pending
}
```

So the three words are: `+0x3b8` = **current state**, `+0x3bc` = **previous
state**, `+0x3c0` = **pending (next) state latch**. Transitions are requested by
writing `+0x3c0`; they take effect at the end of the same frame. Entry code is
gated by `+0x3bc != +0x3b8` (runs exactly once on the first frame in a state).
Initial state is `-1` (case `-1` writes pending `= 1`); state `0` freezes the
machine (epilogue skipped). Correction to `docs/sim.md` §3.3: `+0x3c0` is not a
"substate", it is the next-state latch; the switch cases are one flat state
space `{-1, 1, 2, 4..0x23}` (no case 3; case 3 falls to the default arm).

`0048af00(param)` stores `param` into `session+0x5c0` = **input source**
(0 = human, 1 = AI, 2 = network). `004876d0` short-circuits: `+0x5c0 == 1`
returns action `1` every frame; `+0x5c0 == 2` returns `1` only in the net case.

### 1.1 State table

Timers are `00650fe0(name, from, to, durMs, &slot, mode)`; "gen0..gen4",
"line_x", "line_y", "nyky", "aalto", "kala_force" are the literal timer names
(`006dab0c/14/1c/34/3c/44/4c`, `006dab24/2c`, `006dabb8`). Slots are the
session timer handles `+0x20c/210/214/220/224/228/22c/230/238/24c`. Timer
"easing modes" seen: 8, 9, 0xb, 0xc, 0x1d (and 0 in "kala_force"); the exact
curves live in `00651620` which is **open** — only durations and firing matter
for the state machine.

| state | name *(inferred)* | entry (`prev != state`) | per-frame / exits |
|---|---|---|---|
| `-1` | init | — | pending `= 1` |
| `1` | idle (line in, free) | — | if pending==2 skip. bit **2** → drill prep (below) → pending `7`. bit **0x10** → pending `0x16`. bit **8** (needs `+0x1c0 != 0`) → pending `0x14`, or `0x15` + HUD `0049b9c0(0,7000)` when the lure is busy. bit **0x20** → pending `0x19` |
| `2` | line out / jig | — | cmd `= 00490060()`: 1/3 → pending `4`; 2/4 → pending `0x11`. Action source `iVar14 = 00432920()`: 1 → bucket `= 004acc40(+0x420, +0x41c)` (reel in); 2 → `004acbf0` (let out); on change `004abf20(+0x420, newBucket)`, pending `0x12`. 3 → pending `0x13`, `+0x5c8 = 0`; 4 → pending `0x13`, `+0x5c8 = 1` |
| `4` | walk-1 | — | when cmd `00490060()` ∈ {0,4} and `00489220()` idle: create gen0 `+0x4f0→+0x4a8`, gen1 `+0x4f8→+0x4b0`, gen2 `+0x4fc→+0x4b4`, all dur `WalkMs(players)` mode `0xb`; pending `5` |
| `5` | walk-2 | — | on gen timers fired: kill 4, pending `2`. cmd == 1 → pending `4`, kill 4 |
| `6` | drill hold | `+0x3fc = 1000` | bit **1** → kill, pending `10`, copy pose `+0x4d4..→+0x504..`, `00477880()` (mode → `0x25`). timer fired → gen0 `+0x4ec→+0x4ec-1` (200 ms), gen1 `+0x4f0→-1` (200 ms), pending `8`. Else if rod cmd `!= 0` and `+0x414 > 50`: `+0x418 = +0x414 + 20`, `+0x41c = 004ac620(+0x420, +0x418)`, same two gens, pending `8` |
| `7` | drill prep anim | gen2 `+0x504→+0x504-55` (600 ms, mode `0x1d`), gen3 `+0x508→+0x508+15` (420 ms, mode `0x1d`), gen4 `0→100` (500 ms, mode 8) | gen4 fired → `004adad0(); 004c1370()` (hole lookup/create). gen fired → kill 5, gen0 `0 → +0x418-20` over `004873c0()` ms mode `0xc`, pending `6` |
| `8` | stand up | — | on gen fired: kill, pending `5`, gen0 `+0x4f0→+0x4a8`, gen1 `+0x4f8→+0x4b0`, gen2 `+0x4fc→+0x4b4`, gen3 `+0x4ec→+0x4a4`, all `WalkMs(players, double=true)` mode `0xb`; "nyky" `0→100` 500 ms → `+0x24c` |
| `9` | retract short | — | on gen fired: kill 3, pending `10`, copy pose `+0x4d4..→+0x504..` |
| `10` | retract line | entry (see §1.2): gen2/line sway, gen3 `+0x414→0` over `(ticks78-1)*200` mode 8, "line_x"/"line_y" 10 ms-quantified pose timers | bit **2** → kill, gen0 `+0x414 → +0x418-20` over `004873c0()` mode `0xc`, `00477310()`, pending `6`. countdown `7bcc78--` per 200 ms fire; at 0 → `+0x414 = 0`, pending `0xb` |
| `0xb` | line end | `00487330()->vft+0x14`, `00488880()` | on gen fired: pending `= 00488060()` (1 or `0x1e`), `00477220()` (mode → `0x27`) |
| `0xc` | fish force prep | — | on 4 gen timers fired: kill 4, pending `0xd`, `00479af0()`, copy pose, `+0x3f8 = 0`, `+0x3f0 = 0` |
| `0xd` | **fish fight / reel-in mini-game** | see §2 | see §2 |
| `0xe` | landed fish | rod vft+0xc | on gen fired: pending `= 00488060()`, `00477220()`, `00473320(+0x2e8 record, …)` (landed fish, weight HUD `"%s %dg"`), `0049b9c0(3, 1950 or 3900)`, pending `0x20` |
| `0xf` | line lift anim | gen2/line_x `+0x534→+0x534+rand(200)-100` (3300 ms), line_y `+0x538→-400` (3300 ms), "aalto" `0→1000` → `+0x238` | on gen fired: bait consume/settle (`0058f760/0058e5d0`…), pending `= 00488060()` |
| `0x10` | retract after catch | `+0x414` timer: `7bcc74 = max(1, (int)(+0x414*0.028))`, gen3 `+0x414→0` over `(7bcc74-1)*200` mode 8, line_x/line_y 500 ms | countdown `7bcc74--` per 200 ms; at 0 → `+0x414 = 0`, pending `0xf` |
| `0x11` | line-out variant | — | cmd `00490060()` 0/3 → pending `2` |
| `0x12` | depth ease | gen3 `+0x414 → +0x418` (dur: `+0x414 < +0x418` ? `004873c0()*1.3` mode `0xc` : `(int)(2.0 - diff*0.04)*200*1.5` mode 8); gen4 `+0x424 → +0x424 + diff*-7.6394` same dur | cmd 1..4 → kill 5, pending `2`. rod 3/4 → pending `0x13` (`+0x5c8 = 0/1`) |
| `0x13` | depth ease 2 | if `+0x5c8==0`: goal `= (+0x23c == -1) ? 50 : +0x414`; else goal `= +0x420 - 5`. If `|goal - +0x414| < 20` → pending `2`. Else gen2 `200→0` (200 ms) → `7bccac`, gen3 `+0x414→+0x418` (mode 8), gen4 `+0x424` sway | rod 3/4 restart waves (`7bccac + 200` capped 400); on gens fired → pending `2` |
| `0x14` | line out waiting | — | `00591680()` (net/bite event): 1/6 → pending `0x15` + HUD `0049b9c0(0, 7000 or 10000)`; 2 → pending `0x1a` or `= 00488060()` + HUD `0049b9c0(2, 2500)`; 5 → `00488060()`. bits `0x2002`/8 → `00488060()`/`0x14`; bit 0x10 → `0x16`; bit 0x20 → `0x19` |
| `0x15` | bite! | — | on `0048ff90() && 00497440()`: strike path (`00427660(5)`, `00491f20/00491f50`, lure ops) → `00488060()` |
| `0x16` | strike | — | `00416340()` == 1 → `00416350()` + `00479700(this, 0,0,0,0, rodCmd)` (50 % bite), HUD `0049b9c0(1, 4000)`, pending `0x18`; == 2 → `00488060()`. bits `0x2002`/0x10 → `00488060()`; bit 8 → `0x14` (or hook) |
| `0x17` | strike 2 | — | on `00497440()`: settle, `00488060()` |
| `0x18` | landed confirm | — | on `0048ff90() && 00497440()`: lure consistency check, `+0x394 = 0`, `00488060()` |
| `0x19` | at-hole wait | — | `00416340()` 1 → HUD `0049b9c0(2, 2500)`, pending `0x1a`; 2 → `00488060()`. bits `0x2002`/0x20/0x10/8 edges like `0x14` |
| `0x1a` | settle 2 | — | on `00497440()`: `00493320` etc., `00488060()` |
| `0x1b` | settle 3 | — | on `00497440()`: `0047b870`, `00488060()` |
| `0x1c` | HUD rect drag | — | mouse rect logic (resolution-sized rects), → `00488060()` |
| `0x1d` | serial rod analog | — | `00433100/00433200/00432eb0` analog value → `vft+0x18` scaled `*0.1` clamped ±30; bit 0x4000 holds, else `00488060()` |
| `0x1e` | at-hole pose | if pending==2 skip | bit 2 → drill prep (same as state 1, plus `00487330()->vft+0x14`, `0058f560`, `00483cc0`, `00477310()`); bit 0x40 → pending `0x1c`; bit 0x4000 → pending `0x1d`; bit 0x80 (latched `7bcc7c`) → pending `0x17` + HUD `0049b9c0(1, 4000)`; bits 0x10/8/0x20 edges as state 1 |
| `0x1f` | at-hole pose 2 | *(inferred)* mirror of `0x1e` | (case exists in the `004876d0` switch; controller case covered by `0x1e` block) |
| `0x20` | catch hold | — | bit 2 → HUD `0x586`. on `00497440()`: `0048ff30()` false → `+0x504+200`, gen1 `+0x4f0→+1` (1200 ms), gen2 `+0x504→+0x4d4` (1000 ms), gen3 `+0x508+150→+0x4d8+900` (1000 ms), gen4 `0→100` (900 ms), pending `0x21`; true → `00488060()` |
| `0x21` | catch hold 2 | — | gen sway (`-55`, `+15`); gen4 fired → `004adad0/004c1370`; on gens fired → copy pose `+0x4d4..→+0x504..` (`+0x508 - (0x8de or 0x8c0)`), pending `= 00488060()` |
| `0x22` | verdict wait | — | on two gens fired: `+0x33c == 1` → hole create + pending `0xe`; `== 2` → `00473280()` + HUD `0x5a7` + pending `0x10`; else → `004841a0(); 004732d0()` + pending `10` |
| `0x23` | auto-bite fight | HUD `0x558`/`0x9f3030`, `+0x3e0 = 0` | on gen fired: kill 3, pending `0x10` (copy pose) |

State `0x24` is written externally at `00492661` (inside `00492580`); states
`3`, `0x1f` and the remaining writers live in helpers not yet decompiled
(`00482e3f` writes `0x1e` inside `00482ab0`). **open**.

### 1.2 Notable entry details *(verified)*

State 1 bit-2 drill prep (also state `0x1e` bit 2):

```
+0x504 += 200
gen0: +0x4ec      -> +0x4ec+1   800 ms  mode 0xb
gen1: +0x4f0      -> +0x4f0+1   800 ms  mode 0xb
gen2: +0x504      -> +0x4d4     600 ms  mode 8
gen3: +0x508+150  -> +0x4d8     600 ms  mode 9
gen4: 0           -> 100        500 ms  mode 8
pending = 7; 00477310(); 00483cc0()   (0x1e adds 00487330()->vft+0x14 etc.)
```

State 10 entry (retract):

```
7bcc78 = (int)(+0x414 * 0.04 + 3.0)          // tick counter
"line_x": +0x534 -> target   (+0x414*10) ms  mode 0xb
"line_y": +0x538 -> target   (+0x414*10) ms  mode 0xb
gen3: +0x414 -> 0 over (7bcc78-1)*200 ms     mode 8
```

---

## 2. The `0xd` mini-game (fish fight / reel-in) *(verified code, semantics (inferred))*

Called the "drill mini-game" in earlier notes; the code proves it is the
**fish fight**: the timer created on the prep path is named `kala_force`
("fish force"), the run's duration uses the bite record's weight
(`session+0x2e8`, double at `+0x2f8`), the type check `+0x2f0 == 3` reads the
record's species id, the rolls use the record's catch/fail probabilities
(`+0x310` / `+0x308`, backed up at `+0x320` / `+0x318`), and the exits call
`00473280` (catch), `004732d0` (escape) and `00473320` (landed fish). The
hole-drill chain of §3 is a different flow (states `7/6/8`). Both share the
line-depth fields `+0x414/+0x418/+0x41c/+0x420`:

* `+0x414` = current line/tension counter (driven by gen0/gen3 ease timers;
  fed to audio as `004328f0(+0x414 * 0.01)` every frame).
* `+0x418` = target = `+0x414 + 20` *(verified formula; meaning "one drop
  step" is inferred)*, set in state 6 on a rod command while `+0x414 > 50`.
* `+0x41c` = bucket {0,1,2} from `004ac620(+0x420, +0x418)`.
* `+0x420` = hole depth (bottom) at the current hole.

**What the player does:** nothing is required to progress — the run is fully
timer driven. The only inputs consumed in `0xd` are action bits **0x400** and
**0x800** (from `004876d0`: hold menu command 5/6 — mapped from the spacebar
VK `0x20` path too — for 3 s through the `forceRise` timer `0->100/3000 ms`):

* `0x400` (held < 3 s): HUD message `0x59e` only.
* `0x800` (held >= 3 s): HUD `0x59f`, kill gen2/gen3/line_x/line_y, free the
  depth array, **restore** `+0x308/+0x310` from `+0x318/+0x320`, set the
  `forced` flag and re-run the init block with `forced` (duration
  `power*0.75` instead of the weight formula).

### 2.1 Init block (state entry, or after a `0x800` force)

```
stepMs   = (rand(4) + 13) * 200                  // 0079916c  -> 2600..3200 ms
power    = max(+0x414, 0x46)                     // 7bcca0    (min 70)
ms       = forced ? power * 0.75
                  : (w * 0.5 + 0.8) * power + w * 70.0    // w = +0x2f8 double
ticks    = (int)(ms * 0.04 + 0.5)                // 7bcc90   (0.04 = 1/25)
stepTicks= stepMs / 200                          // 13..16
downStep = (int)(stepTicks * -25.0)              // 006db200 = -25.0
upStep   = (int)(stepTicks * 25.0)               // 006db138 =  25.0
count    = ticks / stepTicks + 1                 // 7bcc98   (array length)
array    = calloc(count, 4)                      // session+0x5b8
array[0] = +0x414
writeIdx = 1                                     // 7bcca4
dir      = 0                                     // 7bcca8
```

Depth array build — for `t = 0 .. ticks` inclusive, whenever
`t / stepTicks >= writeIdx`:

```
lo     = max((int)(power - t*25.0), 0)
up     = (int)((ticks - t) * 25.0);  up = min(up, +0x420)   // clamp to hole depth
up     = max(up, lo)
prev   = array[writeIdx-1]
candHi = min(prev + upStep, up)
lo     = max(lo, prev + downStep)
target = (t == ticks) ? 0 : (int)((double)(ticks - t) / ticks * power)
width  = candHi - lo
best   = 0
for k = 0..2:                        // 3 candidates, keep closest to target
    cand = lo + rand(width)
    best = (k == 0 || |target-cand| < |target-best|) ? cand : best
    if (writeIdx < count-1 && best < 0x3c) best = 0x3c      // min 60
array[writeIdx] = best; writeIdx++
if (writeIdx == count) break
```

Outcome rolls (both run; the second overwrites `stop`):

```
good = bad = 0; stop = 0                          // 7bcc94/95/9c
if (rand(10000) < (int)(+0x310 * 10000)):         // good-outcome p (double)
    stop = rand(ticks)
    if (+0x2f0 == 3 && weight(00590c10(+0x2e8)) > 1000 && rand(3) < 2):
        stop = ticks                              // big type-3 fish runs to the end
    good = 1
if (rand(10000) < (int)(+0x308 * 10000)):         // bad-outcome p (double)
    stop = rand(ticks); bad = 1
```

### 2.2 Per-frame

```
gen2 tick (slot 0x228, re-armed every fire):   200 ms, +0x4f8 -> rand(500)+pos.x-250
    ticks--; clamp >= 0; if (ticks < stop) ticks = stop

gen3 step (slot 0x22c, fired when not lastDone):
    if (stepIdx + 1 < count):
        lo = array[stepIdx]; hi = array[stepIdx+1]
        turn = (stepIdx+2 < count) ? ((hi > lo) != (array[stepIdx+2] > hi)) : 0
        mode = dir ? 9 + 2*turn : 8 + 2*turn          // {8,9,10,11}
        dir = turn; stepIdx++
        gen3: array[stepIdx-1] -> array[stepIdx] over stepMs, out = +0x414
    else:
        gen3: +0x414 -> 0 over (ticks+1)*200 ms, mode dir?9:8
        lastDone = 1
```

So **`+0x414` advances along the precomputed array**, one eased segment per
`stepMs`, and the final segment eases it back to 0.

### 2.3 End conditions

When `ticks == stop` (countdown reached the rolled stop depth):

```
kill tick timer
if (!stepTimerFired || !0048ff90()):   ticks = 1        // stall one tick
else:
    if (!good):
        if (!bad):
            +0x414 = 0
            if (+0x33c == 0):  hole create (004adad0/004c1370), pending 0xe,
                               line_y +0x508 -> -2184 (1800 ms mode 8),
                               line_x (rand(200)+500 ms, mode 0x1d)
            else:              HUD 0x5a6 + 0x59f, pending 0x22,
                               gen0 0->1000 3000 ms mode 8
        else:  // jam/escape
            004841a0(); 004732d0()   // escape stub
            pending = 10
    else:  // breakthrough/catch
        good = 0; 00473280()         // catch stub
        HUD 0x557 (red)
        pending = 0x10
```

Two ticks before the end (`ticks - stop == 2`): kill 2 timers and start the
"line_x"/"line_y" 500 ms sway timers. On any pending change out of `0xd`:
kill gen3, free `+0x5b8`, `00488a60()`.

**Jam vs breakthrough:** rolled at init — `good` (breakthrough) vs `+0x310`,
`bad` (jam) vs `+0x308`, with `stop = rand(ticks)` deciding how far the run
gets. A big type-3 fish (`+0x2f0 == 3`, weight > 1000 g) gets a 2-in-3 chance
of `stop = ticks` (fight runs to the last segment).

### 2.4 Mode chain question (5→6→0x13→0x18→0x27)

These are **game modes** (the `00464470` move timeline), not controller
states. Verified edges:

* `5 -> 6`: `004764b0` (guards: mode==5, lake present, `+0x4bc == 0`, no hole
  within 300 units of `pos - vel*(-75)`; else returns 4). Records
  `+0x724/+0x728/+0x720`, `+0x4e4 = 2`, `00465d50(6)`. If `+0x4bc == 1` it
  queues mode `0x20` instead.
* `0x13` (hole creation pass, `00480820`): `004aeee0` create, `+0x4c0` id,
  `+0x4c4 = 004ac150`, `004bbd90` open, `+0x4bc = 1`. *(verified code)*
* `00477310` (bore begin): `+0x438 = 1`; queues `0x18` if current mode ∈
  {`0x25`, `0x26`, `0x18`}, **else `0x29`** *(verified)*.
* `0x18 -> 0x27`: `00477220` (if `+0x438 == 2` there are extra guards:
  mode >= 5, not `0x1e/0x1f`, `00475fa0() == 0`), `00465d50(0x27)`.

The links `6 -> 0x13` and `0x13 -> 0x18` are **not written in code** — they
come from the timeline next-table (data; `00467210` "default next"). Marked
*(inferred)*. The code-visible rule for `00477310` from mode `0x13` would be
`0x29`, so the exact timeline chain needs the move script (**open**).

---

## 3. Action bitmask `004876d0` *(verified)*

| bit | value | source |
|---|---|---|
| 1\|2 | `3` | raw key VK `0x20` (space) from the event queue, or menu commands 5/6 |
| 1 | `1` | `+0x5c0 == 1` (AI) every frame; `+0x5c0 == 2` net case |
| 4 | `4` | `00490060() != 0` and accumulated `+0x53c` past `-(screen*0.0463)` (release/rod path) |
| 8 | `8` | menu command 2 |
| 0x10 | `0x10` | menu command 3 |
| 0x20 | `0x20` | menu command 4 |
| 0x40/0x80 | | HUD mouse-rect buttons in state `0x1e` (rects `0xf7/0x1e0/0x2d0 × 0x6e/0x8f/0xd6` by resolution) |
| 0x100 | | menu command 9 |
| 0x200 | | menu command 8 |
| 0x400 | | command 5/6 held < 3 s (`forceRise` timer created `0->100/3000 ms` mode 8) |
| 0x800 | | command 5/6 held >= 3 s (timer fired, latch `7bcd44`) |
| 0x1000 | | auto-bite: every >= 1000 ms while `session+0x428 > 0` (or motion drift), `p = 00590a40(...).out`, `rand(100) < p` |
| 0x2000 | | menu command 1, only while controller state ∈ {`0x14`, `0x16`, `0x19`} |
| 0x4000 | | external serial rod controller (`00433100/00433200/00432e00`) |

The VK→menu-command map (`00495930() = menuObj+0x28`) is still **open**.

## 4. Jig / line / strike

Two command sources feed the controller:

* `00490060()` = pointer/rod command 0..4: 1 = hover rect, 2 = click inside
  radius, 3/4 = serial rod down/up. Cached in `7bccb0`. In state 2: 1/3 →
  state 4, 2/4 → state 0x11 *(verified)*.
* `00432920()` = key/wiimote/stick command 0..4: 1 = line in, 2 = line out,
  3 = jig down, 4 = jig up (from menu commands, `DAT_007c6924` stick sign,
  wiimote, serial axis `004330c0`). In state 2: 1 → `004acc40` (bucket -1),
  2 → `004acbf0` (bucket +1), 3/4 → state 0x13 with `+0x5c8 = 0/1` *(verified)*.

Line-depth bucket ladder (`+0x41c`):

```
004ac620(holeDepth, target):        // initial bucket
    holeDepth <= 130               -> 2
    holeDepth <= 250               -> ((holeDepth-50)/2 + 50) <= target ? 2 : 0
    else: target <= 135            -> 0
          (holeDepth*2)/3 <= target ? 2 : 1
004acc40(holeDepth, bucket):  // reel in:  131..250: 2->0; >=251: 1->0, 2->1
004acbf0(holeDepth, bucket):  // let out:  131..250: 0->2; >=251: 0->1, 1->2
004abf20(holeDepth, bucket):  // depth for bucket
    holeDepth <= 130: (int)(d*0.9)
    131..250: b==0 ? (int)(d*0.2+30) : (int)(d*0.95)
    >=251:    b==0 ? (int)(d*0.1+40) : b==1 ? (int)(d*0.55) : (int)(d*0.95)
```

Bite attempt `00479700(session, rec, a, b, c, d)` *(verified)* — guards
`+0x52c`, `+0x528`, `+0x640`, hole `+0x348` valid, then `+0x348 = -1`:

* `rec == 0` (Ambient timer, strike state 0x16, `00473280/004732d0` restart):
  pass iff `rand(1000) <= 500`.
* `rec != 0` (`00473320` and record-driven callers): thresholds by
  `00590c10(rec)` = **the record's obfuscated weight field** (grams; the
  "activity" name in earlier notes is a mislabel): `<500 -> 125`,
  `500..999 -> 250`, `1000..1699 -> 500`, `>=1700 -> always`. Pass iff
  `rand(1000) <= thr`. *(verified)*

On success: filter the `+0x338` list via `00475c70`, `pick = rand(count)`,
consume `pick+0x6c`, attach the fish to the hole (`004c1390`), kill the
"Ambient" timer, return 1.

Auto-bite percent `00590a40(lureType, x, &p)` *(verified table)*:

| lureType | factor | out percent p |
|---|---|---|
| 10 | x<5: 1.6; x<9: 1.3; else 1.0 | x>20: 80; >18: 70; >17: 60; >15: 50; >13: 40; >12: 30; >9: 20; else 0 |
| 20 | 1.17 | 0 |
| 30 | x<3: 0.3; x<4: 0.5; x<5: 0.7; x<7: 0.8; else 1.0 | 0 |
| other | x<5: 0.05; x<8: 0.2; x<10: 0.4; else 1.0 | 0 |

`x = 004487e0(0058ea90(00474970(session+0x1bc)))` = the int at `+0x230` of
the active-lure object *(verified chain; "player count" label in earlier notes
is (inferred))*.

Human fight loop *(verified)*: strike state `0x16` resolves immediately with
the 50 % bite roll; catch/escape resolution for a human is **not** the AI's
`AI_korjaus` timer (`(14-skill)*1000` is AI-only, `00478210`) — the human
resolves through `00473280` (catch) / `004732d0` (escape) from states `0xd`
and `0x22`, and `00473320` (landed fish) from state `0xe`, with the `0x22`
"verdict wait" 3000 ms gen0 timer covering the delayed `+0x33c` result.
`forceRise` = 3000 ms hold (§2). "Nosto" (`00476c00`, fishing modes
`0x18/0x24/0x25/0x26/0x27/0x28`): `00650fe0("Nosto", 0, 1000,
00487560(+0x340), 0, 8)` — the **duration is `00487560(+0x340)`**, `1000` is
the ramp end value (correcting the "Nosto 1000 ms" note).

## 5. Walk / stand timers *(verified)*

```
WalkMs(players, double):            // 004875f0
    ms = players < 8 ? (0x17 - players) * 0x19 : 400     // (23-players)*25
    return double ? ms*2 : ms
```

State 4 creates exactly three gens: gen0 `+0x4f0 -> +0x4a8`, gen1
`+0x4f8 -> +0x4b0`, gen2 `+0x4fc -> +0x4b4`, all `WalkMs(players)` (no
doubling) mode `0xb`. State 8 (stand-up) creates four: gen0 `+0x4f0->+0x4a8`,
gen1 `+0x4f8->+0x4b0`, gen2 `+0x4fc->+0x4b4`, gen3 `+0x4ec->+0x4a4`, all
`WalkMs(players, true)` (doubled) mode `0xb`, plus "nyky" `0->100` 500 ms into
`+0x24c`. `players` comes from the chain
`004487e0(0058ea90(00474970(session+0x1bc)))` *(verified chain; player-count
semantics (inferred))*.

Depth-ease duration `004873c0()` *(verified)*:

```
diff = |+0x418 - +0x414|
ramp:  x<1 -> -12.5 (x forced 1); x==1 -> -12.5; x==2 -> -10.0;
       x 3..8 -> -10.0 + (x-2)*0.83333; x 9..15 -> -5.0 + (x-8)*0.35714;
       x>=16 -> -2.5            (x = same +0x230 value)
scale: type 10 && x<6 -> 0.5 + (x-1)*0.125
       type 30 && x<8 -> 3.0 - (x-1)*0.33333
       type 40 && x<11 -> 5.0 - (x-1)*0.44444
       else 1.0
return 500 - (int)(diff * ramp * scale)      // ramp is negative
```

## 6. Constants (VA: value)

| value | VA | use |
|---|---|---|
| 0.04 | `006db0c8` | ms -> 200 ms ticks (1/25); `+0x414*0.04` counters |
| 0.028 | `006db0c0` | state 0x10 retract tick count |
| 25.0 / -25.0 | `006db138` / `006db200` | per-25 ms depth step up/down |
| 70.0 | `006db150` | `+ w*70` in the fight duration; min power 0x46 = 70 |
| 0.8 / 0.5 | `006d0580` / `00777878` | `(w*0.5 + 0.8) * power` |
| 0.75 | `006d1630` | forced-run duration `power*0.75` |
| 3.0 | `006d9fc0` | `+3` tick bias (state 10 entry) |
| 10000.0 | `006d6b90` | probability scale |
| 1.3 / 0.028 | `006db108` / `006db0c0` | state 0x12/0x13 ease durations |
| 1.5 / 2.0 | `006d1638` / `007778a8` | state 0x12 ease duration parts |
| -7.639437277140305 / 7.639437277140305 | `006db1e8` / `006db130` | gen4 sway per diff |
| 0.78 | `006db100` | state 0x13 gen4 scale |
| 0.1 | `006d12d0` | serial analog scale; `004abf20` deep bucket 0 |
| 30.0 / -30.0 | `007778d8` / `006db208` | analog clamp; `004abf20` mid bucket 0 add |
| 40.0 / 0.55 / 0.95 / 0.9 | `006dc690`/`006dc638`/`006dc660`/`006d9fa8` | `004abf20` |
| 0.01 | `006d0550` | audio feed `+0x414*0.01` |
| -12.5..-2.5 | `006db1f8/1f0/1e0/1d8` | `004873c0` ramp |
| -0.83333 / -0.35714 | `006db1c8` / `006db1c0` | `004873c0` ramp steps |
| 0.125 / 0.33333 / 0.44444 | `00777868`/`006d0570`/`006db0e8` | `004873c0` scales |
| 5.0 | `006db128` | `004873c0` type-40 base |
| 1.6 / 1.3 / 1.17 / 0.3 / 0.5 / 0.7 / 0.8 / 0.05 / 0.2 / 0.4 / 1.0 | `006d6b78`/`006db108`/`0073f500`/`006d9f70`/`00777878`/`006db0f8`/`006d0580`/`006d1618`/`006d0568`/`006d9f88`/`00777888` | `00590a40` table |
| 214013 / 2531011 | `00676d2c` | MSVC rand |
| 2^-30 | `0073fd80` | `rand(max)` scale (`0059db20`) |
| 0x0E5FAF5F | `00590c10/00590e70` | weight-field XOR key |
| 125 / 250 / 500 | `00479700` | bite thresholds on the record weight |
| 500 / 1700 | `00479700` | threshold band edges |
| 60 (0x3c) | `0048af00` array build | min depth value until the last entry |
| 200 ms | `0048af00` | fight tick period |

---

## 7. Integration notes for the orchestrator

`src/controller.hpp` (namespace `ctrl`) ports: the state model (state/prev/
pending + epilogue rule), the action-bit edges of §1.1, the `0xd` machine of §2
verbatim (same constants, same RNG call order), the walk timers of §5, the
bucket ladder, `00590a40`, `00479700`'s roll, and the mode edges of §2.4.
External predicates/effects (HUD `0049b9c0`, net msgs, `00488060`,
`00490060/00432920` input queries, `0048ff90`, `+0x33c` verdict) are exposed as
plain fields/callbacks on `ctrl::Session` — defaults reproduce the observed
single-player behavior.

Proposed call ordering per slice (mirrors `0048af00`'s own order):

```
game::Update(sliceMs):
  1. controller  = sess.Controller()          // +0x5b0 input device present?
  2. bits        = ctrl::Actions(...)          // port of 004876d0 (host side)
  3. sess.Step(bits, sliceMs, rng)             // 0048af00: state switch,
                                               // timer tick, epilogue
  4. mode/visual pass (00480820) then HUD      // existing render/audio wiring
```

`ctrl::Session::Step` must be called once per slice (<= 1000 ms, see
`docs/sim.md` §6.1) with the same action bits the original computes, and a
`ctrl::MsvcRand` seeded once per game. `game.hpp`/`main.cpp` should replace the
current key-WASD and instant-catch paths with: bit translation (space ->
bits 1|2, configured command keys -> 8/0x10/0x20, hold -> 0x400/0x800),
`ctrl::Session` driving the player, and the mode chain of §2.4 feeding
`00480820`-style visuals. `0x414/0x418/0x41c/0x420` are the single source of
truth for line depth; render them from `sess.depth/target/bucket/holeDepth`.
