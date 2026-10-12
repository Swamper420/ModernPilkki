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
| 0049e3c0 | key bitmap set/clear (`DAT_007bcd5c`, bit = keycode & 0x1f) |
| 005f3820 | WndProc: input router + `Reset DX` on display change, save+exit on EndSession |

## Frame-tick stage bodies (00605eb0 callees, decompiled)

Update side:

| addr | role |
|---|---|
| 0059ae70 | **elapsed** `now-prev`; on wrap `(now-prev)-1` (uint huge, callers clamp). ~100 callers |
| 0062e230 | sprite **anim timer**: gate `elapsed > 39ms`, CS `7c6978`; per entry `+4` period ms, `+8` accumulator, `+0xc` callback; frame step on obj `+0x128` frame / `+0x138` speed / `+0x134` max / `+0x140` ping-pong (reverses speed at ends) |
| 00471810 | game **update slicer**: delta -> `<=999ms` slices, slice widens to `5000ms` while remaining `>60000ms`; each slice: entity update `0047f740` (count `+4`) + vtable+8 object update (count `+8`) |
| 00467e80 | update pass under CS `7c69a8`: per entity `0058ab20(elapsed)` + `00467210(elapsed)` (count `+8`) |
| 00651620 | timer/fade state machine: state `+0x1a` (0/1/2, else `INVALID TIMER STATE`), rate `+0x28`, double math `elapsed * rate` vs `1.0`; flag `0x10` = chain to next index (rewinds it), modes `5/6` pick slot 1, mode `8` keeps entry |
| 00606bd0 | **mouse input**: click edges (latch `7c6910/11`, press `7c6934/38/3c`, release `7c6915/16/17`), press/release pos deltas into 10-slot ring `7c6ac0` (index `7c6ab4`), path length `7c69f8 += hypot(dx,dy)`; returns `(elapsed/10)` clamped to 100 |
| 00658570 | small state machine `+0x890` (0 idle, 1/2 waiting on `00651220`, -> 3/0 on ready + `00651410`), then `00650300(delta)` + `006521c0(1,0)` |

Render side:

| addr | role |
|---|---|
| 006499e0 | one-shot latch: `7c696c = 1` (render-enable) |
| 006440b0 | **letterbox**: bar half-height `(1.0 - local_c) * 0.5`, two white `0040ab00` rect fills + RTV/viewport set (`local_c` = aspect from `0061e400`) |
| 00640370 | **frame governor + game-mode state machine** (720 lines, see below) |

### 00640370 governor (ported as `tick::Governor`)

Every `>=500ms` window: `fps = (int)(frames * 1000.0 / ms + 0.5)`
(constants `006d0590`=1000.0, `00777878`=0.5, both doubles). Sleep per
frame: mode 0 none, mode 1 adaptive (`fps>=121` -> sleepMs+1 capped 10;
`80<=fps<100` -> sleepMs-1 floor 0; `100..120` hold; `fps<80` -> 0),
mode 2 `Sleep(1)`, mode 3 `Sleep(5)`; mode from vsync probe `0061ec30`,
limiter flag `7c83bc`, sleep `7c83b4`, fps `7c6958`. Then the big
`DAT_007c6908` state switch (states 0/1/4/10/0x15/0x31/…) drives game
flow (`0063f160`, `004c2710`, `FUN_005d86e0` mode query) — not yet
walked state by state (open).

Constants dumped straight from the PE with `tools/peconst.py` (rdata
section); faster than decompiler round-trips for float tables.

Input inventory (from imports): keyboard ONLY via WndProc (no GetAsyncKeyState);
mouse via capture messages; clipboard API; Wiimote detect via HID+SetupDi,
I/O via ReadFile; serial port (SetCommState/Timeouts — hardware controller?);
no joystick API; GDI32 nearly unused (DeleteObject/CreateBitmap only).
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
| 0043d820 | **GameClient dispatch**: LAN checksum verify (`00463370`), msg id
  u16 (< 0x48 = 72 handlers at obj+0x5a0), ack-byte dedup @+0x200 |
| 00463370 / 00446800 | LAN checksum: zero u16@+4, XOR-fold all u16 words
  (odd trailing byte), compare low16. No capture to verify against yet |
| 005a0030 | Ogg voice loader (but 194/196 .sfo are OSND — routing TBD) |
| 0062f750 | **.spr file decoder**: 12 B hdr (`nframes,bpp,enc,flags,w,h,key,eh`),
extra-hdr, 2×`u16[nframes]` tables, raw (enc 0) / RLE (enc 1, bpp 1 or 2) /
interleaved (enc 2, frames.spr only), 256×3 B palette if bpp==1, upload via
`0062e390` when caller arg != 0; `nframes>1` sets anim flag +0x123 |
| 0062ea00 | sprite buffer alloc (`w*h*nframes*bpp` + tables); `OUT OF MEM!!` |
| 0062e390 | sprite→texture atlas uploader (splits >200-texture sprites, `SPR3D%d`
textures, per-frame rects at +0x554); logs `SPRITE SIZE too large…` |
| 0062fcd0 | 3D font setup from sprite (zero-frame guard); caller `004344b0` = sprite
manager init (0x100-slot tables, glyph remaps) |
  SetCooperativeLevel(PRIORITY) via vtable +0x18, GetCaps +0xc, 0x80 voice
  slots, log `Sound init fail`) |
- Present call outstanding (replace with SDL in port; swapchain = `DAT_007c8b14`,
  device = `…8b8c`, context = `…8b88`, RTV = `…8b90`).
- Frame-tick callees decompiled (see "Frame-tick stage bodies"); left:
  `00640370` state-by-state walk, `0064c920`/`00638280`, Present call.
- Renderer-init caller `0064ebb0` not yet decompiled.
- D3D11 error strings (`…FAILED`, `d3d11.dll`) have no imm32 refs — fallback
  path addressing TBD, not important.
- One Ghidra run at a time: parallel runs fail with project LockException.
