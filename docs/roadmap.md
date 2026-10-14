# Roadmap to parity

Destination: a game **identical to Propilkki 2 v2.0.7** on Linux, in Rust.
Parity is the gate: it is what makes the Linux version trustworthy and makes
later modifications safe (a mod diffs against a known-identical baseline).
Until parity is proven for a system, any divergence from the original is a
bug. **No placeholders**: no stand-ins, synthetic fallbacks, or invented
constants in gameplay/render/audio paths — see "Parity gap" below for what
still violates this. External crates are welcome where they replace weeks of
unglamorous work (Vorbis/OSND decode, image decode, windowing/audio I/O) —
never for game logic, which is hand-ported bit-exact (MSVC rand, float order,
timers). All new code is Rust (`rust/` Cargo workspace, `cargo build` /
`cargo test`); the `src/` C++ tree is the behavioral oracle until its systems
are replaced 1:1, then it goes away. New mechanics only after parity, behind
flags.

The old goal — `pilkki` opens window, loads lake, renders scene, plays sound,
input works — is reached (see Done), but that is a demo, not the destination.

## Done

- Rust tree started (`rust/`, `88c1512`): Cargo workspace + SDL window
  skeleton + bit-exact MSVC rand port (`00676d2c`/`0059db20`), parity test
  asserts the `test_game.cpp:74` golden vectors (18 asserts). Env note:
  system SDL2 is sdl2-compat 2.32 (SDL3-backed) and forwards unmapped event
  `0x207`; rust-sdl2's safe event pump aborts on it — skeleton polls
  `SDL_PollEvent` at sys level and ignores unknown types (as a C SDL2 app).
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
- In-game HUD (`src/hud.hpp`, `src/test_hud.cpp`): depth gauge
  (depthbar/d_up+d_base+d_down + d_lure needle on the 10..40 m buckets),
  round icon buttons (clock/clk_1..5) with fish/bait/line counts, timer +
  tally, bite flash + pause/results overlays, over the PLAY background;
  text via the game bitmap fonts (fonts/fnt.spr 7x8 + fnt_iso.spr 18x23,
  glyph frame = ASCII - 0x20); missing assets fall back to ui::Render
  debug text. Layout is the port's design (004bc9e0/004bd330 panel
  placement not decompiled yet).

## Next

- Per-screen fades: fade machine ported 1:1 (`src/state.hpp`: FadeTimer,
  Screen, FadeUpdate=00651620, ScreenStep=006304e0, ScreenRun=0058aaf0,
  ScreenFadeDur=005c8930; tests in `src/test_state.cpp`); decomp in
  `docs/state-open.md` (linear 0..1000 permille over `durationMs`, 88-slot
  registry, corrected duration table). Open: wire `ScreenRun`/`FadeUpdate`
  into `ui::Game`/`main.cpp`. Per-screen fade *application* only verified
  for MENU_MAIN (image alpha); no shared full-screen fade exists.
- PLAY input `0063ca80` + in-game UI draw `00638dd0` walked
  (`docs/state-open.md` §3/§4); the screen-class cluster
  `006489c0/00648d80/00648f60/00649370/00649540/00649a30` is NOT the
  fade code but the 2D D3D draw helpers (see docs/render.md §5).

Sim gameplay 1:1 (night of 2026-10-14, per-system docs authoritative,
`docs/sim.md` §8 errata supersedes its first-pass claims):

- `3b5554d` opponent AI: event/action queue + `AI_STATE_{WALKING,
  DRILLING, FISHING, FISH_TAKE_UP}` machines (`src/botai.hpp`,
  docs/sim-ai.md)
- `6a457be` round structure: move script (`<STARTMOVES>` of
  human2.3ds), drill chain 9→0x12, end hold 2300 ms, results rows
  (`src/timeline.hpp`, `src/results.hpp`, docs/sim-timeline.md /
  docs/sim-results.md)
- `a7b9ec9` bite records 0x70 + candidate walk + rate window
  (`src/bite.hpp`, docs/sim-bite.md)
- `446326c` controller: drill run (`stepMs=(rand(4)+13)*200`, stop
  rolls) + fish fight state 0xd + WalkMs (`src/controller.hpp`,
  docs/sim-controller.md)
- `3dc1219` DATA1 fish stock loader (deobf keys `006dbfa8`, species
  from `data/nyky/data0.dat`) (`src/fishstock.hpp`, docs/sim-stock.md)
- `38e22f8` click orders + 16-case walkability grid + `test_game` fix
  (`src/walkgrid.hpp`, docs/sim-walk.md)
- `a6bddc3` sim core integration (`game.hpp` delegates; one RNG)
- `9c5a642` loop wiring: real stock at SetLake, round end 1:1, results
  rows on the Results screen, bots drive real sim sessions via
  `botai::Hooks`

Sim open items: `004b6990` walk-grid builder (type.dat/depth.dat
corner bytes — `SetWalkGrid` waits on it), `005f8ff0` candidate-eval
chain + `005f8490` size roll (`Stock::Evaluate` seam), `004bc340` fish
migration (static-stock ceiling), `00487560(+0x340)` Nosto duration,
`0059fbe0` bot quality curve (identity stand-in), results `0x2bc`
value source + MENU_RESULTS_* row layout handlers, `00467c30`
keyframe eval (presentation), event id 0x10 + RTTI names for unported
state ids, `+0x33c` verdict source (net).

## Parity gap: placeholders to purge

Every item below is a shipped-path placeholder — a divergence from the
original. Purge = decompile the real routine, port it 1:1, delete the
stand-in. Nothing new may be added to this list.

Sim/gameplay (`src/`):

- `bite::SyntheticStock` + `game.hpp` synthetic fallback (`game.hpp:157/580/644`)
  — dies with the sim open items above (`004b6990`, `005f8ff0`, `005f8490`,
  `004bc340`)
- `offline.hpp:240` bot quality curve `0059fbe0` identity stand-in;
  `offline.hpp:398` stuck-action stand-in
- `botai.hpp:147` round clock `DAT_007c695c` stand-in
- `bite.hpp:353` `failP` uninitialised-value stand-in
- `fishstock.hpp:295` depth fn `004a5700` stand-in
- walk-grid bounds fallback mask 15 (`test_game.cpp:199`) — needs `004b6990`
- `00487560(+0x340)` Nosto duration, results `0x2bc` value source +
  MENU_RESULTS_* row layout, `00467c30` keyframe eval, event id 0x10 + RTTI
  names, `+0x33c` verdict (net)

Audio (`src/audio_play.hpp`, docs/audio.md):

- drill = synth growl; KairausPlain.sfo is OSND and needs real Vorbis decode
  (`audio_play.hpp:170`) — candidate external dep (lewton-class), massive win

Render (`src/render3d_*`, `src/hud.hpp`, docs/render.md):

- HUD layout is the port's design: `004bc9e0`/`004bd330` panel placement not
  decompiled (`hud.hpp:10`)
- sampler stand-in (original's 4 sampler objects) + terrain VS pair stand-in
  (`render3d_pipeline.hpp:123/339/555`)
- lake bed: `004a67b0`/`004a84a0` open; DEPTHMAP stand-in bed + grey palette
  (`render3d_scene.hpp:37/142`)
- object draw `0040afd0` stand-in (`render3d_objects.hpp:2`)
- angler shadow alpha ceiling (`angler.hpp:146`)
- fades ported but not wired (`ScreenRun`/`FadeUpdate` -> `ui::Game`/
  `main.cpp`); PLAY input `0063ca80` + in-game UI draw `00638dd0` walked,
  not ported
- it12: dynamic entities `004a7030`, rod `0040b0b0`, menu variant `004a7b70`,
  bed passes above

## Rust migration (from 2026-10-14)

All new code is Rust; no new C++. Per system:

1. Decompile the routine(s) first (addresses cited in `docs/`).
2. Port into `rust/` with a parity test: same inputs/seeds -> same outputs as
   the original (verified against the decomp or fixtures from the C++ oracle).
3. When a system's Rust tests cover the C++ implementation, delete the C++
   side. `src/` keeps building (`cmake -S src -B build && cmake --build
   build`) until fully replaced.

`rust/` is a Cargo workspace: `cargo build && cargo test` from `rust/`.

## 3D render: decomp done (docs/render.md), SDL_GPU port is the remaining rock

`docs/render.md` resolves the PLAY-screen scene: the lake is NOT screen
MENU_PLAY (id 0x15, a menu state machine) but mode states 0x14/0x15 ->
`00638dd0` -> `004a5d00` (world draw). Contains the full D3D11
object/vtable map, 11 precompiled DXBC blobs (addr+size), 6 input
layouts, 8 constant buffers, 6 draw wrappers, CPU-projected terrain
chain, camera/matrix builders, a constants cheat-sheet, and an SDL_GPU
port plan (`src/render3d.*`).

Port: the 11 shaders must be re-authored (clean-room, DXBC not
shippable) -> the only non-1:1 area. Resolve before coding: view-vs-
projection assignment (`0064f470`) + fov/near/far builder, `00650910`
CB0 layout, `004a5d00` this identity, `0064b350` fixed-buffer init.
Port as NEW files (`src/render3d.*` + viewer + tests) first, wire into
`main.cpp` last.

### Port iterations (small, committed)

| it | commit | what |
|---|---|---|
| 1 | `45a7cf4` | `src/render3d.hpp` SDL_GPU device/swapchain + `render3d_view` + letterbox/clear math; `test_render3d` |
| 2 | `498d1d8` | `src/render3d_math.hpp` 1:1 4x4 math (`00598dc0/00598860/00598460/00599d40/00650380`); corrected docs (00599d40 args are rotation, not scale) |
| 3 | `9af269f` | `src/render3d_layout.hpp` 6 input layouts (`0064b560`), CB table (`0064a790`), fixed buffers (`0064b350`), terrain append (`006487f0`) |
| 4 | `ae941ac` | shader build (`glslc` -> SPIR-V via CMake, `src/shaders/*.vert\|frag`) + first graphics pipeline: one hardcoded triangle (`src/render3d_pipeline.hpp`), drawn in the `render3d_view` letterbox; SPIR-V artifact assert in `test_render3d` |
| 5 | `5ea6e54` | layout-B textured quad + the buffer/texture upload path (`src/shaders/quad.*`, `render3d_pipeline.hpp` `Quad`: VS+PS, sampler, RGBA8 texture, VB/IB via `SDL_UploadToGPUBuffer`, `MakeQuad`); drawn in `render3d_view --quad`; pixel-verified offscreen |
| 6 | `4902336` | layout-A `.obj` static mesh + `.pcx` texture upload (`render3d_mesh.hpp` `BuildMeshA`, `render3d_pipeline.hpp` `MeshGpu`, clean-room `src/shaders/mesh.*`); `render3d_view --obj P [--tex Q]`; generalised `Ctx::Draw`; pixel-verified with `varis_b.obj` + `varis.pcx` |
| 7 | `5914a85` | terrain cell pipeline: cull (0040e260 integer cross + `DAT_006d0590`/`005b8` test) + layout-B append (006487f0) + flush signal (0064c8a0) in `src/render3d_terrain.hpp`; camera CB0/CB1 packing (00650910/0064f470) in `src/render3d_camera.hpp`; `render3d_view --terrain` (synthetic grid, layout-B triangle list); tests |
| 8 | `da90636` | world-space lake scene (`src/render3d_scene.hpp`): DEPTHMAP -> layout-A mesh + palette texture + clean-room look-at/perspective; `InitMeshVerts` split in `render3d_pipeline.hpp`; `render3d_view --lake P`; tests (later fix: LookAt x = z x up, y = x x z -- the first cut mirrored the world and movement E/W) |
| 9 | `c5e4ae8` | in-game bridge (`src/render3d_bridge.hpp`, `pilkki --3d`): offscreen SDL_GPU scene -> per-frame readback -> `SDL_Texture` PLAY background under the SDL_Renderer HUD; angler re-drawn at a fixed spot; CMake defines the shader paths for `pilkki` |
| 10 | `3771cc8` | static lake objects (`src/render3d_objects.hpp`): objpos -> baked world-space layout-A meshes (objects.dat Angle/Scale, game units -> +-128 frame), drawn in `pilkki --3d`; world-pass depth buffer (new `depthFmt` arg on `InitMeshVerts` + `render3d_bridge` depth target); clean-room magenta colour-key in `mesh.frag`; case-insensitive asset open; tests (later fix: P2 .obj is y-down with the origin at the base -- BuildMeshA mirrors model y so props stand upright) |
| 11 | 5d60586 | exact `004a7660` weather projection (`src/render3d_terrain.hpp` `WeatherField/SpawnWeather/UpdateWeather`): 004bce00 spawn (166 snow / 500 rain, wide x1.5 clamp 1000), depth factor `s`, wind + camera parallax, one-step screen wrap + life re-roll, 20*pi phase wrap, sin(1/2+1/10)-sway right-triangle into the it7 layout-B batch; `render3d_view --weather`; docs/render.md 8.6 CORRECTS §8: `004a7660` is the particle field, not the DEPTHMAP grid (bed pass `004a67b0`/`004a84a0` stays open); tests |
| 12 (next) | - | move the HUD to SDL_GPU and drop the readback bridge; dynamic entities (fish/other anglers, 004a7030) and the local rod (0040b0b0); wire `WeatherField` into `pilkki --3d` (needs weather id + `004c02d0` wind tables); `004a7b70` menu-screen variant (caller `004841e0`) |

Shaders are re-authored GLSL (clean-room); no DXBC shipped. `glslc`
present in this env; CMake targets skip the GPU/exe gracefully if absent
(`render3d_view` then falls back to clear-only). Verified end to end with
`SDL_VIDEODRIVER=offscreen SDL_GPU_DRIVER=vulkan ./build/render3d_view --quad --frames 3`
(prints `quad pipeline ready`); the quad is additionally pixel-checked by
rendering to an offscreen RGBA8 target (two checker colours present). The
it6 mesh (`--obj extracted/gfx/obj/animals/varis_b.obj --tex
extracted/gfx/txt/misc/varis.pcx`) is likewise pixel-checked (427 distinct
colours, 667 non-grey pixels of 128x128). The it7 terrain grid
(`SDL_VIDEODRIVER=offscreen SDL_GPU_DRIVER=vulkan ./build/render3d_view --terrain
--frames 3`, prints `terrain pipeline ready`) is pixel-checked offscreen into an
RGBA8 target (3504/4096 non-black, 3 colours). `render3d_view --lake
extracted/lakes/haapolahti.ves --frames 3` prints `scene pipeline ready`; the
scene mesh is pixel-checked offscreen (8186/16384 non-black). In-game:
`SDL_VIDEODRIVER=offscreen SDL_GPU_DRIVER=vulkan ./build/pilkki
extracted/lakes/haapolahti.ves extracted/data/objects.dat 0 90 --3d` runs 90
frames with `3d background: on` and exits 0. Game smoke test:
`SDL_VIDEODRIVER=dummy ./build/pilkki extracted/lakes/haapolahti.ves
extracted/data/objects.dat 0 60` runs 60 frames (lake 99x109, 219 audio
clips, 5 bots, 2083 mesh tris) and exits 0. it10: the same `--3d` run prints
`3d props: 30` (haapolahti) / `13` (vuolasniva) and the offscreen render shows
the lake's trees/buildings/cars placed on the terrain (`propscheck`, scratch);
the terrain + props pass is depth-tested (D32_FLOAT) and magenta is cut out.
it11: `SDL_VIDEODRIVER=offscreen SDL_GPU_DRIVER=vulkan ./build/render3d_view
--weather --frames 30` prints `weather pipeline ready`; a scratch check
(`weathercheck`) renders 166 snow cells into an offscreen target — 7052/307200
drawn pixels, both checker colours (per-frame dynamic VB rebuild verified).

### Known issues (pre-existing, unrelated to the 3D work)

- The repo `build/` is configured `Release` (`-DNDEBUG`) so **all `assert`
  tests are compiled out** and "pass" vacuously — use an assert-on build
  (`cmake -S src -B <dir> -DCMAKE_BUILD_TYPE=`) for the real gate; all
  30 test binaries pass there as of `9c5a642`. (The walk-test failure
  this note used to track was real and is fixed in `38e22f8`: out-of-
  bounds clicks are accepted and stop at the wall — the test expected
  the original behavior, `game::Click` was wrong.) `test_bots` fixed
  earlier (`6000` ticks; a bot's first catch needs >= ~720 ticks,
  `600` could never observe one).

Rules: parity before features (destination: identical to Propilkki 2
v2.0.7), flags for new mechanics, new code in Rust (`cargo build && cargo
test` in `rust/`; C++ `cmake -S src -B build && cmake --build build` until
deleted), tests per system, no placeholders in shipped paths (Parity gap
above), external deps only where they save massive effort — never for game
logic, never commit binaries/extracted/Ghidra blobs.
