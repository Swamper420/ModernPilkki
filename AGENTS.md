# AGENTS.md - ModernPilkki

## Goal
Ship a native Linux game **identical to Propilkki 2 v2.0.7**, in Rust.
Parity first — it is what makes the Linux version and later modifications
trustworthy. New mechanics only after parity.

## Constraints
- Clean-room: never commit original binaries, extracted assets, Ghidra blobs. See .gitignore.
- `original/SHA256SUM.txt` is source of truth for installer version.
- Language: **Rust for all new code** (from 2026-10-14). No new C++. The `src/`
  C++ tree is the behavioral oracle; keep it building until its systems are
  replaced 1:1 in `rust/`, then delete it.
- Parity gate: identical behavior to original v2.0.7. Every ported routine
  cites its decompiled address in `docs/`. Divergence from the original is a
  bug until proven otherwise.
- **No placeholders**: no stand-ins, synthetic fallbacks, or invented
  constants in gameplay/render/audio paths. Routine not decompiled yet?
  Decompile it first. Tests and offline tools may fake.
- External crates OK when they replace weeks of unglamorous work (Vorbis/OSND
  decode, image decode, windowing/audio I/O). Never for game logic: parity
  math (MSVC rand, float order, timers) is hand-ported bit-exact. No dep for
  what a few lines can do.
- Matching behavior first, new mechanics behind compile/runtime flag.

## Workflow for following agent
1. `7z x original/P2Installer_v2.0.7.exe -oextracted/` (install `p7zip` on Arch)
2. Inventory EXE/DLL: compiler, SDL version, DirectX calls. Write `docs/triage.md`.
3. Ghidra headless or GUI: entry point, WinMain, game loop. Write `docs/decomp-notes.md`.
4. Rust skeleton in `rust/` (Cargo workspace, SDL window) that builds and
   runs on Linux: `cargo build && cargo test` in `rust/`.
5. Per-system port into `rust/`: input, audio, rendering, file formats. One
   system per commit. When a system's Rust tests cover the C++ implementation,
   delete the C++ side.
6. Tests: smallest assert proving each ported function matches original
   behavior (decomp-verified, or fixtures from the C++ oracle).

## Don't
- No large binaries in git. No `git add -f` around .gitignore.
- No Wine prefix, `build/`, or `target/` dirs committed.
- No speculative abstractions; port what exists.
- No stand-ins in shipped paths, no new C++, no placeholder data. Deps that
  save massive effort are fine — note which and why in `docs/`.
