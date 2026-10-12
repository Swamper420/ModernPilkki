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

## Next (one commit each)

1. botinfo.dat cipher (AI, defer if needed)
2. Playable loop: scene + input + tick + audio wired into `pilkki`

Rules: match behavior first, flags for new mechanics, build stays
`cmake -S src -B build && cmake --build build`, tests per system,
never commit binaries/extracted/Ghidra blobs.
