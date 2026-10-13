// Iteration 3 of the SDL_GPU port of the PLAY-screen 3D scene: vertex formats,
// input-layout descriptors, the constant-buffer table, and the terrain vertex
// append. Pure data/packing -- no GPU pipelines, no shader creation.
//
// Ported 1:1 from the original (image VAs; see docs/render.md 3/3.1, 4.1, 4.2):
//   layouts + DXGI formats <- 0064b560 (D3D11_INPUT_ELEMENT_DESC table)
//   terrain append        <- 006487f0 (3 layout-B vertices per cell-triangle)
//   small fixed buffers   <- 0064b350 (0x60/8/4/0xc, bind flags VERTEX/INDEX)
//   constant buffers      <- 0064a790 (create order + sizes)
//   clear flush           <- 0064c8a0 -> 00648f60
//
// DXGI formt ids seen in 0064b560: 6=R32G32B32_FLOAT, 0x10=R32G32_FLOAT,
// 0x29=R32_FLOAT, 2=R32G32B32A32_FLOAT, 0x2a=R32_UINT.
#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>

namespace render3d {

// --- vertex formats (stride = value in 006489c0's IASetVertexBuffers arg) ---

// Layout A (7c8bcc): static .obj mesh, stride 0x20.
struct VertexA {
  float px, py, pz;
  float nx, ny, nz;
  float u, v;
};
static_assert(sizeof(VertexA) == 0x20, "VertexA stride");
static_assert(offsetof(VertexA, nx) == 0xc && offsetof(VertexA, u) == 0x18,
              "VertexA offsets");

// Layout F (7c8bd0): deform mesh, stride 0x24 (+ TEXCOORD1 R32_UINT).
struct VertexF {
  float px, py, pz;
  float nx, ny, nz;
  float u, v;
  uint32_t extra;  // TEXCOORD1 @0x20 (skin/blend index)
};
static_assert(sizeof(VertexF) == 0x24, "VertexF stride");
static_assert(offsetof(VertexF, extra) == 0x20, "VertexF extra offset");

// Layout B/D (7c8bd8 / 7c8be0): 2D/terrain quad, stride 0x18.
struct VertexB {
  float x, y;      // @0, @4  POSITION (R32G32)
  float u, v;      // @8, @0xc TEXCOORD
  float depth;     // @0x10    DEPTH  (R32_FLOAT)
  float alpha;     // @0x14    ALPHA  (R32_FLOAT)
};
static_assert(sizeof(VertexB) == 0x18, "VertexB stride");
static_assert(offsetof(VertexB, u) == 8 && offsetof(VertexB, depth) == 0x10 &&
                  offsetof(VertexB, alpha) == 0x14,
              "VertexB offsets");

// Layout C (7c8bdc): point/line, stride 0x4.
struct VertexC {
  float x;
};
static_assert(sizeof(VertexC) == 4, "VertexC stride");

// Layout E (7c8bd4) slot1: per-instance 4x float4 = one 4x4 matrix, stride 0x40.
struct InstanceE {
  float m[16];
};
static_assert(sizeof(InstanceE) == 0x40, "InstanceE stride");

constexpr uint32_t kStrideA = 0x20, kStrideF = 0x24, kStrideB = 0x18,
                   kStrideC = 0x4, kStrideE0 = 0x20, kStrideE1 = 0x40;

// --- input-layout descriptors (SDL_GPU equivalents of the 0064b560 descs) ---
// The D3D per-instance step rate 1 becomes input_rate = INSTANCE with
// instance_step_rate = 0 (SDL_GPU reserves that field).

// Layout A: POSITION f3 @0, NORMAL f3 @0xc, TEXCOORD f2 @0x18.
inline constexpr SDL_GPUVertexBufferDescription kDescA[] = {
    {0, kStrideA, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0}};
inline constexpr SDL_GPUVertexAttribute kAttrA[] = {
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0x0},
    {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0xc},
    {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0x18}};

// Layout F: A + TEXCOORD1 R32_UINT @0x20.
inline constexpr SDL_GPUVertexBufferDescription kDescF[] = {
    {0, kStrideF, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0}};
inline constexpr SDL_GPUVertexAttribute kAttrF[] = {
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0x0},
    {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0xc},
    {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0x18},
    {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UINT, 0x20}};

// Layout E: slot0 = A shape (stride 0x20); slot1 = 4x float4 per-instance.
inline constexpr SDL_GPUVertexBufferDescription kDescE[] = {
    {0, kStrideE0, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
    {1, kStrideE1, SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0}};
inline constexpr SDL_GPUVertexAttribute kAttrE[] = {
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0x0},
    {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0xc},
    {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0x18},
    {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0x0},
    {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0x10},
    {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0x20},
    {6, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0x30}};

// Layout B / D: POSITION f2 @0, TEXCOORD f2 @8, DEPTH f @0x10, ALPHA f @0x14.
inline constexpr SDL_GPUVertexBufferDescription kDescB[] = {
    {0, kStrideB, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0}};
inline constexpr SDL_GPUVertexAttribute kAttrB[] = {
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0x0},
    {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0x8},
    {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, 0x10},
    {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, 0x14}};
// Layout D is the same shape with an alternate vertex shader (7c8be0).
inline constexpr SDL_GPUVertexBufferDescription kDescD[] = {
    {0, kStrideB, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0}};
inline constexpr SDL_GPUVertexAttribute kAttrD[] = {
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0x0},
    {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0x8},
    {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, 0x10},
    {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, 0x14}};

// Layout C: POSITION f @0.
inline constexpr SDL_GPUVertexBufferDescription kDescC[] = {
    {0, kStrideC, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0}};
inline constexpr SDL_GPUVertexAttribute kAttrC[] = {
    {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, 0x0}};

// --- constant buffers (0064a790 create order -> globals 7c8bf4..7c8c10) ---
// Slots are the VS/PS bind slots seen in the draw wrappers (docs/render.md 4.1).
struct CbDesc {
  uint32_t size;   // CreateBuffer ByteWidth
  int vs_slot;     // VSSetConstantBuffers slot, -1 = not a VS CB
  int ps_slot;     // PSSetConstantBuffers slot, -1 = not a PS CB
};
inline constexpr CbDesc kConstantBuffers[8] = {
    {0x50, 0, -1},  // 7c8bf4 CB0: screen projection + viewport
    {0x40, 1, -1},  // 7c8bf8 CB1: camera matrix B (view)
    {0x40, 2, -1},  // 7c8bfc CB2: per-object world matrix
    {0x40, 3, 3},   // 7c8c04 CB3: quad/rect params (VS3 + PS3)
    {0x10, 4, -1},  // 7c8c0c CB4: fog/depth plane vec4
    {0x50, 5, -1},  // 7c8c08 CB5: textured-triangle params
    {0x40, -1, 6},  // 7c8c10 PS6: global tint/lighting (4x float4)
    {0x600, 7, -1}  // 7c8c00 CB7: world-matrix array (64 matrices)
};

// --- small fixed buffers (0064b350; sizes = CreateBuffer ByteWidth) ---
constexpr uint32_t kFixedQuadVbBytes = 0x60;   // 7c8be4, layout-B quad, 4 verts
constexpr uint32_t kFixedQuadIbBytes = 0x8;    // 7c8be8, u16 x4
constexpr uint32_t kFixedRectIbBytes = 0x4;    // 7c8bf0, u16 x2
constexpr uint32_t kFixedTriVbBytes = 0xc;     // 7c8bec, layout-C, 3 verts

// --- terrain vertex append (006487f0) ---
// One cell-triangle appends 3 layout-B vertices. x/y are int grid coords cast to
// float; u/v are the cell texture coords; depth is the shared depth value;
// alpha is the shared tint alpha (param_7[3]).
inline VertexB MakeTerrainVertex(int x, int y, float u, float v, float depth,
                                 float alpha) {
  return VertexB{(float)x, (float)y, u, v, depth, alpha};
}

// --- layout-B quad (it5 scaffold) ---
// 4 pixel-space verts + a 4-index triangle strip, sized to 0064b350's fixed
// 0x60 VB / 0x8 IB (the "2D/terrain quad" vocab: 00648f60 / 00649a30).
// NOT the original fixed-buffer init data (still open, docs/render.md 13) --
// a stand-in so the layout-B indexed draw path can be exercised. quad.vert
// maps pixel space (x right, y down) to NDC via the pushed viewport uniform.
struct QuadVerts {
  VertexB v[4];
  uint16_t idx[4];
};
static_assert(sizeof(QuadVerts::v) == kFixedQuadVbBytes, "quad VB = 0x60");
static_assert(sizeof(QuadVerts::idx) == kFixedQuadIbBytes, "quad IB = 0x8");

inline QuadVerts MakeQuad(float x, float y, float w, float h) {
  return {{{x, y, 0.f, 0.f, 0.5f, 1.f},
           {x + w, y, 1.f, 0.f, 0.5f, 1.f},
           {x, y + h, 0.f, 1.f, 0.5f, 1.f},
           {x + w, y + h, 1.f, 1.f, 0.5f, 1.f}},
          {0, 1, 2, 3}};
}

}  // namespace render3d
