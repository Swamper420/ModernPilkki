# Decomp notes

Ghidra 12.1.2 project: `/tmp/opencode/ghidra-proj` (outside git).
Target `extracted/ProPilkki2.exe` (x86, VS2022, 15080 functions). DWARF-free,
MSVC + `__security_check_cookie` everywhere.

## Anchors (verified via decompiler)

| addr | role |
|---|---|
| 0066f076 | PE entry (CRT startup) |
| 005f55c0 | **WinMain**: single-instance mutexes `{ProPilkki2-0000/0001}`, `RegisterClassExA("MainWClass")`, WndProc `005f3820`, `PeekMessageA` pump, per-frame `00632640`, `PostQuitMessage` exit |
| 00605070 | engine init: `Game version: v2.0.7`, critical sections, subsystem allocs |
| 004b6d70 | **.ves lake loader** (called from `004b18e0`); reads INFOBLOCK[/V2], DEPTH_TABLE (+defaults), TYPEMAP/DEPTHMAP (+`type.dat`/`depth.dat` cache write), OBJECT_POSITIONS, STRINGINFO_FI/EN, GAMETYPES… |
| 00430770 | **section reader**: `<TAG> u32 len payload u32 hash`; skips unknown sections by `len+4`; mode from `00430d10` (1 = legacy lakefile, 2 = v002); status 0 = hash OK, 5 = mismatch-but-usable, 1/2/3/4 = fail |
| 00430d10 | file sniffer: `Pro Pilkki lakefile` → 1, `Pro Pilkki datafile v002` → 2 |
| 00430ee0 / 00430f30 | section cache writer (`fwrite`) / `free` wrapper |
| 0066139d | `D3D11CreateDeviceAndSwapChain` IAT thunk |

## .ves integrity hash (mode 2, cracked, 768/768 verified)

`H = XOR_j ((byte_j ^ 0x5A) << (j mod 24))` (u32). AVX2 in binary is just
vectorization (`0x2AAAAAAB` div-magic computes the mod-24 shifts,
`0x5A5A5A5A` xor key). File trailer = H(whole file minus last 4 bytes).
Mode 1 (legacy): alternating-`NOT` de-obfuscation + byte-sum. See
`tools/ves_hash.py`. Single-byte-diff pairs across lakes pinned `shift = pos`
before generalizing.

## Open threads

- D3D11 init: no static caller of the import thunk, string xrefs absent —
  likely `GetProcAddress` path (`006739a2`? `00431df0`? `00402650`?). Follow
  WinMain init chain (`00447900`, `005f47d0`, `00605b10`) and WndProc `005f3820`.
- OBJECT_POSITIONS item fields: `[u8 0][u8 class][u32 x][u32 y][u32 z?][u64 ?]`
  (stride 25); class tables at `+0x207a4` (< 0x1e) / `+0x1f01c` (< 0xfb).
- Frame `00632640`, WndProc `005f3820` not yet decompiled.
- One Ghidra run at a time: parallel runs fail with project LockException.
