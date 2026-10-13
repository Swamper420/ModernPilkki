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

Sim open items (docs/sim.md delta 20): VK->command map (00495930 feeder),
results builder 004c2ce0, full 0048af00 drill walk, move-script countdown
durations, lake walk grid corner bit 0x10 + per-cell populations,
world-unit scale -- tracked in `docs/sim-open.md`.

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
| 7 | `<this>` | terrain cell pipeline: cull (0040e260 integer cross + `DAT_006d0590`/`005b8` test) + layout-B append (006487f0) + flush signal (0064c8a0) in `src/render3d_terrain.hpp`; camera CB0/CB1 packing (00650910/0064f470) in `src/render3d_camera.hpp`; `render3d_view --terrain` (synthetic grid, layout-B triangle list); tests |
| 8 (next) | - | `004a7660` per-frame projection (depth grid -> screen positions): cell array `+0x15248`, frame-delta `7c695c`, camera fields `+0x1ee88/90/98` (docs/render.md 8.1/13); then wire the world + camera into `main.cpp` mode 0x14 |

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
RGBA8 target (3504/4096 non-black, 3 colours). Game smoke test:
`SDL_VIDEODRIVER=dummy ./build/pilkki extracted/lakes/haapolahti.ves
extracted/data/objects.dat 0 60` runs 60 frames (lake 99x109, 219 audio
clips, 5 bots, 2083 mesh tris) and exits 0.

### Known issues (pre-existing, unrelated to the 3D work)

- The repo `build/` is configured `Release` (`-DNDEBUG`) so **all `assert`
  tests are compiled out** and "pass" vacuously. An assert-on build
  (no `CMAKE_BUILD_TYPE`) exposes: `test_game` fails at the walk test —
  `game::Click` rejects an out-of-bounds target (`Walkable` max
  `(w-1)*600`), but the test expects a walk that stops at that wall
  (docs/sim.md delta 3 walk grid, still open). Fix `004b0bb0`/`00495930`
  first, then re-point the test. `test_bots` fixed (`6000` ticks; a bot's
  first catch needs >= ~720 ticks, `600` could never observe one).

Rules: match behavior first, flags for new mechanics, build stays
`cmake -S src -B build && cmake --build build`, tests per system,
never commit binaries/extracted/Ghidra blobs.
