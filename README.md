# ModernPilkki

Native Linux port of Propilkki 2 (P2Installer_v2.0.7.exe), in Rust,
identical to the original. Clean-room: no original binaries/assets in git.

## Destination

The shipped game must be **identical to Propilkki 2 v2.0.7**: same files
parsed the same way, same simulation results for the same inputs and seeds,
same frame timing, same pixels, same sounds. Parity is the gate — it is what
makes the Linux version trustworthy and makes later modifications safe (a
mod can diff against a known-identical baseline).

Until parity is proven for a system, any divergence from the original is a
bug. **No placeholders**: no stand-ins, synthetic fallbacks, or "close
enough" constants in gameplay, render, or audio paths. If a routine is not
decompiled yet, decompile it first. Only tests and offline tools may fake
anything.

New mechanics and Linux conveniences come **after** parity, behind compile-
time or runtime flags, never mixed into parity fixes.

## Language and dependencies

- All new code is **Rust** (from 2026-10-14). No new C++.
- External crates are welcome when they replace weeks of unglamorous work:
  Vorbis/OSND decode, image decode, windowing and audio I/O. Game logic is
  never delegated — parity math (MSVC rand, float evaluation order, timers)
  is hand-ported so it matches the original bit for bit. Never add a dep for
  what a few lines can do.

## Layout

- `original/` - local installer copy only (ignored, see SHA256SUM.txt)
- `extracted/` - NSIS extract output (ignored)
- `decomp/` - Ghidra/notes, only committed summaries (no .gpr blobs)
- `rust/` - the port (Cargo workspace): `cargo build` / `cargo test`
- `src/` - C++17 reference port (SDL2, CMake): behavioral oracle for the
  Rust work; kept building until its systems are replaced 1:1, then deleted
- `tools/` - extract/analyze scripts
- `docs/` - findings, file formats, progress

## Next agent workflow

1. Extract: `7z x original/P2Installer_v2.0.7.exe -oextracted/`
   Arch: `sudo pacman -S p7zip`
2. Triage: `file extracted/**/*`, list EXE/DLL, detect compiler (strings, PE headers)
3. Decomp root: one Ghidra project in `decomp/`, document entry point + main
   loop in `docs/`; every ported routine cites its address
4. Port in Rust into `rust/`, one system per commit, each with a parity test
   (original behavior from the decomp, or fixtures from the C++ oracle)
5. New mechanics go behind flags, never mix with parity fixes

## Rules

- Never commit `original/*.{exe,dll}` or `extracted/` contents
- Decomp output committed = `.c/.h` + notes only, no Ghidra blobs
- Rust: `cargo build && cargo test` in `rust/`. C++ must keep building
  (`cmake -S src -B build && cmake --build build`) until it is deleted
- No placeholders in shipped paths; tracked in `docs/roadmap.md` (Parity gap)
- Need original file? Ask user, verify against `original/SHA256SUM.txt`:
  `e9558e764271662b6e143a9d9483c9f7cffcbddecf77713f4834a4b247f3393a`
