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

## Next

- Frame-tick stages all decompiled (`docs/decomp-notes.md`); next:
  `0064ebb0` renderer init + Present call site, `005d86e0` mode query,
  then sub-state internals of the 7c6904 machine; new mechanics behind
  flags

Rules: match behavior first, flags for new mechanics, build stays
`cmake -S src -B build && cmake --build build`, tests per system,
never commit binaries/extracted/Ghidra blobs.
