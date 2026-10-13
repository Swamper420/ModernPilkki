# Round structure — move timeline, modes, round end (clean-room)

Target: `extracted/ProPilkki2.exe` (x86). VAs are image addresses
(`00467210` = `0x00467210`). Sources: Ghidra headless dumps in
`/tmp/opencode/sim-dec/`, raw `objdump -d` disassembly, `tools/peconst.py`
for `.rdata` constants, and the already-ported `.3ds` text format
(`docs/file-formats.md`, `tools/model3ds.py`, `src/model3ds.hpp`).
Verified claims cite code; guesses are marked *(inferred)*.

---

## 1. Move script: layout and data source (verified)

### 1.1 Who builds the script object

Session `+0x5d4` points at a **timeline object** (the `00467210` player).
Its constructor `004634c0(this, arg, byte)` allocates three blocks
(`0066e39f` = malloc, sizes `0xe10`, `0xe10`, and the script):

* `this+0` / `this+4` = two aux rings (ctors `00406950(5000)` / `00406950(100)`),
* `this+0x1a8` = **script**, a `0x53aa4`-byte heap block initialized by
  `00463630` (defaults: `moveCount = 0`, every chain flag `= 1`),
* `this+0x1ac = -1` (current move), `this+0x1b0 = 0` (pending),
  `this+0x1a0 = 0` (track count), `this+0x1a4 = 0` (elapsed),
  `this+0x1ec` = ctor byte arg.

Moves are appended with `00463b30` → **`00463b50(script, moveIdx, def, size)`**
("AddMove", verified from its asm):

| check / store | meaning |
|---|---|
| `moveIdx <= 0x45` | max 69 ⇒ **70 moves** |
| `def+0x10 → script+move*4` | next-move table |
| `def+0x14 → script+0x118+move*4` | length (int, ms) |
| `def+0x18 → script+0x53a44+move` (setne) | chain flag (byte) |
| `def+0x1c → script+0x52aec+move*4` | speed scale (f32) |
| `def+0x20 → script+0x52c04+move*4` | position scale (f32) |
| `def+0x24 …` | per-track byte array (≤ 40 B) → `script+0x52f54+move*40+i` |
| then keyframes | `A×B` records of 8 B (4 × i16: t, v0, v1, v2) |
| trailing name | `min(len,30)` bytes → `script+0x522b0+move*30`, NUL at ≤ 29 |

Resulting script struct (all offsets from `this+0x1a8`):

```
+0x00000  u32  next[70]              next-move table
+0x00118  i32  len[70]               length ms
+0x00228  kf[70][40][15]             8 B keyframes (i16 t, x, y, z), 15/track
+0x522b0  char name[70][30]          move names
+0x52aec  f32  speedScale[70]        duration = (int)(len * speedScale)
+0x52c04  f32  posScale[70]          keyframe value scale
+0x52f50  i32  moveCount             (00464410/00465d50 bound check)
+0x52f54  u8   trackFlag[70][40]
+0x53a44  u8   chainFlag[70]         1 = keep hopping (00467210 break test)
```

Correction to `docs/sim.md` §1: the name table is **`+0x522b0 + move*30`**
(`00464470` reads `(move*15 + 0x29158)*2`, the decompiler's factored form of
`0x522b0 + move*30`), not `+0x29158+move*30`. Move count is at `+0x52f50`.

### 1.2 Where the data comes from (verified)

The move definitions are the **`<STARTMOVES>` sections of the `.3ds`
model-assembly scripts** (already parsed 111/111 by `tools/model3ds.py`
/ `src/model3ds.hpp`, `docs/file-formats.md`):

```
<STARTMOVES>
<FUNCTION=9=KAIRAAMINEN1>
<NEXT=9>
<LENGTH=1000>
<DISABLE=21,15,18,19,17,20>
<PART=idx=name>
(time,x,y,z) rows ...
```

Mapping onto the AddMove def struct *(field-level mapping inferred; every
value class is present in the file)*: `<NEXT>` → `+0x10` next,
`<LENGTH>` → `+0x14`, `<DISABLE>` list → the `+0x24` byte array (every list
is ≤ 10 entries, cap 40), `<PART>` keyframe rows → the 8-byte records,
`<FUNCTION=name>` → the name. `<NEXT=a,b>` supports two targets (no shipped
file uses the comma); the second target / chain flag `+0x18` source is
**open** — the reset default is 1, so the port defaults `chain = true`.

Delivery path: the model loader pushes moves through an ADD_MOVE message
whose handler `0043e940` is installed at connection vtable slot `+0x448`
(asm `movl $0x43e940, 0x448(%esi)`); the handler looks up the session,
takes `session+0x5d4`, and calls `00463b30(timeline, moveIdx, def, size)`
(verified asm at `0x43e9d6..0x43e9df`). The loader→message half is
*(inferred)*; it is irrelevant for the port (the port reads the same
`.3ds` text directly).

**The session model is `gfx/3ds/human2.3ds`** (57 moves, ids 0..56):
`0047f740` reads HUD flag `mode == 0x35` (= 53 = `KYYKYSSA`, "squatting"),
which exists only in `human2.3ds`/`human2_m.3ds`; `human.3ds` tops out at
id 51. `human2_m.3ds` is the same table minus the squat moves (53 moves).
Speed scale is 1.0 for shipped moves *(inferred)*: no per-move scale field
exists in the `.3ds` text; `def+0x1c/+0x20` presumably come from
`<STARTSCALING>` (6 files, still unported) and default 1.0.

### 1.3 Duration formula (verified)

```
duration(m) = (int)( (float)len[m] * speedScale[m] )      // 00464510
```

Identical code in `004644d0`, `00467210`. With scale 1.0 the duration is
just `<LENGTH>`.

## 2. Mode table — `human2.3ds` `STARTMOVES` (verified file data)

Mode id = `<FUNCTION>` id. `dur` = `<LENGTH>` ms (scale 1.0), `next` =
`<NEXT>`. Self-next = hold. Full table (game-code roles from decompilation
cited in §3):

| id | name | dur | next | game role |
|---|---|---|---|---|
| 0 | KAVELY1 | 500 | 1 | walk cycle (K = 0.34) |
| 1 | KAVELY2 | 500 | 0 | walk cycle; no-lure fallback |
| 2 | KAVELY_ALOITA | 250 | 0 | walk start (00476c00 queue) |
| 3 | PYSAHTYMINEN_VASEN | 700 | 5 | stop left (HUD progress modes 3/4) |
| 4 | PYSAHTYMINEN_OIKEA | 700 | 5 | stop right |
| 5 | PAIKALLAAN | 100 | 5 | standing still — drill start allowed (004764b0) |
| 6 | KAIRAUSASENTOON1 | 250 | 7 | drill stance (004764b0 → mode 6) |
| 7 | KAIRAUSASENTOON2 | 550 | 8 | |
| 8 | KAIRAUSASENTOON3 | 200 | 9 | |
| 9 | KAIRAAMINEN1 | 1000 | 9 | drill chain stage 1 (§4) |
| 10 | KAIRAAMINEN1TO2 | 1000 | 11 | chain stage 2 |
| 11 | KAIRAAMINEN2 | 1000 | 11 | hold |
| 12 | KAIRAAMINEN2TO3 | 1000 | 13 | chain stage 3 (mode 0xc) |
| 13 | KAIRAAMINEN3 | 1000 | 13 | hold |
| 14 | KAIRAAMINEN3TO4 | 1000 | 15 | chain stage 4 (0xe) |
| 15 | KAIRAAMINEN4 | 1000 | 15 | hold |
| 16 | KAIRAAMINEN4TO5 | 1000 | 17 | chain stage 5 (0x10) |
| 17 | KAIRAAMINEN5 | 1000 | 17 | hold |
| 18 | LAPI_MENI | 350 | 19 | "it went through" — drill outcome (0x12) |
| 19 | KAANNY_JATTAAKSEEN | 450 | 20 | 0x13: hole creation pass (00480820) |
| 20 | JATA_JA_KAANNY_TAKAISIN | 2500 | 21 | 0x14: handle teardown (+0x344) |
| 21 | REPPU_POIS | 500 | 22 | 0x15: backpack off, `+0x640 = 1` |
| 22 | REPPU_MAAHAN | 1000 | 23 | 0x16 |
| 23 | ISTU | 1000 | 24 | 0x17: sit down |
| 24 | JUST_SITTING | 200 | 24 | 0x18: **sitting/fishing at hole** (00477310 → 0x18) |
| 25 | NOUSE_YLOS_1 | 1000 | 26 | 0x19 |
| 26 | NOUSE_YLOS_2 | 500 | 27 | 0x1a |
| 27 | REPPU_SELKAAN | 500 | 28 | 0x1b |
| 28 | KAIRA_KATEEN | 500 | 29 | 0x1c |
| 29 | KAIRA_OLALLE | 500 | 5 | 0x1d |
| 30 | JUOKSU1 | 400 | 31 | 0x1e: run (K = 0.725 fast step) |
| 31 | JUOKSU2 | 400 | 30 | 0x1f: run |
| 32 | SUORAAN_JATTOASENTOON1 | 250 | 33 | 0x20 |
| 33 | SUORAAN_JATTOASENTOON2 | 550 | 34 | 0x21 |
| 34 | SUORAAN_JATTOASENTOON3 | 400 | 20 | 0x22 |
| 35 | PUUHA_STAND_KATSELU | 3000 | 5 | 0x23 idle fidget |
| 36 | KOKEILU | 500 | 24 | 0x24: "trying" — round-end mode (00476180) |
| 37 | NOSTO1 | 500 | 38 | 0x25: lift/reel (00477880 → 0x25) |
| 38 | NOSTO2 | 500 | 37 | 0x26: lift |
| 39 | KORJAILU_ASENTOON | 500 | 40 | 0x27: mending stance (00477220 → 0x27) |
| 40 | KORJAILEE | 500 | 40 | 0x28: mending — round-end mode |
| 41 | PILKKIASENTOON | 500 | 24 | 0x29 jigging stance |
| 42 | PUUHA_STAND_LAKKI | 3000 | 5 | 0x2a |
| 43 | PUUHA_JALAT | 3000 | 5 | 0x2b |
| 44 | PUUHA_STAND_KATSELU2 | 3000 | 5 | 0x2c |
| 45 | NOUSE_YLOS_KORJAILUSTA | 1000 | 26 | 0x2d |
| 46 | SUORAAN_SELKAAN1 | 200 | 47 | 0x2e |
| 47 | SUORAAN_SELKAAN2 | 550 | 48 | 0x2f |
| 48 | SUORAAN_SELKAAN3 | 250 | 5 | 0x30 |
| 49 | LAPI_MENI_JATKA | 350 | 50 | 0x31: drill outcome "through, continue" |
| 50 | LAPI_MENI_JATKA_2 | 450 | 46 | 0x32 |
| 51 | POHJAAN_MENI_JATKA | 2000 | 50 | 0x33: drill outcome "hit bottom" |
| 52 | KYYKKYYN | 1000 | 53 | 0x34 squat down |
| 53 | KYYKYSSA | 100 | 53 | 0x35 squatting (HUD flag 0047f740) |
| 54 | KYYKYSTA_YLOS | 1000 | 5 | 0x36 |
| 55 | KYYKYSSA_PUUHA1 | 10000 | 53 | 0x37 |
| 56 | DUMMY_DISABLE | 1000 | 56 | 0x38 |

**Correction to `docs/sim.md` §4.6 and `src/game.hpp`:** the chain
`9 → 10 → 0xc → 0xe → 0x10 → {0x12|0x31|0x33}` is **not a pre-round
countdown** — it is the auger **drilling progression** (`KAIRAAMINEN1..5`
drill stages → breakthrough/bottom outcomes). The topology quoted in
sim.md is right; the label is wrong. Mode 0x18 = `JUST_SITTING` is the
fishing pose, not "bore begin"; 0x25/0x26 = `NOSTO` (fish lift), 0x27/0x28
= `KORJAILU` (tackle mending), 0x1e/0x1f = `JUOKSU` (running — hence the
0.725 step constant).

## 3. Timeline player `00467210` (verified, clock half)

`this` fields: `+0x1a0` track count, `+0x1a4` elapsed-in-move,
`+0x1a8` script, `+0x1ac` current move, `+0x1b0` pending move,
`+0x58..` target pointers (keyframe outputs), `+0x1ec` flag byte.

Per call `00467210(this, dt)` (keyframe evaluation omitted here — see §6):

1. If `script == 0` return −1. If `dt > 0`: `elapsed += dt`.
2. If `cur == -1`: `elapsed = 0`; if `pending != -1` then `cur = pending`
   (+ hooks `00467f10`/`004638f0`); `pending = -1`. If `cur` still −1 return −1.
3. Hop loop (`hops < 5`):
   `dur = duration(cur)`; while `elapsed >= dur`:
   `elapsed -= dur; hops++`;
   if `pending == -1`: `next = *(u32*)(script + cur*4)`;
   if `next != cur && next != -1`: `cur = next`, hook, **if chainFlag[cur]
   == 0 break** (one transition per call for non-chained moves);
   else (pending set): `cur = pending`, hook, `pending = -1` (no break —
   keeps hopping while `hops < 5`).
4. Clamp for evaluation: if `elapsed >= duration(cur)`:
   `carry = elapsed − dur + 1; elapsed = dur − 1`; … evaluate …;
   `elapsed += carry`. (Net zero on the clock; only the evaluated outputs
   see the clamp.)
5. Keyframe evaluation (verified shape, not ported): per track ≤ 40,
   15 keyframes `script+0x228+((track + cur*40)*15 + i)*8`, find last
   `t` with `elapsed ≥ (int)t * speedScale`, lerp to the next keyframe,
   values × `posScale`, written as float triples to `*(target+0x1b0)`.
   Two extra "result" tracks beyond the track count feed `+0x1b4..0x1c8`
   (one with `+ DAT_0077782c` bias); applied via `00467c30` / `00467f60`
   *(the exact 2-track split is inferred)*.

Query/queue API (all verified):

| VA | semantics |
|---|---|
| `00464470(this, buf)` | current move; with `buf != 0` copies the 30-byte-stride name (NUL-terminated) |
| `004644d0(this)` | elapsed clamped to `duration(cur) − 1` (no `cur == −1` guard — callers must not call it when idle) |
| `00464510(this, m)` | `(int)((float)len[m] * speedScale[m])` |
| `00464600(this)` | pending move (`+0x1b0`) |
| `00465d50(this, m)` | queue pending; bounds `0 ≤ m < moveCount`, else −1 |
| `00464410(this, m)` | queue pending **and** `cur = -1` (restart current move at 0 on next tick) |
| `00463aa0(this)` | `elapsed %= duration(cur)` (wrap) |

Transitions only happen inside `00467210` (timeline pass `00467e80` under
CS `7c69a8`, once per frame with the wall-clock delta). A queued pending
mode therefore takes effect at the **next move-duration boundary** (or
immediately when `cur == -1`), never mid-move — except `00464410`, which
forces `cur = -1` and so applies on the next tick.

## 4. Drill/round chain `00480820` (verified)

The chain state lives on the **session**, not the timeline object:
`+0x450` stage counter, `+0x470` stage stamp (game clock `DAT_007c695c`),
`+0x46c` per-stage hold ms. Each frame for modes `9..0x11`:

| mode(s) | stage rule | on `now − stamp ≥ +0x46c` |
|---|---|---|
| 9 | `<1 → 1`, stamp now | stage 2, `00465d50(10)` |
| 10, 0xb | `<3 → 3` | 4, `00465d50(0xc)` |
| 0xc, 0xd | `<5 → 5` | 6, `00465d50(0xe)` |
| 0xe, 0xf | `<7 → 7` | 8, `00465d50(0x10)` |
| 0x10, 0x11 | `<9 → 9` | outcome (below), stage 10 |

Outcome selection (also entered when `+0x4e4 != 2`, i.e. not the plain
drill-start path which sets `+0x4e4 = 2` in `004764b0`):

```
if (+0x4e8 == 0)  +0x6c0 == 0 ? queue 0x12 (LAPI_MENI)   : queue 0x31 (LAPI_MENI_JATKA)
else              queue 0x33 (POHJAAN_MENI_JATKA)
```

(`+0x4e8` set by the drill controller per §4.2 of sim.md; `+0x6c0` is the
netplay "alt" byte, client path only.)

HUD/side data in the same function (constants verified with peconst):
scale `dVar11 = 30.0 / duration` (`DAT_007778d8 = 30.0`) for modes
`10..0x11`; the `+0x468` gauge ramps from `230.0` (`006da010`) downward,
sub-terms `×3.0` (`006d9fc0`) for 0xf and `×4.0` (`006d1640`) for 0x11,
`006da048 = 4000`, `006da040 = 2000`, `006d9fe0 = 28`, `007778c8 = 10`,
`006da018 = 270` (modes 0x12/0x31 rise-back over 200 ms; mode 0x33
switches hole handle types after 1500 ms). Presentation only.

**Hold value `session+0x46c` (`0047a1c0`, verified):**
`v = 0042bd00()` (drill skill/level, *(exact getter semantics inferred)*,
clamped ≥ 0):

```
hold = (v > 100 ? 100000 : v * 1000);  if (hold > 0) hold += 200;
```

so `v = 0` → 0 ms (instant stages), `v = 1` → 1200 ms, `v ≥ 100` →
100200 ms. Since each chain move's duration is 1000 ms and the pending
queue fires at the duration boundary (§3), the effective stage time is
`max(1000, hold)` — the hold is a *minimum dwell*, the move duration is
the *transition gate*.

**Round time limit (user-selected minutes, comp setup):** converted with
`min(minutes, 60) * 60000` ms into a `00650fe0` timer (`00443c42..0x443c4e`,
`imul $0xea60` = minutes × 60000; the `+0x8` mode = repeating timer, the
uncapped `minutes − min(minutes,60)` tail feeds the `from` argument).
Full owner of that timer and its expiry path is *(inferred)* — the
in-game status row `Kilpailuaikaa jäljellä:` (string id 0x94) displays it.
**Open:** exact expiry → round-end wiring (see §5; the end-mode rising-edge
gate below is the only verified round-end trigger).

## 5. Round end and pause (verified)

Mode-set checks (all just `00464470(0)` tests):

| VA | true when mode ∈ | meaning |
|---|---|---|
| `00476180` | `{0x18, 0x24, 0x27, 0x28}` | fishing-idle: sitting / trying / mending stance / mending |
| `00476160` | `{0x25, 0x26}` | lifting the fish (NOSTO) |
| `004761d0` | `{0x15, 0x16, 0x17}` | backpack-off / sit-down transition |

State-20 handler (`0063ca80`, end of frame):

* `endMode = 00476180() || 00476160()` (guarded by local client
  `DAT_007c849c != 0`); `bVar13` = rising edge of `endMode`
  (`DAT_007c8a30` keeps the previous value).
* **End timer creation**: when `DAT_007c896c == 0` and
  `(((client == 0 || 004761d0() == 0) && !bVar13) || local_2c > 0)` is
  false — i.e. `(client && 004761d0()) || bVar13`, and
  `local_2c = 00495b10(0) ≤ 0` — then:
  `uVar5 = 2000; if (DAT_007c897c || DAT_007c8980) uVar5 = 0;`
  `DAT_007c896c = 00650fe0(&DAT_0074c150, 0, uVar5, 0x8fc, &DAT_007c895c, 0xb)`
  — a **2300 ms** timer (`0x8fc`) ramping `7c895c` 0 → 2000 with easing
  `0xb`. (docs/sim.md called this "2000 ms hold"; the hold is 2300 ms, 2000
  is the ramp target.)
* **Fire path**: `00651220(7c896c)` fired && `client != 0` &&
  `004761d0() == 0` && `004749b0() == 0` (session `+0x4d4 == 0`, no pending
  action) → kill timer (`00651410`), `7c896c = 0`, **`00483890(2, 0)`**,
  then **`00632880()`**; if it approves → `DAT_007c6904 = 0x15`
  (**Results** state 21).
* **Immediate path** (command case 0/5/6): `00476180() && 00474aa0()`
  (session `+0x698` "arrived") `&& !004749b0()` → if `7c896c != 0` do
  nothing (wait for the timer path), else `00632880()` → Results.
  Command case 1 with the same predicate: `7c896c == 0` → Results direct.
* Bare rising edge with no client: `bVar13 && 7c896c == 0` →
  `00483890(2, 0)` only.

Helpers (verified):

* `00632880()`: `00438290()` (byte `match+0x50`) false → approve; else
  approve iff `00495b10(0) < 1` (nothing pending).
* `00483890(session, 2)` (case 2/3): freezes the session — copies walk /
  hole / reel fields into `+0x4ec..+0x518`, zeroes `+0x3f0/+0x3f8`,
  `+0x534/+0x538`, resets drill targets `+0x414 = 0`,
  `+0x418 = 004abf20(+0x420, +0x41c)`, `+0x41c = 004ac620(...)`;
  case 2 only: `+0x23c = −1`, `+0x240 = 0`, `+0x3fc = 1000`,
  `+0x3b8 = +0x3c0 = 00488060()`, `+0x3bc = −1`, `+0x394 = 0`; finally
  starts the `"Syoko"` timer (`00650fe0("Syoko", 0, 1000, 1000, +0x244, 8)`)
  when `+0x204 == 0`.

**Pause** (verified): Esc (`0x1b`) in state 20 → `DAT_007c8790 = 0x14`
(game state) and pending `DAT_007c6904 = 4` (session hub); `MENU_PAUSE`
screen (id `0x10`) is entered via input action 3 (`005cfca0` case 3 loads
`gfx\pic/menu/pause.pcx`, `7c4448 = 0x3f`). Resume: state 4 polls the
screen driver (`005d86e0`); screen `0x15` (PLAY) → pending = `7c8790`
(the Esc-stashed `0x14`); any other screen → state 10.

**Round reset `00436a30(match, param_2, param_3)` (verified):** for every
entry of the bag/player list `match+0x6c` (count `**(int**)(match+0x6c)`):
zero `+0xb4, +0xb8, +0x94, +0x98, +4, +8, +0xc`. With `param_2 != 0` each
entry gets `+0xc0 = 1` inside the loop and all entries are set
`+0xc0 = 0` again in the tail pass (net effect: 0). `param_3 != 0` is the
"full" reset: rebuilds teams (`PlayerTeam` `0xd8`-byte objects with
name copy, `00437320/004371f0/00436e80/00437000/00437430/00437530/
004376c0`) and runs vtable hooks `+0xc/+0x10/+0x18/+0x24` per entry.
`00438a00(client)` (score readback) = `(double)*(client+0x150)`.

## 6. Verified vs inferred / open

Verified: script struct layout and AddMove (`00463b50`), `.3ds`
`STARTMOVES` as the data source (file + ported parser), duration formula,
the whole `00467210` clock/hop logic, query API, chain machine
(`00480820`) and hold formula (`0047a1c0`), mode-set checks, end-timer
sequence, `00483890(2,0)` field copies, `00436a30` reset fields, pause
flow, peconst values (30.0 / 3.0 / 4.0 / 230.0 / 270.0 …).

*(inferred)*: AddMove def ↔ `.3ds` field mapping (values all present,
exact plumbing untraced), speed/pos scale 1.0 for shipped moves, second
`<NEXT>` target = chain-flag source, `0042bd00()` = drill skill getter.

Open: the exact owner/expiry wiring of the minutes-timer from §4 (the
verified round-end trigger is the end-mode rising edge; how "time is up"
forces it is not traced); keyframe evaluation details (`00467c30`/
`00467f60` targets); `<STARTSCALING>` (6 files); netplay alt-byte paths
(`+0x6c0`, `DAT_007c84bc` branches).

## 7. Integration notes (what should drive what)

* `src/timeline.hpp` (this slice) is the authority for: durations,
  mode transitions (next-table + pending override + restart), the
  mode-query API, the drill-chain stage machine and its hold formula,
  and the round-end mode sets. Tests: `src/test_timeline.cpp`.
* `src/game.hpp`: `CountdownNext` implements the chain as a *pre-round
  countdown* (delta 15) — that label is wrong (§2). Replace its use with
  `timeline::Chain::Step` driven from the drill start (`004764b0`
  resets `+0x450 = 0`, stamps `+0x470`, sets `+0x4e4 = 2`, `+0x4e8 = 0`,
  mode → 6). The chain is `9..0x10` per-stage dwells, not a match-start
  countdown. Keep `kMode*` enum ids — the ids are right, only the names
  should follow §2 (e.g. `kModeBore = 0x18` is `JUST_SITTING`).
* `game::kEndHoldMs = 2000` is the wrong number: the end timer is
  **2300 ms** (`0x8fc`) ramping `7c895c` to 2000 (`00650fe0(..., 0, 2000,
  0x8fc, &7c895c, 0xb)`). Port `kEndTimerMs = 2300` and treat 2000 as the
  ramp-to value (fade), per §5.
* `src/ui.hpp` `ui::Game` (Play countdown → Results) should stop owning a
  countdown at all. Drive it from the §5 sequence: on the end-mode rising
  edge (`00476180() || 00476160()`, per local session) start the 2300 ms
  end hold; when it expires and `+0x4d4 == 0`, run the `00483890(2, 0)`
  session freeze, ask `00632880()`'s port ("nothing pending"), then
  `ui::To(g, ui::State::Results)`. The immediate command path (end mode
  && arrived && idle && no timer) may jump straight to Results.
* `src/main.cpp`'s `endHoldMs` accumulates toward `kEndHoldMs`; rewire it
  to the timeline end-timer above (2300 ms, gated on the mode set) instead
  of an unconditional countdown.
