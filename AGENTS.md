# AGENTS.md - ModernPilkki

## Goal
Decompile Propilkki 2 Windows game, ship native Linux port, add mechanics.

## Constraints
- Clean-room: never commit original binaries, extracted assets, Ghidra blobs. See .gitignore.
- `original/SHA256SUM.txt` is source of truth for installer version.
- Port stack: C++17, CMake, SDL2/3. No new deps without asking.
- Matching behavior first, new mechanics behind compile/runtime flag.

## Workflow for following agent
1. `7z x original/P2Installer_v2.0.7.exe -oextracted/` (install `p7zip` on Arch)
2. Inventory EXE/DLL: compiler, SDL version, DirectX calls. Write `docs/triage.md`.
3. Ghidra headless or GUI: entry point, WinMain, game loop. Write `docs/decomp-notes.md`.
4. Skeleton `src/CMakeLists.txt` + `main.cpp` opening SDL window. Must build on Linux.
5. Per-system port: input, audio, rendering, file formats. One system per commit.
6. Tests: smallest assert proving each ported function matches original behavior.

## Don't
- No large binaries in git. No `git add -f` around .gitignore.
- No Wine prefix or build/ dirs committed.
- No speculative abstractions; port what exists.
