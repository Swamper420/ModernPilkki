# Decomp notes

Ghidra 12.1.2 project: `/tmp/opencode/ghidra-proj` (outside git).
Target `extracted/ProPilkki2.exe` (x86, VS2022, 15080 functions). DWARF-free,
MSVC + `__security_check_cookie` everywhere.

## Anchors (verified via decompiler)

| addr | role |
|---|---|
| 0066f076 | PE entry (CRT startup) |
| 005f55c0 | **WinMain**: single-instance mutexes `{ProPilkki2-0000/0001}`, `RegisterClassExA("MainWClass")`, WndProc `005f3820`, `PeekMessageA` pump, per-frame tick `00605eb0`, `PostQuitMessage` exit |
| 00605070 | engine init: `Game version: v2.0.7`, critical sections, subsystem allocs |
| 004b6d70 | **.ves lake loader** (called from `004b18e0`); reads INFOBLOCK[/V2], DEPTH_TABLE (+defaults), TYPEMAP/DEPTHMAP (+`type.dat`/`depth.dat` cache write), OBJECT_POSITIONS, STRINGINFO_FI/EN, GAMETYPES… |
| 00430770 | **section reader**: `<TAG> u32 len payload u32 hash`; skips unknown sections by `len+4`; mode from `00430d10` (1 = legacy lakefile, 2 = v002); status 0 = hash OK, 5 = mismatch-but-usable, 1/2/3/4 = fail |
| 00430d10 | file sniffer: `Pro Pilkki lakefile` → 1, `Pro Pilkki datafile v002` → 2 |
| 00430ee0 / 00430f30 | section cache writer (`fwrite`) / `free` wrapper |
| 0066139d | `D3D11CreateDeviceAndSwapChain` IAT thunk |
| 0064ad60 | **D3D11 renderer init**: tries driver types [1,5,2], feature levels
  11_1→9_1 (retry w/o 11_1 on E_INVALIDARG), logs `Selected D3D_FEATURE_LEVEL_*`
  via `00431970` (log fn), creates swapchain + RTV. Called from `0064ebb0` |
| 0049e3c0 | key bitmap set/clear (`DAT_007bcd5c`, bit = keycode & 0x1f) |
| 005f3820 | WndProc: input router + `Reset DX` on display change, save+exit on EndSession |

## Frame-tick stage bodies (00605eb0 callees, decompiled)

Update side:

| addr | role |
|---|---|
| 0059ae70 | **elapsed** `now-prev`; on wrap `(now-prev)-1` (uint huge, callers clamp). ~100 callers |
| 0062e230 | sprite **anim timer**: gate `elapsed > 39ms`, CS `7c6978`; per entry `+4` period ms, `+8` accumulator, `+0xc` callback; frame step on obj `+0x128` frame / `+0x138` speed / `+0x134` max / `+0x140` ping-pong (reverses speed at ends) |
| 00471810 | game **update slicer**: delta -> `<=999ms` slices, slice widens to `5000ms` while remaining `>60000ms`; each slice: entity update `0047f740` (count `+4`) + vtable+8 object update (count `+8`) |
| 00467e80 | update pass under CS `7c69a8`: per entity `0058ab20(elapsed)` + `00467210(elapsed)` (count `+8`) |
| 00651620 | timer/fade state machine: state `+0x1a` (0/1/2, else `INVALID TIMER STATE`), rate `+0x28`, double math `elapsed * rate` vs `1.0`; flag `0x10` = chain to next index (rewinds it), modes `5/6` pick slot 1, mode `8` keeps entry |
| 00606bd0 | **mouse input**: click edges (latch `7c6910/11`, press `7c6934/38/3c`, release `7c6915/16/17`), press/release pos deltas into 10-slot ring `7c6ac0` (index `7c6ab4`), path length `7c69f8 += hypot(dx,dy)`; returns `(elapsed/10)` clamped to 100 |
| 00658570 | small state machine `+0x890` (0 idle, 1/2 waiting on `00651220`, -> 3/0 on ready + `00651410`), then `00650300(delta)` + `006521c0(1,0)` |

Render side:

| addr | role |
|---|---|
| 006499e0 | one-shot latch: `7c696c = 1` (render-enable) |
| 006440b0 | **letterbox**: bar half-height `(1.0 - local_c) * 0.5`, two white `0040ab00` rect fills + RTV/viewport set (`local_c` = aspect from `0061e400`) |
| 00640370 | **frame governor + game-mode state machine** (720 lines, see below) |
| 0064c920 | mirror of `006499e0`: clears the `7c696c` render latch |
| 00638280 | per-frame text/sprite flush: if `7a6b51` set and `00652bc0()` clear, `0062ead0(cursor x=7c6abc, y=7c6b10)`; ~120 UI callers |
| 00649810 | **Present**: `context +0x1bc` flush, `swapchain->Present(1,0)`, `7c8b0c++` (governor frame count), mouse-delta snapshot; 0 = device lost -> restore chain |
| 0064ebb0 | **device/swapchain re-init** (restore path): mode pick (log `Window size selected: %dx%d.`), device `0064ad60` + RTV/DSV `0064c480` + `0064a790`, texture re-upload `0064f150`, clear + viewport; fail -> `0064e380` teardown |
| 0064c480 | RTV + depth texture + DSV creation, `OMSetRenderTargets` (+0x84) |
| 0064be00 | texture upload/copy: pow2 sizing (cap 0x200), staging path, DXGI formats 0x56/0x55/0x73/0x57 |
| 0064cf10 | swapchain `SetPrivateData` (+0xc) with a mode struct; backbuffer Map/Unmap copy path |
| 0064e380 | device teardown: releases RT/DS views + caches `7c8c14..7c8c28`, `7c8bf4` |

## Screen/scene transition driver (006304e0)

Called from the mode query `005d86e0` (set full viewport, flip, return
screen id `*DAT_007c3f38`) and from entity update `0047f740`. Walks the
active screen object `param_1[1]` through phases at `+0x30`:

| phase | meaning |
|---|---|
| 0 | enter: vtable +0 called once, phase -> 1 |
| 1 | fade-in: timer "Fade_in" (`00650fe0("Fade_in",0,1000,dur,+0x48,8)`), fade value `+0x48/+0x4c` runs 0..1000 permille, vtable +4 each tick; done -> phase 2 |
| 2 | frame: vtable +8 every tick |
| 3 | fade-out: timer "Fade_out" over `+0x38`, vtable +0xc; then vtable +0x10 (exit), next screen = table lookup `0058aaf0(*(obj+0x44))`, phase back to 0 |

`+0x34` = fade-in duration ms, `+0x38` = fade-out duration ms; skips the
fade when duration is 0. Fades reuse the 00651620 timer/fade system
(same "Fade_in"/"Fade_out" names). Port has the mode protocol but not
per-screen fades yet (`src/state.hpp`; `src/ui.hpp` frame/exit blocks
are the hook).

### Frame order in `00605eb0` (verified, incl. Present)

```
update off (7c6a09) -> return 1 (skip); quit (7c689c) -> return 2
clock: 7c695c game-time (= 7c6a0c + offset), 7c6a0c frame base
UPDATE: 62e230(7c695c) 471810(7c6a0c) 467e80(7c6a0c) 651620(7c6a0c)
        651620(raw ms) 606bd0() 658570()
[focus lost (652bc0): zero mouse edge latches 7c6910..7c693c]
render off (7c6a08) -> return 1
RENDER: 6499e0() latch on, 6440b0(0) letterbox, 640370() governor+states,
        652940() UI rects, [652bc0 -> 638280(0) flush], 64c920() latch off,
        649810() Present(1,0)
        Present fail -> log "Restoring DirectX..", 6325c0, 64f0f0, 632640
tail: state 7c6908 == 0x32 -> 6062d0 + 431ae0, return 2 (quit)
```

### 00640370 governor (ported as `tick::Governor`)

Every `>=500ms` window: `fps = (int)(frames * 1000.0 / ms + 0.5)`
(constants `006d0590`=1000.0, `00777878`=0.5, both doubles). Sleep per
frame: mode 0 none, mode 1 adaptive (`fps>=121` -> sleepMs+1 capped 10;
`80<=fps<100` -> sleepMs-1 floor 0; `100..120` hold; `fps<80` -> 0),
mode 2 `Sleep(1)`, mode 3 `Sleep(5)`; mode from vsync probe `0061ec30`,
limiter flag `7c83bc`, sleep `7c83b4`, fps `7c6958`.

### 00640370 game-mode state machine (walked)

State written to `DAT_007c6904` (pending), switch reads `DAT_007c6908`
(active; the deferred copy is applied by `00632560` — protocol
verified, see "7c6904 sub-state internals" below). Phase
`DAT_007c690c` per state: 0 = enter, 1 = per-frame, 2 = exit (states
run the three blocks in that order each pass). Per-frame tail when
phase==1 after the switch: `0049e2b0()` (clears `DAT_007bdd00`), then
if net handle `7c84b8` && `004487b0()` -> state `0x31`. Mode/screen
query is `FUN_005d86e0`.

| state | meaning |
|---|---|
| 0 | boot: sets `7c6904=1`, `7c6898=0` |
| 1 | `0063f160()` (splash) -> 10 |
| 4 | session hub: `005d86e0` result chooses next (0x15 -> `7c8790`; default -> 10); net/lobby calls `004383c0/00439ae0`, `00644220/00644530` |
| 10 (0xa) | **front-end hub**: mode 0x10 -> 4 (start session), 0x14 -> `0x31` (quit path), 0x15 -> `0x14` (start play; see correction below) |
| 11 (0xb) | one-shot: model `006d197c`, loads `gfx\\pic/linja2.pcx` (loading art) |
| 12 (0xc) | create session object (0x9e8 B via `00435fc0`); load progress `00633e00(...) > 99` -> state `0x14` |
| 20 (0x14) | **in-game play**: `0063ca80` input, `00638dd0(1, …)`; Esc (0x1b) or back -> `7c8790=0x14`, state 4 (pause); key `0x45c` -> chat path `0045a540/0045c330`; else key -> `0049e320` |
| 21 (0x15) | alt in-game (replay/net): takes screenshot on enter (`7c878c = 0064a330()`), Esc -> `7c8790=0x15`, state 4; exit frees `7c878c` via `0064d9b0` |
| 22 (0x16) | teardown (`005b6c20`) -> 10 |
| 49/50 (0x31/0x32) | net transition: `00632c10/00600b10` -> 0x32 (idle); 0x50 waits |
| 70 (0x46) / 80 (0x50) | big subsystem states: alloc 0x7ed0 (`0059fd40`) / 0x2d268 (`004c0760`) on enter, free on exit |
| 100 | idle/quit sink |

Port mapping today (`src/ui.hpp`): Title=10, LakeSelect=11/12, Play=20,
Pause=4 (via 7c8790), Results=21. States are wired through the port of
the mode driver (`src/state.hpp`), see "7c6904 sub-state internals"
below.

### 7c6904 sub-state internals + per-screen vtables (decompiled)

**00632560 verified** — exact deferred protocol (was "verify" above):

```
pending(7c6904) == active(7c6908):
    phase 0 -> 1 (enter done), else unchanged
pending != active:
    phase 1 or 0 -> 2 (request exit)
    phase 2      -> phase 0, active = pending   <- the deferred copy
```

00640370's `do{}while(true)` runs the ACTIVE state's block for the
current phase, calls 00632560, and loops until phase==1, then runs the
tail (0049e2b0 = clears `DAT_007bdd00`; net `7c84b8 && 004487b0` ->
pending 0x31) and returns. So a transition executes frame(old),
exit(old), enter(new) inside one 00640370 call; frame(new) runs on the
NEXT call (phase reached 1 at loop exit). Corrections to the state
table above: state 10's screen-id 0x15 branch sets pending **0x14**
(start play), NOT 7c8790 — the `7c8790` resume is state **4**'s 0x15
branch. State 10 has no else (stays); state 4's else -> 10. Also state
0's block runs when phase==0 (it is the boot enter), state 11 allocates
a 0x51c model object (vtable from `DAT_006d197c` -> `7c8770`) plus the
`linja2.pcx` loading art, state 20's Esc branch also latches
`7c8472` via `00418910(&7c84d4)`.

**Screen driver 006304e0 (second, per-screen phase machine)** — field
map of the screen object (`param_1[1]`), all offsets verified:

| off | field |
|---|---|
| +0x00 | vtable: +0 enter, +4 fade-in tick, +8 frame, +0xc fade-out tick, +0x10 exit (thiscall, ECX=screen) |
| +0x30 | phase: 0 enter, 1 fade-in, 2 frame, 3 fade-out |
| +0x34 / +0x38 | fade-in / fade-out duration ms (0 = skip that fade) |
| +0x3c / +0x40 | "Fade_in"/"Fade_out" timer handles (00650fe0, killed via 00651410) |
| +0x44 | next screen id; enter resets it to -1; exit reads it, `0058aaf0` lookup, **loops in the new screen's enter in the same call** (do-while, not return) |
| +0x48 / +0x4c | fade value permille 0..1000 + copy handed to the tick |
| +0x58 / +0x5c | skip-fade flags (`006306d0` sets +0x58=1 -> fade-in snaps to 1000) |

Driver object (`DAT_007c3f38`, 0x74 B, ctor `006301c0`): +0 current id
(-1 = none), +4 active screen object, +8 screen list, +0xc event list
(0x14-B nodes, `0062ffe0` ctor, pushed by `006303c0` = "Input Event.
id:" log), +0x10 debug-log flag. `0058aaf0`/`0058b510` walk the list by
index (+8 = next link, +4 = screen ptr). `005d86e0` = set viewport,
run 006304e0 once, return current id — this is what the 00640370 mode
states poll.

**Screen registry** (`005c8930`, called from splash `0063f160`): 89
slots `DAT_007c3f58..0x7c40b8`, slot index = screen id, then all are
appended to the driver's list (`00630380`). Named vtables + ctor fade
args (`00630120(0, fadeInMs, fadeOutMs)` or `00630070` = no fades):

| id | screen | fade |
|---|---|---|
| 0 | LOAD_ADDON_PACKAGE | in 700 |
| 1 | START_SCREEN | in 700 |
| 2 | MAIN | in 700 |
| 3 | SINGLE_PLAYER | in 500 |
| 4/5 | SINGLE_GAME / PRACTICE | none |
| 6..0xb | TEAM_CAPTAIN_GAME, PASSIVE_ROD_{NET_GAME_INFO,START,LAKE,DESTINATION}, SELECT_PLAYER | in 500 |
| 0xc | SELECT_LAKE | in 500 |
| 0xd | SELECT_GAME_TYPE | none |
| 0xe | LOADING_LAKE | in 500 |
| 0xf | END_SCREEN | out 1800 |
| 0x10 | PAUSE | none |
| 0x11/0x12/0x13 | RACE_RUNNING_TO_END, SAVING_FLAG_RODS, RESTORING_FLAG_RODS | none |
| 0x14 | QUIT | none |
| 0x15 | PLAY | none |
| 0x16 | RESULTS_START | out 500 |
| 0x17 | RESULTS_TOTAL | none |
| 0x18 | RESULTS_PLAYER | in 500 |
| 0x19/0x1a | RESULTS_OWN_RECORDS / PRIZES | none |
| 0x1b | RESULTS_CUP_FINNISHED | in 500 |
| 0x1c | RESULTS_CUP_PRIZES | none |
| 0x1d/0x1e | LAKE_INFO / LAKE_INFO_DEPTH | in 500 |
| 0x29 | SETTINGS | in 500 |
| 0x2a | QUESTION | none |
| 0x34..0x37 | SELECT_START_PLACE, MULTI_PLAYER, CHECK_LAKES, SEND_BUG_REPORT | 35-37 in 500 |
| 0x46/0x47 | SUPPORT_GAME / PRELOADING_LAKES | in 500 |
| 0x4f..0x53 | RECORDS_CLEARED, NET_RECORDS_INFO, LICENSE_AGREEMENT, INFO_TEXT_WITH_LINK, IMPORT_EXPORT | in 500 |
| others | anon ctors `005e7*`, `005ae*`, `005d9*`, `005a0*`, `005a9*` | per-ctor |

Mode-state <-> screen-id contract (how 00640370 uses the poll):
state 10 (front-end): screen 0x10 (PAUSE) -> mode 4, 0x14 (QUIT) ->
0x31 (quit path), 0x15 (PLAY) -> mode 0x14 (start fishing), else stay.
State 4 (pause/session hub): screens {0x10,0x12,0x1d,0x1e,0x28,0x41,
0x42,0x43} keep it open, 0x15 (PLAY) -> pending = 7c8790 (resume the
Esc-stashed 0x14/0x15), else -> 10. Ported as `src/state.hpp`
(pending/active/phase + 00632560 deferred copy + 7c8790 pauseRet),
driven from `ui::Step`/`ui::To` in `src/ui.hpp`; per-screen fades not
yet ported (frame/exit blocks are the hook).

Constants dumped straight from the PE with `tools/peconst.py` (rdata
section); faster than decompiler round-trips for float tables.

Input inventory (from imports): keyboard ONLY via WndProc (no GetAsyncKeyState);
mouse via capture messages; clipboard API; Wiimote detect via HID+SetupDi,
I/O via ReadFile; serial port (SetCommState/Timeouts — hardware controller?);
no joystick API; GDI32 nearly unused (DeleteObject/CreateBitmap only).
| 00431970 | log function (`Game version: v2.0.7`, feature levels, `Resolution change…`) |
| 00605eb0 | **frame tick** (WinMain loop): timer delta, quit flag (`DAT_007c689c`
  → 2), update `0062e230/00471810/00467e80/00651620/00606bd0/00658570`,
  render block `006499e0/006440b0/00640370/00652940/00638280/0064c920`,
  DX-restore check |
| 00632640 | async-load gate (50 ms sleeps), NOT the frame |

Lesson: Ghidra xref queries can miss callers (thunk showed none); a raw
`E8`-CALL byte-scan found both call sites (`64ae91`, `64aed5`). Verify.

## .ves integrity hash (mode 2, cracked, 768/768 verified)

`H = XOR_j ((byte_j ^ 0x5A) << (j mod 24))` (u32). AVX2 in binary is just
vectorization (`0x2AAAAAAB` div-magic computes the mod-24 shifts,
`0x5A5A5A5A` xor key). File trailer = H(whole file minus last 4 bytes).
Mode 1 (legacy): alternating-`NOT` de-obfuscation + byte-sum. See
`tools/ves_hash.py`. Single-byte-diff pairs across lakes pinned `shift = pos`
before generalizing.

## Screen vtable dispatch map (raw xref scan, 2026-10-13)

Callers grouped by virtual-method displacement (from
`/tmp/opencode/render-dec/vt.txt`, scratch). The screen-class cluster:
`006489c0`, `00648d80`, `00648f60`, `00649370`, `00649540`, `00649a30`
(~0x841 B) — all have methods at disp 0x30 (phase), 0x44, 0x48, 0x4c,
0xac. Others: 0x34 -> `00649540/0064ea70`; 0x38 -> `00624ea0/0064a330
(screenshot)/0064cf10 (Map copy)/0064d0c0`; 0xb8 -> `00495c00`. The 3D
scene draw is expected behind one of these screen vtables (PLAY =
mode 0x14/0x15 screen). Not yet dumped.

## Ghidra headless recipe (learned 2026-10-13)

- `.py` postScripts FAIL: "Ghidra was not started with PyGhidra". Use a
  Java GhidraScript instead — working template
  `/tmp/opencode/render-scr2/Dump.java` (args: address-list file, output
  file; one hex address per line, decompiles to C).
- Command: `/opt/ghidra/support/analyzeHeadless <projdir> Pilkki
  -process ProPilkki2.exe -noanalysis -scriptPath <dir> -postScript
  Dump.java addrs.txt out.c` (analysis already done in the base
  project).
- Parallel agents: `cp -r` the base project first (`ghidra-proj`,
  `ghidra-render`/`ghidra-sim`/`ghidra-state`/`ghidra-audio` copies in
  /tmp/opencode) — one headless per project dir, lock otherwise.

## Open threads

- D3D11 init: no static caller of the import thunk, string xrefs absent —
  likely `GetProcAddress` path (`006739a2`? `00431df0`? `00402650`?). Follow
  WinMain init chain (`00447900`, `005f47d0`, `00605b10`) and WndProc `005f3820`.
- OBJECT_POSITIONS item fields: `[u8 0][u8 class][u32 x][u32 y][u32 z?][u64 ?]`
  (stride 25); class tables at `+0x207a4` (< 0x1e) / `+0x1f01c` (< 0xfb).
| 0064a5f0 | `ClearRenderTargetView` wrapper (ctx +0xd4) — called from UI paths |
| 0064a530 | `OMSetRenderTargets` wrapper (ctx +0xc8) |
| 00652940 | UI rect fill (RGB565-packed color) + clear |
| 0040ab00 | 2D rect draw workhorse (RGB565→float, many UI callers) |
| 0064a330 | screenshot capture (CreateTexture2D/GetBuffer/CopyResource/Map/Unmap) |
| 0040d320 | **.obj mesh loader**: tries `Pro Pilkki object` (v1) then
  `Pro Pilkki Object` (v2); verts/faces/textures/footer (see file-formats) |
| 004070e0 | model manager init: calls `.obj` loader, per-texture path build
  (`.pcx` default ext), texture units init |
| 00401870 | OSND music demuxer (chunked Ogg) |
| 0043d820 | **GameClient dispatch**: LAN checksum verify (`00463370`), msg id
  u16 (< 0x48 = 72 handlers at obj+0x5a0), ack-byte dedup @+0x200 |
| 00463370 / 00446800 | LAN checksum: zero u16@+4, XOR-fold all u16 words
  (odd trailing byte), compare low16. No capture to verify against yet |
| 005a0030 | Ogg voice loader (but 194/196 .sfo are OSND — routing TBD) |
| 0062f750 | **.spr file decoder**: 12 B hdr (`nframes,bpp,enc,flags,w,h,key,eh`),
extra-hdr, 2×`u16[nframes]` tables, raw (enc 0) / RLE (enc 1, bpp 1 or 2) /
interleaved (enc 2, frames.spr only), 256×3 B palette if bpp==1, upload via
`0062e390` when caller arg != 0; `nframes>1` sets anim flag +0x123 |
| 0062ea00 | sprite buffer alloc (`w*h*nframes*bpp` + tables); `OUT OF MEM!!` |
| 0062e390 | sprite→texture atlas uploader (splits >200-texture sprites, `SPR3D%d`
textures, per-frame rects at +0x554); logs `SPRITE SIZE too large…` |
| 0062fcd0 | 3D font setup from sprite (zero-frame guard); caller `004344b0` = sprite
manager init (0x100-slot tables, glyph remaps) |
  SetCooperativeLevel(PRIORITY) via vtable +0x18, GetCaps +0xc, 0x80 voice
  slots, log `Sound init fail`) |
- Present call found: `00649810` = `SwapChain->Present(1, 0)` (vtable slot
  8, vsync) + `context->Flush`, bumps the governor frame counter
  `7c8b0c`, snapshots per-frame mouse deltas (`7c8ae8/7c8ae4` ->
  `7c8aec/7c8af0`); return 0 = device lost -> tick logs
  `Restoring DirectX..` and runs `006325c0/0064f0f0/00632640`.
  **Corrected globals** (earlier notes had swapchain/device swapped):
  device = `DAT_007c8b14`, swapchain = `DAT_007c8b8c`,
  context = `DAT_007c8b88`, RTV = `DAT_007c8b90`, DSV = `DAT_007c8b9c`.
  Present is a vtable call, so no IAT thunk exists — byte-scan
  `FF 5x 20` (`tools/`-side Ghidra script Vt20.java) found it.
- Renderer-init caller `0064ebb0` decompiled: picks window size mode
  (logs `Window size selected: %dx%d.`), `UpdateWindow`/`ShowWindow`,
  `0064ad60` device + `0064c480` RTV/DSV + `0064a790`, re-uploads
  textures via `0064f150` (Sleep(1) every 10), swapchain private data
  `0064cf10`, clear color (0.75 grey or scaled), sets viewport, then
  VSSetConstantBuffers (+0x8c). Failure -> `0064e380` teardown.
- Frame-tick callees + mode query + screen driver decompiled (see
  "Frame-tick stage bodies" / "Screen/scene transition driver");
  7c6904 sub-state internals + per-screen vtables decompiled and
  mode protocol ported (`src/state.hpp`); left: `0063ca80` input /
  `00638dd0` UI draw, per-screen vtable bodies + fades.
- D3D11 error strings (`…FAILED`, `d3d11.dll`) have no imm32 refs — fallback
  path addressing TBD, not important.
- One Ghidra run at a time: parallel runs fail with project LockException.
