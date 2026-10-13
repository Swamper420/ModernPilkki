# Render analysis — Propilkki 2 PLAY-screen 3D lake scene (clean-room)

Target: `extracted/ProPilkki2.exe` (x86, VS2022 D3D11). All addresses are image
VAs (`006489c0` = `0x006489c0`). Decompiled with a headless Ghidra project
(`/tmp/opencode/ghidra-render`, base `ghidra-proj`, Ghidra 12.1.2); scratch dumps
in `/tmp/opencode/scratch-render/` (not committed). Float/i32 constants read with
`tools/peconst.py`. Analysis only — no `src/` was modified.

Claims are backed by a decompiled address. Inferences are marked *(inferred)*,
anything not yet decompiled is **open**.

---

## 0. Executive map — where the lake scene actually is

The screen-registry name `MENU_PLAY` (screen id `0x15`, vtable `00741750`,
frame `005cfca0` = slot+8) is **not** the lake renderer. `005cfca0` is a menu
state machine that only picks the next screen/mode (it writes `007c40b8`, calls
`00630790(nextId)`, and never touches the D3D context). The 3D lake is drawn by
the **in-game mode states `0x14` / `0x15`** in the frame governor `00640370`
(see `docs/decomp-notes.md`).

One frame, render half (`00605eb0` → confirmed order from decomp-notes):

```
006499e0  render latch on
006440b0  letterbox bars (aspect from 0061e400)
00640370  governor + game-mode state machine
   state 0x14 (single-player play) / 0x15 (net/replay play):
      0063ca80  input/command dispatch (5.5 KB)
      00638dd0  >>> RENDER THE WORLD + HUD <<<
   state 0x15 enter: 0064a330 screenshot, exit: 0064d9b0
00652940  UI rect fills (RGB565) + clears
[0062bc0 clear?] 00638280  text/sprite flush (if 7a6b51)
0064c920  render latch off
00649810  Present: context->Flush (ctx+0x1bc), swapchain->Present(1,0) (+0x20)
```

`00638dd0` (2.1 KB) is the play-frame draw entry. Verified body highlights:

* `DAT_007c849c = 00438a90()` — the **local player/angler** entity (position at
  `+0x4ac/+0x4b0`, look at `+0x324/+0x328`, model handle at `+0x330`).
* calls `004a5d00(0x10, *DAT_007c8948, DAT_007c8948[1], DAT_007c8948[2], …)`
  → **the 3D world draw** (camera ints in `DAT_007c8948`).
* plus HUD subsystems: HUD layout `00416c0`/`004168c0`, panels `004bc9e0`/
  `004bd330`/`00494eb0`/`00638a70`/`006388f0`, text flush `0062ead0`,
  `004a5d00` is the only 3D world pass (one call per frame at
  `00638dd0+0x…` with `param_1 = 0x10`).
* top-level `00638dd0` runs *after* `0063ca80` (update+input) in the same
  RENDER block, before Present.

### 0.1 `004a5d00` — world render (the big one, 2.7 KB)

`thiscall(this = world)`. Iterates `this+0x15178` world objects (visibility flags
at `this+0x240c8`, per-object model handles at `+0x240a4/+0x240ac/+0x240b0/
+0x240b4/+0x240bc`), plus:

| callee | role (verified by body) |
|---|---|
| `004a67b0` | pre-pass per object (shadow/reflection? uses `DAT_007c7130` bit tests) |
| `004a84a0` | echo/ripple strips along 4 screen edges (`0x24078` array) |
| `004a7030` | draw entity sub-list (`*param_2` count) via `0040afd0` / `00463d40` |
| `004a73c0` | draw "landing net / flies" (`0x240dc` list) via `0040afd0` |
| `004a69e0` | draw "bobber/float" (`0x240e0` list) via `0040afd0` |
| `004a6bf0` | animated water/weed overlay (`004107d0` tint + `0040afd0`) |
| `004a80b0` | **fishing line** — samples N points from A to B, draws a dotted strip (`param_7` = style 0..5) |
| `004a8d90` | **shadows** of local player/fish (4 quadrant samples) |
| `004a2aa0` | build dynamic line/fish geometry buffer + indices (`0064dfe0`/`0064ce70`) |
| `004a7660` | **build terrain vertices each frame** from depth grid (`0x151b4` cells) |
| `0040afd0` | draw one `.obj` model instance (world transform) |
| `0040b0b0` | **draw one `.obj` model's sub-meshes** (+ camera setup) |
| `0040e560`/`0040ac60`/`0040e260` | terrain cell raster (screen-space, `006487f0`) |

`00416ad0(x,y,z,rx,ry,rz)` is called immediately before each `0040afd0` — it fills
a 6-float transform struct (`[0..3]=x,y,z` as int→float, `[3..5]=rot`). It is the
per-draw **model matrix input**, the GPU matrix is then built by `00599d40`.

---

## 1. D3D11 object map (verified offsets)

Only **one** static D3D import exists (`objdump -p`): `D3D11CreateDeviceAndSwapChain`
(thunk `0066139d`). Every other D3D call goes through COM vtables, so there is no
import thunk to grep; offsets below are derived by matching call signature +
argument immediates against the D3D11 SDK vtable order, and cross-checked
(e.g. `+0x84` called with `(1, &RTV, DSV)` = `OMSetRenderTargets`, `+0x1bc` =
`Flush`). `D3DCompile` is **absent** → all shaders are precompiled bytecode blobs
(§3).

### 1.1 `ID3D11Device` (`DAT_007c8b14`) vtable offsets used

| off | method |
|---|---|
| `0x0c` | CreateBuffer |
| `0x14` | CreateTexture2D |
| `0x1c` | CreateShaderResourceView |
| `0x24` | CreateRenderTargetView |
| `0x28` | CreateDepthStencilView |
| `0x2c` | CreateInputLayout |
| `0x30` | CreateVertexShader |
| `0x3c` | CreatePixelShader |
| `0x50` | CreateBlendState |
| `0x54` | CreateDepthStencilState |
| `0x58` | CreateRasterizerState |
| `0x5c` | CreateSamplerState |

### 1.2 `ID3D11DeviceContext` (`DAT_007c8b88`) vtable offsets used

| off | method | first seen at |
|---|---|---|
| `0x1c` | VSSetConstantBuffers | `006489c0`, `00648d80`, `00648f60` |
| `0x20` | PSSetShaderResources | `006489c0` |
| `0x24` | PSSetShader | `006489c0` |
| `0x28` | PSSetSamplers | `00648f60`, `006508c0` |
| `0x2c` | VSSetShader | all draw wrappers |
| `0x30` | DrawIndexed | `006489c0`, `00648d80`, `00648f60`, `00649370`, `00649a30` |
| `0x34` | Draw | `00649540` |
| `0x40` | PSSetConstantBuffers | `006489c0`, `00649370` |
| `0x44` | IASetInputLayout | all |
| `0x48` | IASetVertexBuffers | all |
| `0x4c` | IASetIndexBuffer | all |
| `0x50` | DrawIndexedInstanced | `00648d80` |
| `0x60` | IASetPrimitiveTopology | `006489c0` (`4`=TRI, `2`=LINELIST, `5`=STRIP) |
| `0x84` | OMSetRenderTargets | `0064c480` |
| `0x8c` | OMSetBlendState | `0064a790`, `0064ebb0` |
| `0x90` | OMSetDepthStencilState | `00650710` |
| `0xac` | RSSetState | `0064fdf0`, `00649540`, `00649a30` |
| `0xb0` | RSSetViewports | `0064ff60` |
| `0xb4` | RSSetScissorRects | `0064ff60` |
| `0xc0` | UpdateSubresource | constant-buffer uploads (`0064f470`, `00650380`, `00650910`, `006507b0`, `00648f60`, `00649370`, `00649a30`) |
| `0xc8` | ClearRenderTargetView | `0064a530` |
| `0xd4` | ClearDepthStencilView | `0064a5f0` |
| `0x1bc` | Flush | `00649810` |

> Note: earlier project notes labelled `0064a530` = OMSetRenderTargets and
> `0064a5f0` = ClearRenderTargetView; that was off by one. `0064a530` calls
> `ctx+0xc8` with `(RTV, &color4)` = **ClearRenderTargetView**; `0064a5f0` calls
> `ctx+0xd4` with `(DSV,1,1.0f,0)` = **ClearDepthStencilView**.

### 1.3 `IDXGISwapChain` (`DAT_007c8b8c`)

| off | method |
|---|---|
| `0x0c` | IDXGIObject::SetPrivateData (`0064cf10`) |
| `0x20` | Present (`00649810`, args `1,0`) |
| `0x24` | GetBuffer (`0064c480`) |
| `0x30` | GetDesc (`0064ea70`) |
| `0x34` | ResizeBuffers (`0064ea70`) |

### 1.4 Globals

| global | role |
|---|---|
| `DAT_007c8b14` | `ID3D11Device*` |
| `DAT_007c8b8c` | `IDXGISwapChain*` |
| `DAT_007c8b88` | `ID3D11DeviceContext*` (immediate) |
| `DAT_007c8b90` | back-buffer RTV |
| `DAT_007c8b9c` | depth-stencil DSV |
| `DAT_007c8b94` | depth-stencil texture |
| `DAT_007c8ba0`/`ba4`/`ba8`/`bac`/`bb0`/`bb4` | 6 × `ID3D11VertexShader*` |
| `DAT_007c8bb8`/`bbc`/`bc0`/`bc4`/`bc8` | 5 × `ID3D11PixelShader*` |
| `DAT_007c8bcc`/`bd0`/`bd4`/`bd8`/`bdc`/`be0` | 6 × `ID3D11InputLayout*` |
| `DAT_007c8bf4`/`bf8`/`bfc`/`c00`/`c04`/`c08`/`c0c`/`c10` | constant buffers (§4) |
| `DAT_007c8be4`/`be8`/`bec`/`bf0` | small fixed vertex/index buffers (§4) |
| `DAT_007c8c1c`/`c20`/`c24`/`c28` | 4 × sampler state |
| `DAT_007c8c2c` | blend state |
| `DAT_007c8c30`/`c34`/`c38`/`c3c`/`c40` | 5 × depth-stencil state |
| `DAT_007c8c44`/`c48` | 2 × rasterizer state |
| `DAT_007c8b78`/`b7c` | viewport width/height (float) |
| `DAT_007c8b80`/`b84` | render-target width/height (float) |
| `DAT_007c8af8`/`afc`,`DAT_007c8b00`/`b04` | letterbox offset X/Y (`0064ff60`) |
| `DAT_007a6bb7` | "context state dirty" flag (set by draw wrappers) |
| `DAT_007c696c` | render-enable latch (`006499e0`) |
| `DAT_007c8a50` | currently bound PS SRV (texture) |
| `DAT_007c8a54[0..0xb]` | render-state mirror table (see `0064fdf0`) |
| `DAT_007c8a80` | vertex-layout selector (0 = static 0x20, 1 = deform 0x24) |
| `DAT_007c8a84`/`a88`/`a8c`/`a90` | camera/projection matrix cache keys |
| `DAT_007c8abc`/`ac4`..`ad0` | global tint (RGBA) + tint-enable |
| `DAT_007c8ab0`,`7c8a70`,`7c8a74` | depth-stencil state selector inputs |
| `DAT_007c8a68`/`a78` | sampler selector inputs |

---

## 2. Renderer init / restore chain

```
0064ebb0  device+swapchain (re)init  (called from WndProc "Reset DX" and start)
  0061edd0/0061e680..   pick window size mode, log "Window size selected: %dx%d."
  UpdateWindow/ShowWindow(007c4d60)
  0064ad60  D3D11CreateDeviceAndSwapChain (feature levels 11_1→9_1, drivers 1,5,2)
  0064c480  GetBuffer → CreateRenderTargetView (dev+0x24) → CreateTexture2D depth
            (dev+0x14, DXGI 0x2d? R32_TYPELESS/D24) → CreateDepthStencilView (dev+0x28)
            → OMSetRenderTargets (ctx+0x84)
  0064a790  font/2D? no: full pipeline init:
     0064b560   create 6 VS + 5 PS + 6 input layouts (see §3)
     0064b350   create small fixed buffers (sizes 0x60,8,4,0xc) → 7c8be4/be8/bf0/bec
     0064aa70   CreateBlendState(0x50) + 5×DepthStencilState(0x54) + 2×RasterizerState(0x58)
                + 4×SamplerState(0x5c)
     CreateBuffer ×7 (sizes 0x50,0x40,0x40,0x600,0x40,0x50,0x10) → constant buffers
       → ctx+0xc0 UpdateSubresource(0x10-byte CB, zero) → OMSetBlendState(0x8c,c2c)
       → 00650710 (depth state) → 006507b0 (tint CB) → 006508c0 (samplers)
  0064f150  re-upload textures (Sleep(1) every 10)
  0064cf10  swapchain SetPrivateData (mode struct) + backbuffer Map/Unmap copy
  0064ff60  set viewport (ctx+0xb0) + scissor (ctx+0xb4), compute 7c8b00/b04
  0064a530(1) ClearRenderTargetView, 0064a5f0 ClearDepthStencilView
  OMSetBlendState(0x8c, c2c, factor, 0xffffff)
0064ea70  on WM_SIZE: ResizeBuffers(swapchain+0x34), GetDesc(+0x30) → 7c8b78/b7c,
          recreate RTV/DSV (0064c480), viewport (0064ff60)
0064e380  teardown: release RTV/DSV, clear asset pool, free 7c8bf4..
0064be00  texture upload / CreateTexture2D (DXGI 0x56/0x55/0x73/0x57, pow2 cap 0x200)
0064cf10  swapchain private data + Map copy   (0x38 Map, 0x3c Unmap)
0064a330  screenshot capture
0064d0c0  Map/Unmap copy helper
```

`0064a790` = the one place the whole pipeline is built; `0064ebb0` re-runs it
after device loss (`00649810` returns 0 → `Restoring DirectX..`).

---

## 3. Shaders + input layouts (precompiled; no D3DCompile)

`0064b560` calls `CreateVertexShader(dev+0x30, blob, size)` /
`CreatePixelShader(dev+0x3c, blob, size)` / `CreateInputLayout(dev+0x2c, desc,
count, blob, size, &layout)` with these `.rdata` bytecode blobs. Sizes are the
immediate lengths passed to the device.

| blob | size | type | stored to | input layout | stored to |
|---|---|---|---|---|---|
| `0074c370` | `0xa88` | VS | `7c8ba0` | `7c8bcc` | static mesh |
| `0074dce8` | `0x7e4` | VS | `7c8ba8` | `7c8bd8` | 2D/terrain quad |
| `0074e4d0` | `0x9ec` | VS | `7c8bac` | `7c8bdc` | point/line |
| `0074eec0` | `0x7c4` | VS | `7c8bb0` | `7c8be0` | 2D/terrain quad (alt) |
| `0074f688` | `0xab0` | VS | `7c8bb4` | `7c8bd4` | instanced mesh |
| `0074cdf8` | `0xeec` | VS | `7c8ba4` | `7c8bd0` | deform mesh |
| `00750138` | `0xdc4` | PS | `7c8bb8` | — | mesh PS #1 |
| `00750f00` | `0x488` | PS | `7c8bbc` | — | mesh PS #2 (tint, selected by `7c8abc==1`) |
| `00751388` | `0x6c4` | PS | `7c8bc0` | — | 2D/terrain/sprite PS |
| `00751a50` | `0x448` | PS | `7c8bc4` | — | line/rect PS |
| `00751e98` | `0x710` | PS | `7c8bc8` | — | textured-2D PS |

### 3.1 Input layout element descriptors (from `0064b560`)

DXGI formats: `6`=R32G32B32_FLOAT, `0x10`=R32G32_FLOAT, `0x29`=R32_FLOAT,
`2`=R32G32B32A32_FLOAT, `0x2a`=R32_UINT.

* **Layout A** `7c8bcc` (static `.obj` mesh, stride **0x20**):
  POSITION f3 @0, NORMAL f3 @0xc, TEXCOORD f2 @0x18.
* **Layout F** `7c8bd0` (deform mesh, stride **0x24**):
  POSITION f3 @0, NORMAL f3 @0xc, TEXCOORD f2 @0x18, TEXCOORD1 `R32_UINT` @0x20.
  (`006489c0` picks A vs F via `DAT_007c8a80`; stride `0x20` vs `0x24`.)
* **Layout E** `7c8bd4` (instanced, slot0 stride **0x20**, slot1 stride **0x40**):
  POSITION f3 @0, NORMAL f3 @0xc, TEXCOORD f2 @0x18 (per-vertex);
  TEXCOORD1..4 as `R32G32B32A32_FLOAT` @0,0x10,0x20,0x30, slot1, **per-instance,
  step 1** (4×float4 = 1 matrix per instance). (`00648d80` binds both slots.)
* **Layout B** `7c8bd8` (2D/terrain quad, stride **0x18**):
  POSITION f2 @0, TEXCOORD f2 @8, DEPTH f @0x10, ALPHA f @0x14.
* **Layout D** `7c8be0` (same shape as B, alt VS), stride **0x18**.
* **Layout C** `7c8bdc` (point/line, stride **4**): POSITION f1 @0.

---

## 4. Buffers, constant buffers, render states

### 4.1 Constant buffers (all created `0064a790`, device+0x0c, USAGE DEFAULT + INIT)

| CB | size | bind slot (draw wrapper) | written by | content |
|---|---|---|---|---|
| `7c8bf4` | `0x50` | VS slot0 | `00650910` (+`0064f470`) | camera/screen projection matrix + viewport params (from cached `7c9570..`, letterbox `7c8b00/b04`) |
| `7c8bf8` | `0x40` | VS slot1 | `0064f470` (param_4) | camera matrix B (view) |
| `7c8bfc` | `0x40` | VS slot2 | `00650380` (param_1) | per-object **world matrix** |
| `7c8c00` | `0x600` | VS slot7 | `00650380` (param_2 list) | **world-matrix array** (64 matrices; batched/skinned) |
| `7c8c04` | `0x40` | VS slot3 + PS slot3 | `00648f60`/`00649370`/`00649a30` | per-quad rect/tint params |
| `7c8c08` | `0x50` | VS slot5 | `00649540` | textured-triangle params |
| `7c8c0c` | `0x10` | VS slot4 | `0064f610`, `0064fdf0` | fog/depth plane vec4 |
| `7c8c10` | `0x40` | PS slot6 | `006507b0` | global tint/lighting (4×float4) |

### 4.2 Small fixed vertex/index buffers (created `0064b350`)

| buf | size | used as |
|---|---|---|
| `7c8be4` | `0x60` | 2D quad/rect VB (stride 0x18), bound by `00649370`/`00649a30` |
| `7c8be8` | `0x08` | index buffer (R16) for `00649a30` |
| `7c8bf0` | `0x04` | index buffer (R16) for `00649370` |
| `7c8bec` | `0x0c` | 3-vertex VB (stride 4) for `00649540` |

Dynamic geometry (fishing line, fish schools, terrain batch) is allocated at
runtime through the asset pool: `0064dfe0(kind,count,flags)` creates+registers a
buffer (`kind` fixes the stride: 0→0x20, 1→0x24, 2/6→0x18, 3/5→1, 4→2), and
`0064ce70(id,&outSize)` **maps** it (returns CPU pointer; outSize=size). Layout-B
terrain/line buffers use `kind 2` (stride 0x18) or `kind 6`.

### 4.3 Render-state objects (`0064aa70`) and setters

| state | globals | selected/used by |
|---|---|---|
| blend | `7c8c2c` | `0064a790`, `0064ebb0` (OMSetBlendState ctx+0x8c) |
| depth-stencil ×5 | `7c8c30,c34,c38,c3c,c40` | `00650710` picks by `7c8ab0`(2/4)/`7c8a74`, restores from `7c8a70` |
| rasterizer ×2 | `7c8c44,c48` | `0064fdf0(id 0)` (ctx+0xac) |
| sampler ×4 | `7c8c1c,c20,c24,c28` | `006508c0` (ctx+0x28 slot0), keyed by `7c8a68`/`7c8a78` |

`0064fdf0(id, on)` is the master state-flag setter (table `7c8a54`):
`0`→rasterizer, `5`/`9`→sampler, `6`→global tint CB (`006507b0`),
`7`/`8`→depth-stencil (`00650710`), `0xb`→dirty flag. It is called ~30× per model.

### 4.4 Viewport / letterbox (`0064ff60`)

`0064ff60(scaleX,scaleY,tileW,tileH,flag)` computes letterbox offsets
`7c8b00/b04`, `RSSetViewports` (ctx+0xb0), `RSSetScissorRects` (ctx+0xb4) and
re-uploads CB0 (`00650910`). `006440b0` is the per-frame caller that draws the
two white bars (`0040ab00`).

---

## 5. Draw wrappers (the whole 3D/2D vocabulary)

These six are the only things that ever touch the context's IA/shaders.

| fn | what it draws | VB/IB | layout/VS/PS | primitive | index count |
|---|---|---|---|---|---|
| `006489c0` | one **mesh sub-mesh** (`param_1`=VB id, `param_2`=IB id, `param_3`=count, `param_4`=topology: 0 TRI/1 LINELIST/2 STRIP, `param_5`=CB2 override/-1) | 1 VB stride 0x20 (or 0x24 if `7c8a80`) | A (or F) / VS A (or F) / PS `7c8bb8`, or `7c8bbc` if `7c8abc==1` | `IASetPrimitiveTopology` | `param_3` |
| `00648d80` | one **instanced sub-mesh** (binds 2 VB: slot0 stride 0x20, slot1 stride 0x40) | 2 VB + IB | E / VS E / PS `7c8bb8`/`7c8bbc` | per `param_6` | via `DrawIndexedInstanced` (ctx+0x50) |
| `00648f60` | **2D/terrain textured quad(s)** at pixel `(param_5,param_6)`, colour `param_7`, alpha `param_8`; params normalized by viewport inside | 1 VB stride 0x18 | D / VS D / PS `7c8bc0` | TRI/LINE/STRIP (`param_4` 0/1/2) | `param_3` |
| `00649370` | **rect outline** (x1,y1,x2,y2, colour) | fixed VB `7c8be4` + IB `7c8bf0` | B / VS B / PS `7c8bc4` | LINELIST (2) | 2 (×2 if `param_7` flip) |
| `00649540` | **textured triangle** (3 manual verts, UVs) | fixed VB `7c8bec` | C / VS C / PS `7c8bc4` or `7c8bc8`+tex | TRIANGLELIST | `Draw(3)` |
| `00649a30` | **screen sprite blit** (axis-aligned quad from a `.spr` frame; clips to `param_1+0x11c/0x120`, frame `+0x158/0x15c`, SRV `+0x118`) | VB `7c8be4` + IB `7c8be8` | B / VS B / PS `7c8bc0` | TRISTRIP (5) | 4 |

Mesh **buffer ids** (`param_1`/`param_2`) are resolved by `0064cab0(id,&idxCount)`
(→ `00592270(id)` in the asset pool `7c8a48`; returns `+4`=`ID3D11Buffer*`,
`+0x1c` = index count). Terrain batch: `006487f0(v0,v1,v2,uv...,alpha)` appends 3
24-byte layout-B vertices to buffer `7a6bbc`; `0064c8a0` flushes via `00648f60`.

`0064fef0(tex)` = bind PS SRV (`7c8a50`, ctx+0x20). `0064fd50`/`0064faa0` =
global tint / fog; `006507b0` uploads them to CB `7c8c10`.

---

## 6. Camera + matrices

* `00416ad0(x,y,z,rx,ry,rz)` → writes the pending transform (6 floats).
* `00599d40(pos x,y,z, sx,sy,sz, out16, mode, xform)` builds the **world matrix**
  (scale applied as `* DAT_006d0528` = 1/180) into a 64-byte buffer; `00598460`,
  `00598860`, `00598dc0` are row-major 4×4 multiplies (verified by body:
  `out = A*B`, `out = A*B*C`, `out = A*B*C*D`).
* `0040b0b0` (model draw) rebuilds the world matrix at `model+0x800`, then calls
  `00650380(worldMatrix,0,0)`.
* `00650380(mat16, count, list)` **transposes & uploads** `mat16` to VS CB **2**
  (`7c8bfc`) and, when `count>0`, uploads `count` matrices to VS CB **7**
  (`7c8c00`) as a flat array (dirty-checked against `7c8b18`). `0064f470` also
  calls `00650910` to fold the camera matrix + viewport into CB **0**.

Camera source: `0040b0b0` calls
`0064f470( FUN_00416850(FUN_00416870(FUN_00416840(FUN_00416860(model+0x5a0)))) )`
— i.e. the camera lives in the **model/renderable object at `+0x5a0`** and its
combined matrix is cached (`7c8a84/a88/a8c/a90` keys, matrix copies at
`7c9570..7c95ac`, `7c9510..7c954c`). **open:** exact view vs projection assignment
(the two uploaded matrices are verified; which is view and which is projection is
UNVERIFIED) and the fov/near/far builder (not yet located).

`0064f610(enable, planeFloat4)` computes the per-draw fog/depth vec4 into VS CB
**4** from the camera matrix; it uses `DAT_007c8a94/98/9c/a0` (camera row copies)
and `7c8b7c` (viewport height).

---

## 7. Model (`.obj`) path

* Loader `0040d320` (already documented in `docs/file-formats.md`): reads
  `"Pro Pilkki object"` (v1) or `"Pro Pilkki Object"` (v2) into the model object
  (`+0x8c8` sub-mesh count, `+0x987` names, geometry sub-struct `+0xd6c`: `+0x330`
  vertex count, `+0x368` vertex array (stride 0xc = 3×i32 world units), `+0x378`
  face count, `+0x37c` face array (stride 0x34), animations at `+0xd70..`).
* Manager `004070e0` allocates the geometry pools, calls `0040d320`, builds
  materials/texture paths, and initialises the animation/state tables.
* Draw `0040afd0(model, x,y,z, rx,ry,rz, target)` normalises a rotation vector
  (`model+0xd50/d58/d60`) then calls **`0040b0b0`**.
* `0040b0b0(model = ECX, 9 stack args)` = per-model render:
  1. `0064fdf0` state sets (rasterizer/sampler/tint/depth per `model+0x724`
     mode switch: 0..4 = visibility/LOD cases).
  2. `00599d40(...)` builds `model+0x800` world matrix (blend of `model+0x7bc`,
     `model+0x800`, `model+0xd40`, identity).
  3. `00599d40(...)` + `00650380(model+0x800, …)` uploads world matrix; camera via
     `0064f470`/`0064f610`.
  4. Loop `model+0x8c8` sub-meshes (`piVar19` stepping the arrays at `model+0x38`
     and `model+0x8c`): `0064fef0(texture)` then `006489c0` (or `00648d80` when an
     instance/CB override `model+0x894 != -1`).
  5. Extra passes: overlay `model+0xb4`, x-ray/ghost `model+0xb8`, tint `model+0xc0`.

`006489c0`'s `param_5` (texture/CB2 override, `-1` = leave `7c8bfc`) is passed
`0xffffffff` from `0040b0b0`; the `model+0x894`/`0x898` path picks `00648d80`
(instanced) instead.

---

## 8. Terrain path (CPU-projected grid)

Verified chain:

```
004a7660   per frame: walk depth grid (world+0x151b4 cells, size 0x15248 array),
           compute screen/UV/depth/alpha per cell vertex into mesh+0xd6c+0x374
           (12-byte entries), fill cell index quads, call 0040e5b0 + 0040bb60
0040e5b0   per cell: FUN_0040e260 or 0040ac60 for the 3/4 body indices
0040e260   takes 3 grid vertices (i0,i1,i2) from +0xd6c+0x374, computes a
           back-face normal in fixed point, rejects near-degenerate/steep cells,
           selects material by param_2[5], scales UVs by 0x8cc[slot],
           calls 006487f0(3 positions + 6 UVs + colour, 0x3a83126f=0.001f)
0040ac60   packed-RGB variant → 006491b0 → 00649370 (rect)
0040e560   inline depth-copy variant
006487f0   append 3 layout-B verts (x,y,u,v,depth,alpha) to buffer 7a6bbc
0064c8a0   flush batch → 00648f60 (DrawIndexed) → back to 006489c0 state
```

So the lake floor is **not** a static mesh: world vertices are generated from the
DEPTHMAP grid every frame, classified into depth bands, and rasterised as
screen/world-projected layout-B triangles (VS `0074dce8`/`0074eec0`). The
`DAT_006d0590`/`006d05b8` tests in `0040e260` cull cells whose normal is too
steep; `004a7660` additionally clamps vertices into the viewport
(`0061ec90`/`0061ecc0`).

### 8.1 Cell record + cull (ported: `src/render3d_terrain.hpp`)

`0040e260`'s face record (`geometry+0x37c`, stride `0x34`): `@0` three `i32`
vertex indices; `@0x14` material byte; `@0x18` six `i32` UVs (3 pairs); `@0x30`
flags (bit0 skip-cull, `0x1c` packed-RGB variant). The position array
(`+0x374`) is `i32` x,y,z at stride `0xc` per vertex.

Cull (integer cross, exact for screen-sized coords; the original round-trips the
x deltas through `float`):
`n = cross(B-A, C-A)`; reject if all 3 `z < -0x2bd` (-701), if `n == 0`, or if
`nz / |n| > DAT_006d05b8` (0.0). The `|n|_max > 1000` (`DAT_006d0590`)
normalisation is sign-neutral, so it never changes the decision.

Append (`006487f0`): 3 layout-B verts `x,y` (the projected ints), `u,v =
uv[material-scaled]`, `depth = 0x3a83126f` (0.001), `alpha = param_7[3]`. Returns
`1` when the batch reached `DAT_007a6bb8` -> caller flushes via `0064c8a0`
(`DrawIndexed`, resets the count). `2` = already full, `0` = appended, not full.

### 8.2 Camera constant buffers (ported: `src/render3d_camera.hpp`)

`00650910` builds CB0 (`0x50`): `transpose(camA)` (globals `7c9570..7c95ac`,
`0064f470`'s `param_2`) followed by a viewport vec4
`(2*offX/scrW, 2*offY/scrH, panX, panY)`, the first two rescaled by
`win/rtTarget` when the letterbox flag `7c8b08` is 0. `0064f470` also uploads
`transpose(camB)` (`param_4`) to CB1 (`7c8bf8`). The view-vs-projection
assignment of `param_2`/`param_4` is still open (the caller passes a matrix
derived from `model+0x5a0`; exact field map UNVERIFIED).

### 8.3 World-space scene (ported: `src/render3d_scene.hpp`)

Stand-in for `004a7660`, not a port: the DEPTHMAP grid becomes a static
world-space layout-A mesh (`BuildTerrainMesh`: x/z in +-128, y =
`kSceneDepthTable[cell*17/256]/2500 * zScale` downward so deep cells sit lower,
finite-difference normals, uv measured against the padded texture width) plus a
water-ramp texture (`BuildTerrainTex`, row pitch padded to a 256-byte multiple
for the Vulkan copy), drawn with a clean-room look-at + perspective (`ViewProj`,
NDC y-up, clip z 0..1). This is the standard GPU shape and reuses the layout-A
mesh pipeline; it exists so the lake can be seen in 3D before the exact CPU
projection is decompiled.

### 8.4 In-game bridge (`src/render3d_bridge.hpp`, `pilkki --3d`)

`--3d` renders the it8 scene offscreen with SDL_GPU (the device never claims the
window), downloads it each frame and updates an `SDL_Texture` that the existing
SDL_Renderer blits as the PLAY background; the HUD, menus and overlays stay on
SDL_Renderer. The 2.5D angler overlay is skipped in 3D mode (its projection does
not match the perspective view); the angler is re-drawn at a fixed screen spot.
`ponytail:` full-frame GPU->CPU readback per frame -- replace with a real
SDL_GPU present path once the HUD moves off SDL_Renderer.

---

## 9. Draw order (one play frame, verified skeleton)

```
006499e0  latch
006440b0  letterbox bars (2 white rects via 0040ab00)
00640370  state 0x14/0x15:
  0063ca80  update + input
  00638dd0:
    [HUD pre] DAT_007c849c = local player
    FUN_00644220 / FUN_00416b20 / FUN_0063c790 …  (HUD layout, scores)
    004168c0 / 00416ad0 / 4bc590  (text/HUD positioning)
    *** 004a5d00  world render (below) ***
    HUD panels:  004bc9e0 / 004bd330 / 00494eb0 / 00638a70 / 006388f0
    0062ead0  text/sprite flush
  world 004a5d00 order:
    per-object prep 004a67b0
    sky/water 0064a5f0(clear depth) + 004a67b0 / 004a84a0 (ripples)
    terrain build 004a7660  → cells 0040e5b0/0040ac60/0040e260 → 006487f0 → 0064c8a0
    entities 004a7030, 004a73c0, 004a69e0 (net/flies/float) via 0040afd0
    fish/lines 004a80b0 + 004a2aa0 (dynamic buffers)
    models 0040afd0 → 0040b0b0 (per .obj: states, world matrix, sub-meshes)
    004a8d90 shadows, 004a6bf0 weed overlay
    local player 004a5b10 (rod/hand model at world+0x28b84)
00652940  UI rect fills (menus, dialogs) + clears
00638280  text flush
0064c920  latch off
00649810  Present(1,0)
```

3D scene and 2D HUD share the same context/back buffer; the world is drawn into
the full viewport and the HUD (`00652940`/`00649a30` sprites, `0062ead0` text)
is composited on top afterwards. Depth: world uses `7c8c30..c40`; 2D passes bind
rasterizer `7c8c48` (no cull) and depth-state variants for alpha/ghost.

---

## 10. Constants cheat-sheet (from `tools/peconst.py`)

| const | type | value | used for |
|---|---|---|---|
| `DAT_00777850` | f32 | `1.0` | normalising pixel↔UV (`x/width`) |
| `DAT_00777888` | f64 | `1.0` | unit length / normalise |
| `DAT_0077782c` | f32 | `0.5` | rounding bias |
| `DAT_00777878` | f64 | `0.5` | rounding bias |
| `DAT_006d0524` | f32 | `1/255` | byte colour → float |
| `DAT_006d0528` | f32 | `1/180` | model scale (world units→engine) |
| `DAT_006d0534` | f32 | `π/180` | degrees→radians (rot) |
| `DAT_006d0548` | f32 | `0.001` | UV/pos epsilon |
| `DAT_006d051c` | f32 | `0.001` | ripple epsilon |
| `DAT_006d0530` | f32 | `1/63` | packed-RGB green step (`0040ac60`) |
| `DAT_006d053c` | f32 | `1/31` | packed-RGB red/blue step |
| `DAT_006d0590` | f64 | `1000.0` | ms/s; terrain cull normal threshold |
| `DAT_006d05b8` | f32 | `0.0` | terrain cull threshold |
| `DAT_006db094` | f32 | `0.1` | ink/ripple scale |
| `DAT_006dc5c8` | f64 | `0.0025` | line-dot spacing |
| `DAT_006dc6b0` | f64 | `400.0` | line-dot count scale |
| `DAT_006d9f88` | f64 | `0.4` | pause fade scale |
| `DAT_006dc668` | f64 | `1.2` | pause vertical scale |
| `DAT_006d1638` | f64 | `1.5` | pause vertical scale (alt) |
| `DAT_006d06b4` | f32 | `0.2` | ripple/weed scale |
| `DAT_006db098` | f32 | `0.4` | echo strip offset |
| `DAT_006d9f08` | f32 | `0.6` | echo strip offset |
| `DAT_0077783c` | f32 | `0.8` | echo strip offset |
| `DAT_006dba08` | f64 | `90.0` | player heading offset |
| `DAT_006d12d0` | f64 | `0.1` | time scroll |
| `DAT_007a0478` | f64 | (~) `1.0`? | line vertical start (see §4 of sim.md) |

---

## 11. Key data structures (offsets verified by decompile)

**World / scene object** (`004a5d00` this):

| off | field |
|---|---|
| `+0x15178` | world object count |
| `+0x1517c` | world grid width (for `%`/`/` indexing) |
| `+0x15180`,`+0x15190` | shadow/grid tables |
| `+0x151b4` | terrain/water cell count per frame |
| `+0x1518c` | mesh object used for terrain (`+0xd6c` geometry ptr) |
| `+0x2409c`/`a0`/`a4`/`ac`/`b0`/`b4`/`bc`/`c4` | per-object model/tex handles |
| `+0x240c8` | per-object visibility flags (stride `0x1c`: `+0x10` visible, `+0x14` state) |
| `+0x240dc`/`e0`/`24078`/`151c8`/`15204` | sub-object lists (net/float/weeds) |
| `+0x28b84` | local rod/hand model handle |
| `+0x1ef60`/`1ef64` | 3D markers |
| `+0x1ee74..1ee98` | echo/ripple runtime state |

**Model object** (`0040b0b0` this, layout from `004070e0`):

| off | field |
|---|---|
| `+0x000` | "loaded" byte (nonzero → drawn) |
| `+0x004` | chain/parent pointer (`!= 0` → x-ray pass) |
| `+0x008` | vertex/index buffer id (→ `0064cab0`) |
| `+0x010` | index-buffer id (`piVar19[-10]`) |
| `+0x038`, `+0x08c`, `+0x0bc`/`0xc0`/`0xc4` | sub-mesh tables (tex/IB/count, stride by sub-mesh) |
| `+0x0b4`,`0x0b8`,`0xc0`,`0xd4` | LOD/extra-pass handles |
| `+0x5a0` | **camera/render sub-object** |
| `+0x600` | "colour enable" |
| `+0x708`/`709`/`70a`/`70b`/`70c` | render feature flags (blend, tint, shadow, fog, alpha) |
| `+0x724` | draw-mode switch (0..4: opaque/alpha/ghost/shadow) |
| `+0x758`,`+0x75c`,`+0x760` | cutout/threshold params |
| `+0x7bc`,`+0x800`,`+0x778` | 4×4 matrices (model+anim+world) |
| `+0x880` | matrix source override (CB2 id) |
| `+0x894`/`0x898` | instanced draw selector + CB7 list |
| `+0x8c8` | sub-mesh count |
| `+0x8cc` | per-sub-mesh UV scale |
| `+0xd6c` | geometry sub-struct (vertex/face buffers, counts, anim) |
| `+0xd40`/`0xd48` | animation time/loop |
| `+0xd58..d60` | rotation vector (normalised by `0040afd0`) |
| `+0xd68` | alpha |

**Sprite/image object** (`00649a30` this): `+0x118` SRV, `+0x11c`/`0x120` texture
w/h, `+0x128` packed RGB, `+0x132..13c` explicit RGBA bytes, `+0x140` alpha,
`+0x144` depth, `+0x148..14b` flip/anim flags, `+0x154` chained frame,
`+0x158`/`0x15c` current frame cell.

**Asset pool** `DAT_007c8a48` (ctor `00597770`): `00592270(id)` → node
(`+0`=CPU data, `+4`=`ID3D11Buffer*`, `+0x14`=parent id, `+0x18`=kind,
`+0x1c`=index count, `+0x20`=refcount, `+0x24`=size). `0064dfe0` creates a
dynamic node, `0064ce70` maps it, `0064c610` releases.

---

## 12. Port plan (`src/render3d.*`, SDL3 / SDL_GPU)

### 12.1 What to implement (1:1 behaviour)

1. **Pipeline init** mirroring `0064a790`: 6 VS + 5 PS (use the *same* shader
   logic re-authored in HLSL/GLSL — the original DXBC blobs must NOT be shipped,
   clean-room). 6 input layouts exactly as §3.1. 8 constant buffers with the same
   sizes/slots. 5 depth-stencil, 2 rasterizer, 4 sampler, 1 blend state. Small
   fixed quad/point VBs (0x60/8/4/0xc) + a dynamic layout-B buffer.
2. **Camera**: port `004a5d00`'s camera extraction (`model+0x5a0`), the matrix
   multiplies (`00598460`/`00598860`/`00598dc0`), `00599d40` world-matrix build,
   `00650380` world/CB7 upload + dirty cache, `0064f470`/`00650910` camera CBs,
   `0064f610` fog vec4, `0064ff60` viewport/letterbox. **Resolve the view vs
   projection assignment first** (§6 open).
3. **Draw wrappers** `006489c0`/`00648d80`/`00648f60`/`00649370`/`00649540`/
   `00649a30` as thin functions over a `Ctx`-like API; keep the state mirrors
   (`7c8a54` table, `7c8a50` SRV, `7c8af4` topology, `7a6bb7` dirty flag) so
   redundant state changes are skipped hashing identically.
4. **Terrain** `004a7660`/`0040e260`/`0040ac60`/`0040e5b0`/`006487f0`/
   `0064c8a0`: regenerate the grid from the ported DEPTHMAP/TYPEMAP
   (`src/ves.hpp`) each frame, same cull (`006d0590`/`006d05b8`), same per-cell
   material selection and UV scaling, same layout-B vertex packing.
5. **Models** `0040d320` + `004070e0` + `0040afd0` + `0040b0b0`: reuse
   `src/obj.hpp`/`src/models.hpp`; upload sub-meshes to GPU buffers via the pool
   equivalent; keep the per-model state switch and camera.
6. **Frame wiring**: draw the world from `tick`/`state` mode 0x14 (port of
   `00640370`), then `00652940`-equivalent UI, then present. Letterbox first.

### 12.2 Where SDL3/SDL_GPU maps cleanly

* Swapchain/RTV/DSV/resize/device-lost (`0064ad60`,`0064c480`,`0064ea70`,
  `0064ebb0`,`00649810`): SDL_GPU `SDL_ClaimWindowForGPUDevice`,
  `SDL_AcquireGPUSwapchainTexture`, resize callback. 1:1 in spirit; exact D3D
  retry/feature-level logic is unnecessary.
* Shaders: SDL_GPU takes SPIR-V/DXBC/MSL; re-author the 11 shaders as HLSL and
  compile at build time (repo has no shader toolchain yet — add one, or ship
  SPIR-V). Input layouts become `SDL_GPUVertexAttribute` arrays.
* Constant buffers: `SDL_PushGPUVertexUniformData`/`PushGPUFragmentUniformData`
  replace Map/UpdateSubresource on CB slots 0/1/2/7 and PS6. Dynamic vertex/index
  buffers replace `0064dfe0`/`0064ce70`/`0064c8a0` (`SDL_UploadToGPUBuffer`).
* Depth-stencil / raster / sampler / blend objects map 1:1 to
  `SDL_GPUDepthStencilState` etc.

### 12.3 Where exact 1:1 is impossible / needs a knob

* **DXBC shader bodies** cannot be shipped (clean-room) → reimplement; any
  visual bit-exactness depends on the re-authored shaders. Keep the constants
  (§10) as-is.
* **CPU-projected terrain** (`006487f0`, screen/world layout-B) — reproduce the
  arithmetic; GPU float rounding may differ slightly on the min/max clamps in
  `004a7660` (`0061ec90`/`0061ecc0` = window w/h).
* **Matrix convention**: `00650380` transposes on upload → the original is
  row-major with D3D clip space (z 0..1, Y-down option via viewport). SDL_GPU is
  Vulkan-style (Y-down, z 0..1) if using SPIR-V — verify handedness and flip
  (`0064ff60` already flips letterbox for `local_31`).
* **Fixed-function quirks**: `clear color 0.75 grey` fallback, RGB565 UI colours
  (`00652940`), `.pcx`/`.spr` texture formats (0x56/0x55/0x73/0x57) → map to
  SDL formats; sRGB handling may need a flag.
* Device-loss restore path: SDL_GPU manages this; only the asset re-upload
  ordering (`0064f150`) is worth mirroring.

### 12.4 Suggested file split (no code in this task)

```
src/render3d.hpp      Ctx, resource pool, state mirrors, draw wrappers
src/render3d.cpp      pipeline init (0064a790/0064ebb0), wrappers
src/render3d_shaders  re-authored HLSL/GLSL + input layout tables
src/render3d_terrain.hpp  0040e260/006487f0 cell cull + append (it7; 004a7660
                          projection still open)
src/render3d_camera.hpp   00650910/0064f470 CB0+CB1 packing (it7)
src/render3d_scene.hpp    DEPTHMAP -> layout-A terrain mesh + camera (it8 stand-in)
src/render3d_bridge.hpp   offscreen GPU scene -> SDL_Texture PLAY bg (it9)
src/render3d_model.cpp    0040b0b0/0040afd0/camera
test_render3d.cpp     smallest checks: matrix mults, terrain cull, layout packing
```
Wire into `main.cpp` after the scene draws in the mode-0x14 path.

---

## 13. Open / UNVERIFIED

* **View vs projection** assignment of the two uploaded camera matrices
  (`0064f470` args), and the fov/near/far builder (not yet located).
* Exact semantics of `0064f470` param_1/param_2 (camera object `+0x5a0` fields)
  and the id roll-up `00416850/60/70/00416840`.
* `00650910` CB0 layout: **resolved** (it7) -> `transpose(camA)` + viewport vec4
  `(2*offX/scrW, 2*offY/scrH, panX, panY)`, rescaled by `win/rtTarget` when
  `7c8b08 == 0`; `0064f470` CB1 = `transpose(camB)`. See §8.2, ported in
  `src/render3d_camera.hpp`.
* The 11 shader blobs' internal constants/instruction semantics (DXBC not
  decompiled) — only their binding/pointers are known.
* `0064b350`'s four small buffers: exact init data and stride use per wrapper
  (stride values read from the `IASetVertexBuffers` args, sizes from CreateBuffer).
* `004a5d00` `this` identity (`DAT_007c8948` camera struct passed as args;
  world pointer not yet named) and the top-down map second `004a5d00` call.
* `006487f0` batch capacity (`DAT_007a6bb8`) is a runtime dynamic-buffer size,
  not a PE constant; the flush/return-code semantics are **resolved** (it7, §8.1)
  -> ported with the capacity as a parameter.
* `004a7660` per-frame projection (still open): the terrain cell array at
  `world+0x15248` (10 floats/cell), its initialiser, the frame delta
  `DAT_007c695c` / `(&DAT_00777a20)` trick, and the camera fields at
  `+0x1ee88/90/98`. The screen-position clamp uses `0061ec90/ecc0` (win w/h)
  and `0059db20`; the `0067d495` x87 calls are an unresolved trig/normalise pair.
  The integer cell **cull + append are ported** (§8.1) from the projected output.
* Water surface / sky: `0064a5f0` clears the depth buffer mid-frame; the actual
  water shader selection between PS `7c8bb8` and `7c8bbc` and the per-water
  constants are open.
* Sprite (`00649a30`) anim chain `+0x154` recursion semantics.
```
