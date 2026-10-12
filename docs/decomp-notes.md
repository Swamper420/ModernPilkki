# Decomp notes

## Status: Ghidra import running (3rd attempt, 6 GB heap)

Project: `/tmp/opencode/ghidra-proj` (kept outside git — no blobs committed).
Target: `extracted/ProPilkki2.exe`, `x86:LE:32:default`.

- Attempt 1: bad `-compiler` flag (does not exist in 12.1).
- Attempt 2: `OutOfMemoryError: Java heap space` in X86Analyzer (default 2 GB).
- Attempt 3: `-Xmx` passed as tool arg rejected; correct knob is
  `GHIDRA_HEADLESS_MAXMEM=6G` env. In progress.

## Entry (from PE headers, verified)

- `ProPilkki2.exe` entry: `0x0066f076`, ImageBase `0x00400000`.
- `_dx7` entry: `0x0063601f`. `RecMergerP2` entry: `0x00401000`.
- Entry = CRT startup, not game code. WinMain + game loop addrs pending
  Ghidra: look for `WinMain` xref, `PeekMessage`/`GetMessage` loop,
  `D3D11CreateDeviceAndSwapChain` caller (renderer init).

## Next

1. Dump function list + WinMain candidate to `decomp/` notes (text only).
2. Decompile renderer init + `.ves`/`.spr` loaders first (port order:
   formats → rendering → audio → net).
3. Port skeleton builds (`src/`, SDL3 stub). Linux build stays:
   `cmake -S src -B build && cmake --build build`.
