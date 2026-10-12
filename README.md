# ModernPilkki

Native Linux port + new mechanics for Propilkki 2 (P2Installer_v2.0.7.exe).
Clean-room decomp: no original binaries/assets in git.

## Layout

- `original/` - local installer copy only (ignored, see SHA256SUM.txt)
- `extracted/` - NSIS extract output (ignored)
- `decomp/` - Ghidra/notes, only committed summaries (no .gpr blobs)
- `src/` - portable C++ port (CMake, SDL)
- `tools/` - extract/analyze scripts
- `docs/` - findings, file formats, progress

## Next agent workflow

1. Extract: `7z x original/P2Installer_v2.0.7.exe -oextracted/`
   Arch: `sudo pacman -S p7zip`
2. Triage: `file extracted/**/*`, list EXE/DLL, detect compiler (strings, PE headers)
3. Decomp root: one Ghidra project in `decomp/`, document entry point + main loop in `docs/`
4. Port skeleton first: `src/` CMake + SDL window stub before deep decomp
5. New mechanics go behind flags, never mix with matching-port fixes

## Rules

- Never commit `original/*.{exe,dll}` or `extracted/` contents
- Decomp output committed = `.c/.h` + notes only, no Ghidra blobs
- Linux build must stay: `cmake -S src -B build && cmake --build build`
- Need original file? Ask user, verify against `original/SHA256SUM.txt`:
  `e9558e764271662b6e143a9d9483c9f7cffcbddecf77713f4834a4b247f3393a`
