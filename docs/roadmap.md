# Roadmap to playable

Goal: `pilkki` opens window, loads lake, renders scene, plays sound, input works.

## Done

- .ves sections + hash + objpos (`src/ves.hpp`)
- .spr decode + viewer (`src/spr.hpp`)
- .obj mesh + verts/faces/UV + viewer (`src/obj.hpp`, `src/objview.cpp`)
- .pcx decode (`src/pcx.hpp`)
- WAV/Ogg/OSND sniff + chunk demux + Ogg rebuild (`src/audio.hpp`)
- LAN checksum (`src/net.hpp`)
- .3ds STRUCTURE + MOVES + SHADOWS (`src/model3ds.hpp`)
- DEPTHMAP/TYPEMAP grids (`src/ves.hpp`)
- objects.dat class tables (`src/objects.hpp`)
- Input key bitmap + SDL mapping (`src/input.hpp`)
- Frame tick order + clock (`src/tick.hpp`)
- Lake scene: objpos + class map + depth view (`src/lakeview.cpp`)
- Playable loop wired into `pilkki` (`src/main.cpp`)
- botinfo.dat cipher (`src/botinfo.hpp`, 505/505 OK)
- Frame-tick stage bodies decompiled + timing 1:1 port: `tick::Elapsed`
  (0059ae70), `tick::Slices` (00471810), `tick::Governor` (00640370
  adaptive frame limiter), wired into `pilkki`
- Gameplay sim 1:1 per docs/sim.md section 6 deltas (`src/game.hpp`,
  `src/offline.hpp`, `src/main.cpp`, `src/test_game.cpp`): MSVC rand +
  rand(max), click-to-move (1500/600/300/90 thresholds, K=0.34/0.725),
  600-unit cell grid with bilinear depth + walk bounds, drill mode
  machine 5->6->0x13->0x18->0x27 with hole objects (id/open/land
  counter), discrete line {10,20,30,40} m (PgUp/PgDn + slots 1/2/3),
  Ambient timer rand(75000)+20000 with 50% roll, action attempt
  thresholds 125/250/500/always, 900 ms avg-gap bite limiter, 9-cell
  candidate walk, probabilistic catch (0.7/0.9/0.95 depth bands),
  weight min(v*k_line, cap_line), species band tables, 4x16 bait grid
  with consume-on-catch + lure-empty gating, biggest + per-hole tally,
  round-end hold 2000 ms -> Results, AI_* timer formulas
- State driver 1:1 (`src/state.hpp`): 006304e0 phase protocol,
  00632560 deferred copy, 7c8790 pause return; 7c6904 sub-state docs
- Audio routing (`src/audio_play.hpp`, docs/audio.md): 1000-slot clip
  registry, music select 005c8270 per-lake style, splash = random of 6
  cast WAVs, catch = NostoPlain; drill stays synth (OSND needs Vorbis)

## Next

- Frame-tick stages + Present (`00649810`, vtable slot 8, `Present(1,0)`)
  + device re-init (`0064ebb0`) + screen transition driver (`006304e0`,
  phase + permille fades) + 7c6904 sub-state internals + per-screen
  vtables (89-slot MENU_* registry) all decompiled; mode protocol
  ported 1:1 (`src/state.hpp`, pending/active/phase + 00632560
  deferred copy + 7c8790 pause return, wired via `ui::Step`); next:
  per-screen fades + 0063ca80 input / 00638dd0 UI draw; new mechanics
  behind flags

Sim open items (docs/sim.md delta 20): move-script table (pre-round
countdown chain 9->...->0x12 durations, drill/bore durations), lake
walk grid (corner bit 0x10) + real per-cell fish populations, VK->
command map (00495930), results builder 004c2ce0, full 0048af00
drilling sub-state walk, world-unit scale vs a captured run

## 3D render: decomp + port (open, the big remaining rock)

Task: find the PLAY-screen 3D scene draw, then port rendering 1:1
(SDL3). Notes + recipe in docs/decomp-notes.md "Screen vtable dispatch
map" + "Ghidra headless recipe". Lead: screen-class cluster
`006489c0/00648d80/00648f60/00649370/00649540/00649a30` (methods at
vtable disp 0x30 phase, 0x44/0x48/0x4c/0xac). Two decomp attempts were
cancelled mid-run; raw dumps of wrong function (00640370) + vtable xref
scan in /tmp/opencode/render-dec/ (volatile scratch). After
docs/render.md lands: port as NEW files (`src/render3d.*` + viewer +
tests) first, wire into `main.cpp` last pass.

Rules: match behavior first, flags for new mechanics, build stays
`cmake -S src -B build && cmake --build build`, tests per system,
never commit binaries/extracted/Ghidra blobs.
