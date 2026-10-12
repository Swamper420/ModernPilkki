# Decomp notes

Ghidra 12.1.2 project: `/tmp/opencode/ghidra-proj` (outside git).
Target `extracted/ProPilkki2.exe` (x86, VS2022, 15080 functions). DWARF-free,
MSVC + `__security_check_cookie` everywhere.

## Anchors (verified via decompiler)

| addr | role |
|---|---|
| 0066f076 | PE entry (CRT startup) |
| 005f55c0 | **WinMain**: single-instance mutexes `{ProPilkki2-0000/0001}`, `RegisterClassExA("MainWClass")`, WndProc `005f3820`, `PeekMessageA` pump, per-frame tick `00605eb0`, `PostQuitMessage` exit |
| 00605070 | engine init: `Game version: v2.0.7`, critical sections, subsystem allocs |
| 004b6d70 | **.ves lake loader** (called from `004b18e0`); reads INFOBLOCK[/V2], DEPTH_TABLE (+defaults), TYPEMAP/DEPTHMAP (+`type.dat`/`depth.dat` cache write), OBJECT_POSITIONS, STRINGINFO_FI/EN, GAMETYPES… |
| 00430770 | **section reader**: `<TAG> u32 len payload u32 hash`; skips unknown sections by `len+4`; mode from `00430d10` (1 = legacy lakefile, 2 = v002); status 0 = hash OK, 5 = mismatch-but-usable, 1/2/3/4 = fail |
| 00430d10 | file sniffer: `Pro Pilkki lakefile` → 1, `Pro Pilkki datafile v002` → 2 |
| 00430ee0 / 00430f30 | section cache writer (`fwrite`) / `free` wrapper |
| 0066139d | `D3D11CreateDeviceAndSwapChain` IAT thunk |
| 0064ad60 | **D3D11 renderer init**: tries driver types [1,5,2], feature levels
  11_1→9_1 (retry w/o 11_1 on E_INVALIDARG), logs `Selected D3D_FEATURE_LEVEL_*`
  via `00431970` (log fn), creates swapchain + RTV. Called from `0064ebb0` |
| 005f3820 | WndProc: input router + `Reset DX` on display change, save+exit on EndSession |
| 00431970 | log function (`Game version: v2.0.7`, feature levels, `Resolution change…`) |
| 00605eb0 | **frame tick** (WinMain loop): timer delta, quit flag (`DAT_007c689c`
  → 2), update `0062e230/00471810/00467e80/00651620/00606bd0/00658570`,
  render block `006499e0/006440b0/00640370/00652940/00638280/0064c920`,
  DX-restore check |
| 00632640 | async-load gate (50 ms sleeps), NOT the frame |

Lesson: Ghidra xref queries can miss callers (thunk showed none); a raw
`E8`-CALL byte-scan found both call sites (`64ae91`, `64aed5`). Verify.

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
| 0064a5f0 | `ClearRenderTargetView` wrapper (ctx +0xd4) — called from UI paths |
| 0064a530 | `OMSetRenderTargets` wrapper (ctx +0xc8) |
| 00652940 | UI rect fill (RGB565-packed color) + clear |
| 0040ab00 | 2D rect draw workhorse (RGB565→float, many UI callers) |
| 0064a330 | screenshot capture (CreateTexture2D/GetBuffer/CopyResource/Map/Unmap) |
| 0040d320 | **.obj mesh loader**: tries `Pro Pilkki object` (v1) then
  `Pro Pilkki Object` (v2); verts/faces/textures/footer (see file-formats) |
| 004070e0 | model manager init: calls `.obj` loader, per-texture path build
  (`.pcx` default ext), texture units init |
| 00401870 | OSND music demuxer (chunked Ogg) |
| 005a0030 | Ogg voice loader |
  SetCooperativeLevel(PRIORITY) via vtable +0x18, GetCaps +0xc, 0x80 voice
  slots, log `Sound init fail`) |
- Present call outstanding (replace with SDL in port; swapchain = `DAT_007c8b14`,
  device = `…8b8c`, context = `…8b88`, RTV = `…8b90`).
- Frame-tick callees (`006499e0` render?, `006440b0`, `00640370`, `00652940`,
  `0064c920`, update `0062e230`…) not yet decompiled; Present call not found yet.
- Renderer-init caller `0064ebb0` not yet decompiled.
- D3D11 error strings (`…FAILED`, `d3d11.dll`) have no imm32 refs — fallback
  path addressing TBD, not important.
- One Ghidra run at a time: parallel runs fail with project LockException.
