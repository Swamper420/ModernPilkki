# Per-screen fades + PLAY input / UI draw (state-open)

Decompiled with the headless recipe in `docs/decomp-notes.md` (project
`/tmp/opencode/ghidra-state`, `extracted/ProPilkki2.exe`). All addresses are
VAs in the original PE. Scratch C is not committed (clean-room).

Scope of this pass:

- Ported the per-screen fade machine 1:1 (`src/state.hpp`, tests in
  `src/test_state.cpp`).
- Documented `0063ca80` (PLAY input) and `00638dd0` (in-game UI draw), which
  were decompiled-but-unported in `docs/decomp-notes.md`.

## 1. Correction: the "screen-class cluster" is not the fade code

`docs/decomp-notes.md` "Screen vtable dispatch map" guessed the cluster
`006489c0 / 00648d80 / 00648f60 / 00649370 / 00649540 / 00649a30` were the
screen vtable methods. **They are not.** They are the 2D immediate-mode draw
helpers: every one of them calls `DAT_007c8b88` (the ID3D11DeviceContext)
vtable slots `+0x1c,+0x20,+0x24,+0x28,+0x2c,+0x30,+0x34,+0x40,+0x44,+0x48,
+0x4c,+0x50,+0x60,+0xc0,+0xac` to set state and draw. `00649540` is the quad
core reached from `0040ab00` (rect workhorse) — see §4.

The real per-screen virtual methods are the `State` / `MENU_*` vtables, dumped
below.

## 2. Screen fade system (006304e0 driver + 00651620 timer)

### 2.1 Address -> role

| addr | role |
|---|---|
| 0074af50 | `State::vftable` (base): `[0]`=00433f00 `ret 0`, `[4]`=006306f0, `[8]`=006307c0, `[c]`=00630700, `[10]`=00433f00 |
| 006304e0 | screen phase driver (see 2.3) |
| 00630120 | screen ctor with fades: `(id, fadeInMs, fadeOutMs)`; also sets `State::vftable` before the registry overrides it with the `MENU_*` vtable |
| 00630070 | screen ctor, no fades (`(id)`; all fade/link fields zeroed) |
| 00630070/00630120 callers | 005c8930 registry (88 slots) |
| 005c8930 | builds all 88 screens + appends to the driver list |
| 006301c0 | driver object ctor (`DAT_007c3f38`, 0x74 B): +0 current id (-1), +4 active screen, +8/+0xc lists |
| 00630380 | append a screen to the driver list |
| 0058aaf0 | list lookup by id (thiscall; `0058b510` walks links) |
| 005d86e0 | set viewport + run 006304e0 + return `*DAT_007c3f38` (current id) |
| 006306d0 | `screen+0x58 = 1` = skip-in flag |
| 006306e0 | `screen+0x5c = 1` = skip-out flag |
| 00630710 | `fade permille` getter: `(double)*(int*)(screen+0x48) * 0.001` (DAT_006d0548 = `0.001`) |
| 00630740 | event/consume helper for base screens (used by 006307c0) |
| 00630790 | exit request: log `Go to state: N`, `screen+0x44 = N`, `screen+0x30 = 3` |
| 006307c0 | base frame (+8): consumes an event, else frees it |
| 006306f0 | base fade-in tick (+4): just calls frame (+8) |
| 00630700 | base fade-out tick (+c): `+0x4c=+0x48`, then frame |
| 00650fe0 | timer create (locks the timer manager, calls 00650ee0) |
| 00650ee0 | timer entry alloc: `rate = 1.0/duration`, `target+0 = start` |
| 00651620 | timer update pass (linear ramp; see 2.2) |
| 00651220 | timer `done?` = entry state == 2 |
| 00651410 | timer kill/free |
| 00651860 | curve evaluator (mode 8 -> selector 0, linear) |
| 00630450 | debug log (only when driver +0x10 flag set) |

Screen object field map (`piVar1` in 006304e0), all verified:

| off | field |
|---|---|
| +0x00 | vtable (base or `MENU_*`) |
| +0x30 | phase: 0 enter, 1 fade-in, 2 frame, 3 fade-out |
| +0x34 / +0x38 | fade-in / fade-out duration ms (0 = skip that fade) |
| +0x3c / +0x40 | `Fade_in` / `Fade_out` timer handles |
| +0x44 | next screen id (enter resets to -1, exit reads it) |
| +0x48 | fade permille 0..1000 (timer writes here) |
| +0x4c | copy handed to the fade tick (+0x4c = +0x48, only on tick paths) |
| +0x58 / +0x5c | skip-in / skip-out flags |

### 2.2 Fade curve (VERIFIED)

Every fade timer is created as `00650fe0("Fade_in"/"Fade_out", 0, 1000,
durationMs, &screen[0x48], 8)`:

- start = 0, end = 1000, mode = 8.
- `00650ee0` stores `rate = 1.0 / durationMs` (DAT_00777888 = `1.0`).
- The update pass `00651620` computes `t = elapsed_since_creation * rate` and
  `00651860(start,end,t,mode)`. `mode & 7 == 8 & 7 == 0` selects the **linear**
  case: `value = 1000 * clamp(t, 0, 1)`.
- Done when `t >= 1.0` (`00777888`). Curve selectors 1..6 (`00777878` = 0.5,
  `00752ae8` = pi, `006db0f8` = 0.7) are ease/sine/ping-pong and are **not**
  used by fades. `0x1000`-style extras are not present in the fade mode.

So the fade is a **linear permille ramp 0..1000 over `durationMs`**, not eased.

### 2.3 Driver order (006304e0, VERIFIED)

```
if current id == -1: return
do:
  s = driver.active
  switch s.phase:
    0/1:
      if phase == 0: vtable[0](enter); s.nextId = -1; s.phase = 1
      if s.fadeInMs == 0:  s.phase = 2; vtable[8](frame); return   # skip-when-0
      if s[0x3c] == 0:     00650fe0("Fade_in",0,1000,s.fadeInMs,&s[0x48],8)
                           s[0x4c] = 0; s[0x4c] = s[0x48]; vtable[4](); return
      if !done(s[0x3c]) && s[0x58] == 0: s[0x4c] = s[0x48]; vtable[4](); return
      if s[0x4c] == 1000: s[0x58]=0; kill(s[0x3c]); s[0x3c]=0; s.phase=2;
                          vtable[8](frame); return
      s[0x48]=1000; s[0x4c]=1000; vtable[4](); return               # skip snap
    2: vtable[8](frame); return
    3:
      if s.fadeOutMs != 0:
        if s[0x40] == 0:  00650fe0("Fade_out",0,1000,s.fadeOutMs,&s[0x48],8)
                          s[0x4c]=0; s[0x4c]=s[0x48]; vtable[0xc](); return
        if !done(s[0x40]) && s[0x5c] == 0: s[0x4c]=s[0x48]; vtable[0xc](); return
        if s[0x4c] != 1000: s[0x48]=1000; s[0x4c]=1000; vtable[0xc](); return
        s[0x5c]=0; kill(s[0x40]); s[0x40]=0
      vtable[0x10](exit); s.phase = 0
      nextId = s.nextId; driver.active = 0058aaf0(nextId); current = nextId
while true   # loops back: runs the NEW screen's enter in the SAME call
```

Key points, all 1:1 in `state::ScreenStep` / `state::ScreenRun`:

- `enter` is vtable `+0`, `fade-in tick` `+4`, `frame` `+8`, `fade-out tick`
  `+0xc`, `exit` `+0x10`.
- **skip-when-0**: a duration of 0 skips that fade entirely (fade-in 0 still
  runs frame in the same call; fade-out 0 goes straight to exit).
- **006306d0** (`+0x58=1`) makes the in-progress fade-in snap to 1000: one tick
  with `0x48/0x4c = 1000`, then the next driver call kills the timer and enters
  phase 2. **006306e0** (`+0x5c=1`) is the fade-out mirror.
- The `+0x4c` copy means the "finish" test lags the timer by one call — after
  the timer completes, the driver first ticks with 1000, then finishes.
- Exit + next `enter` happen inside one `006304e0` invocation (do-while), so
  the new screen's `frame` runs on the *next* call.

### 2.4 Per-screen fade durations (005c8930, VERIFIED)

`00630120(0, inMs, outMs)` / `00630070(0)` (no fades). Executable mirror:
`state::ScreenFadeDur(id)`. Two ids in the old `docs/decomp-notes.md` table
were off by one (SETTINGS/QUESTION and the RECORDS..IMPORT_EXPORT block); this
is the corrected full registry:

| id | screen | in | out |
|---|---|---|---|
| 0 | LOAD_ADDON_PACKAGE | 700 | 0 |
| 1 | START_SCREEN | 700 | 0 |
| 2 | MAIN | 700 | 0 |
| 3 | SINGLE_PLAYER | 500 | 0 |
| 4 | SINGLE_GAME | 0 | 0 |
| 5 | PRACTICE | 0 | 0 |
| 6 | TEAM_CAPTAIN_GAME | 500 | 0 |
| 7 | PASSIVE_ROD_NET_GAME_INFO | 500 | 0 |
| 8 | PASSIVE_ROD_START | 500 | 0 |
| 9 | PASSIVE_ROD_LAKE | 500 | 0 |
| 0xa | PASSIVE_ROD_DESTINATION | 500 | 0 |
| 0xb | SELECT_PLAYER | 500 | 0 |
| 0xc | SELECT_LAKE | 500 | 0 |
| 0xd | SELECT_GAME_TYPE | 0 | 0 |
| 0xe | LOADING_LAKE | 500 | 0 |
| 0xf | END_SCREEN | 0 | 1800 |
| 0x10 | PAUSE | 0 | 0 |
| 0x11 | RACE_RUNNING_TO_END | 0 | 0 |
| 0x12 | SAVING_FLAG_RODS | 0 | 0 |
| 0x13 | RESTORING_FLAG_RODS | 0 | 0 |
| 0x14 | QUIT | 0 | 0 |
| 0x15 | PLAY | 0 | 0 |
| 0x16 | RESULTS_START | 0 | 500 |
| 0x17 | RESULTS_TOTAL | 0 | 0 |
| 0x18 | RESULTS_PLAYER | 500 | 0 |
| 0x19 | RESULTS_OWN_RECORDS | 0 | 0 |
| 0x1a | RESULTS_OWN_PRIZES | 0 | 0 |
| 0x1b | RESULTS_CUP_FINNISHED | 500 | 0 |
| 0x1c | RESULTS_CUP_PRIZES | 0 | 0 |
| 0x1d | LAKE_INFO | 500 | 0 |
| 0x1e | LAKE_INFO_DEPTH | 500 | 0 |
| 0x1f | anon ctor 005e7bc0 | 500 | 0 |
| 0x20..0x27 | anon ctors 005e7b30/b00/b60/b90/c20/bf0/c50/c80 | 0 | 0 |
| 0x28 | SETTINGS | 500 | 0 |
| 0x29 | QUESTION | 0 | 0 |
| 0x2a | anon 005aec80 | 500 | 0 |
| 0x2b..0x2e | anon 005aec50/ec20/eda0/ed10 | 0 | 0 |
| 0x2f | anon 005aece0 | 500 | 0 |
| 0x30 | anon 005aed40 | 0 | 0 |
| 0x31 | anon 005aecb0 | 0 | 0 |
| 0x32 | anon 005aed70 | 500 | 0 |
| 0x33 | SELECT_START_PLACE | 0 | 0 |
| 0x34 | MULTI_PLAYER | 500 | 0 |
| 0x35 | CHECK_LAKES | 500 | 0 |
| 0x36 | SEND_BUG_REPORT | 500 | 0 |
| 0x37..0x40 | anon 005d99d0/9880/9940/98b0/9910/9850/9970/99a0/9a00/98e0 | 500 | 0 |
| 0x41..0x43 | anon 005d9ac0/9a90/9a30 | 0 | 0 |
| 0x44 | anon 005d9a60 | 500 | 0 |
| 0x45 | SUPPORT_GAME | 500 | 0 |
| 0x46 | PRELOADING_LAKES | 500 | 0 |
| 0x47..0x4d | anon 005a0150/0120/00f0/01e0/01b0/0180 + 005b5640 | 500 | 0 |
| 0x4e | RECORDS_CLEARED | 500 | 0 |
| 0x4f | NET_RECORDS_INFO | 500 | 0 |
| 0x50 | LICENSE_AGREEMENT | 500 | 0 |
| 0x51 | INFO_TEXT_WITH_LINK | 500 | 0 |
| 0x52 | IMPORT_EXPORT | 500 | 0 |
| 0x53..0x57 | anon 005a96c0/96f0/9780/9720/9750 | 500 | 0 |

88 slots (0..0x57), matching the `for (iVar3 = 0x58; …)` zero-fill.

### 2.5 What the permille value drives

- Converted to a `0.0..1.0` double by `00630710` (`* 0.001`, DAT_006d0548).
- **MENU_MAIN (VERIFIED)**: its custom fade-in tick `005c7cc0` does
  `f = 00630710(screen); DAT_007c40dc->vtable[+4](f); DAT_007c40dc->vtable[+8](...)`
  — i.e. the permille sets the draw alpha/blend of the `menu.pcx` image
  (`DAT_007c40dc` is the loaded menu image object from `0064d6b0`). If the
  image is missing it calls `006306d0` (skip-in) and draws the base UI.
- The base fade ticks (`006306f0`/`00630700`) only delegate to the screen's
  `frame`; each screen's frame decides how to apply `+0x48`.
- **UNVERIFIED**: whether every screen applies it as a full-screen alpha. No
  evidence found for a shared full-screen quad/letterbox fade
  (`006440b0` letterbox is aspect-only, not fade-driven). Per-screen
  application is the model; only MENU_MAIN was confirmed.

## 3. PLAY input `0063ca80` (state 20, one queued event per frame)

Called from the 00640370 state-20 block. Signature `(char param_1, float
param_2)`. It first reads one event from the queue `0049e310` /
`0049e2c0` (re-queue via `0049e320`), then handles command/click paths.

Event dispatch (VERIFIED):

| event | action |
|---|---|
| 0x21/0x22/0x23 (PgUp/PgDn/End) | `0047ae90(0/1/2, 0, 0)` rod/lure slot release (gated `DAT_007c849c`) |
| 0x31/0x32/0x33 ('1'/'2'/'3') | `0047ae90(0/1/2, 1, 0)` rod/lure slot press |
| 0x33 also released at 0x33 case; note slot-2 press is case 0x33 | |
| 0x409 / 0x40a (wheel ±) | camera zoom `DAT_007a6b7c ± 100`, clamped to `[-0x1194|-3000 … -0063c980()]`, snapshots `007a6b80` |
| 0x45f | scoreboard/notes toggle path (`00499bb0`, `004c2b70`) |
| 0x460 | cycle player (`00438050/00437cb0`, `00643a10`) |
| default | `0049e320(event)` re-queue (other screens consume) |

Then the command path: `FUN_00495930()` returns a menu command id
(`menuObj+0x28`, the 00495930/00495920 layer) and the switch handles commands
`0,1,5,6,7,8,9,10`. Commands `0/5/6` = primary/cancel/fish-action cluster,
`7` timers/scoreboard (`004ab9e0/004317b0`, `0061edd0/006228f0`), `8/9` team
switches, `10` pause/net. Commands `2/3/4` are the movement/line keys
(consumed by `004876d0`), commands 5/6 map to action 3 (per `docs/sim.md`
§3.3). `00495920()` returns the command used by the click path.

Mouse left press (`DAT_007c6915`, speed gates `007a6b70/007a6b74`, slow-drag
`00606ad0` vs `006d1670` = 15.0) picks `004b9f20` → object `004aeee0/004af1b0`
→ walk order `0047f530` (min distance 1500 = `00799154`) or hole action; sets
the `7c8794` fishing-target latch. Mode/replay end path: `007c896c` 2000 ms
hold (`00650fe0(&0074c150,0,2000,0x8fc,&7c895c,0xb)`) → `00483890(2,0)` →
`00632880()` → `DAT_007c6904 = 0x15` (Results). Esc is **not** handled here —
it is handled one level up in the 00640370 state-20 block (`7c8790 = 0x14`,
pending 4), matching the ported `ui::Press` Play branch.

VK -> command map: **UNVERIFIED / unported.** `docs/sim.md` §3.3 marks it open;
it lives in the menu/input layer (`00416150`, `00432920`, `0058bc80`) feeding
`00495930`, which was not dumped this pass. `docs/sim-open.md` does not exist.

Port status: `ui.hpp` already models the menu-level actions (Title/LakeSelect/
Play/Pause/Results, Esc pause via `state::Pause`). No new input was ported this
pass; the gameplay key handling stays in `src/main.cpp` + `game.hpp`.

## 4. In-game UI draw `00638dd0` (state 20/21 frame)

`00638dd0(int param_1, int param_2)` is the PLAY-screen frame. Sequence
(VERIFIED calls, roles inferred from neighbors):

1. `00643e00`, `004bc6a0` — per-frame scene prep; `DAT_007c849c =
   00438a90()` (session), then per-player `00473a40` + `004109b0`.
2. Net player pass `005895e0/004382a0` over `0058aaf0` ids; `004bd2b0/
   004bd2f0` (names/tags).
3. `00644220`, `00416b20` — pre-draw setup; camera path `0063c790`,
   `004168c0` (viewport/scroll from `DAT_007a6b7c` zoom).
4. Entity render loop `00466f40` per player; `00416ad0`, `004bc9e0`,
   `00460ef0`, `004bd330`, `004bc590`.
5. `004a5d00(0x10, DAT_007c8948[…], …, pcVar7=&00639640, …)` — HUD/marker
   layer (when `DAT_007c89b0 != 0`, `pcVar7 = 00639640`).
6. Action/controller pass: `0048af00` (action bitmask machine), `00494eb0`,
   `00638a70`, `0062ead0(text x,y)` text/sprite flush (gated `7a6b51` and
   `00652bc0` clear), `006388f0` tail.
7. Screenshot/title branch at `param_1 == 1 && (DAT_0077130 & 0x800)`:
   `DAT_007c8960->vtable[+8]`, network/record paths.

Cross-references for the rect layer:

- `00652940` = **UI solid-rect fill + clear**: if `obj+0x890 != 0` it runs
  `0064ff60(0,0,w)` (viewport), `00650300(0)`, `006505c0(2)`,
  `0064fdf0(7,1)`, `0064fdf0(8,1)`, `0064a5f0` (ClearRenderTargetView), then
  `0040ab00(x1=+8, y1=+0xc, x2=+8+0x14, y2=+0xc+0x10, color565, 0,
  0.5f, 1.0f)`, then `006521c0(0,1)`. Color bytes are `+0x38`(R)/`+0x39`(G)/
  `+0x3a`(B), packed 5-6-5.
- `0040ab00` = **2D rect workhorse**: unpacks RGB565
  (`r=(c>>8)&0xf8, g=(c>>3)&0xfc, b=(c<<3)&0xff`, each `*DAT_006d0524` = 1/255)
  and calls `00649540` twice (the quad core, D3D device context at
  `DAT_007c8b88`).

So the "UI rect drawing" is `00652940` (fill wrapper) → `0040ab00` (RGB565
unpack) → `00649540` (D3D quad). No fade is applied in this path.

## 5. Ported / changed

- `src/state.hpp`: added `state::FadeTimer`, `state::Screen`,
  `state::FadeUpdate` (00651620), `state::ScreenStep` (006304e0 for one
  screen), `state::ScreenRun` (0058aaf0 driver loop), `state::ScreenFadeDur`
  (005c8930 durations).
- `src/test_state.cpp`: assert checks for the linear curve, fade-in order +
  the `+0x4c` snap, skip-when-0, 006306d0 skip, fade-out with/without
  duration, single-call exit+enter, and duration-table spot checks.
- Build/test: `cmake -S src -B /tmp/opencode/build-state`, `cmake --build`,
  test binaries run directly (`test_state`, `test_ui`, `test_tick`, ... pass;
  `ctest` finds no tests — pre-existing, no `enable_testing()`).
  Pre-existing failures unrelated to this change: `test_bots`, `test_game`.

## 6. Open / next

- **Wiring (orchestrator, `src/main.cpp`)**: call `state::FadeUpdate(screen,
  delta)` in the update stage and `state::ScreenRun(...)` in the render stage,
  with the active screen looked up by the mode-driver id (currently
  `ui::Game::state` / `state::Driver`). `ui::Game` has no per-screen `Screen`
  objects yet; add one per `ui::State` with `FadeDur` from `ScreenFadeDur`.
  `src/main.cpp` and `src/game.hpp` were left untouched per instructions.
- Per-screen fade *application* (alpha vs overlay) is per-screen and only
  MENU_MAIN was verified; the rest is **UNVERIFIED**.
- VK -> command map (`00495930` feeding layer) still **UNVERIFIED**.
- The 0063ca80 gameplay actions (slots, zoom, pick/walk) are documented but
  not ported here (see `docs/sim.md` §3 for the gameplay mapping already in
  `game.hpp`).
