// Iteration 7 of the SDL_GPU port: the terrain cell pipeline, ported 1:1 from
//  0040e260  cull (cross product in integer screen coords, max-component
//            normalise, nz test), material/UV-scale select, 006487f0 append,
//            0064c8a0 flush signal
//  006487f0  layout-B append (3 verts: x,y,u,v,depth,alpha)
// CPU-only: no GPU objects here. The per-frame projection 004a7660 (depth grid
// -> screen positions) is a later slice -- it needs world state the port does
// not have yet (docs/render.md 8/13); this file starts from the projected
// position array that 0040e260 consumes.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "render3d_layout.hpp"

namespace render3d {

// --- pinned constants (tools/peconst.py) ---
constexpr int32_t kCellPosStride = 0xc;  // 3x i32 (x,y,z) per projected vertex
inline constexpr float kCellNormalScaleMax = 1000.0f;  // DAT_006d0590
inline constexpr float kCellNormalCullZ = 0.0f;        // DAT_006d05b8
constexpr int32_t kCellZMin = -0x2bd;                  // z < -701 -> cell skipped
inline constexpr float kCellDepth = 0.001f;            // 0x3a83126f (006487f0 p8)

// Face record at geometry+0x37c (0040e260 param_2), stride 0x34.
struct CellFace {
  int32_t v[3];      // @0x00 vertex indices into the projected-position array
  int32_t unk[2];    // @0x0c not read by 0040e260
  uint8_t material;  // @0x14 index into the per-mesh UV-scale table (+0x8cc)
  uint8_t pad0[3];
  int32_t uv[6];     // @0x18 three (u,v) pairs
  uint8_t flags;     // @0x30 bit0 = skip cull, 0x1c = packed-RGB variant
  uint8_t pad1[3];
};
static_assert(sizeof(CellFace) == 0x34, "CellFace stride");
static_assert(offsetof(CellFace, material) == 0x14 &&
                  offsetof(CellFace, uv) == 0x18 &&
                  offsetof(CellFace, flags) == 0x30,
              "CellFace offsets");

inline const int32_t* CellVertPos(const int32_t* posBase, int32_t idx) {
  return posBase + (size_t)idx * 3;
}

// 0040e260: cross(B-A, C-A). The original carries the x deltas through float
// and back to int, which is exact for screen-sized coords. false when the
// triangle is degenerate (all three components zero).
inline bool CellNormal(const int32_t* posBase, const int32_t v[3], int32_t n[3]) {
  const int32_t* a = CellVertPos(posBase, v[0]);
  const int32_t* b = CellVertPos(posBase, v[1]);
  const int32_t* c = CellVertPos(posBase, v[2]);
  const int32_t abx = b[0] - a[0], aby = b[1] - a[1], abz = b[2] - a[2];
  const int32_t acx = c[0] - a[0], acy = c[1] - a[1], acz = c[2] - a[2];
  n[0] = aby * acz - abz * acy;
  n[1] = abz * acx - abx * acz;
  n[2] = abx * acy - aby * acx;
  return n[0] || n[1] || n[2];
}

// 0040e260 cull: all three z above kCellZMin, non-degenerate normal, and the
// normal's z fraction not positive. The 1000 max-component normalisation
// cannot change nz/|n|, so it is a no-op for the decision -- kept for fidelity.
inline bool CellVisible(const int32_t* posBase, const int32_t v[3]) {
  for (int i = 0; i < 3; ++i)
    if (CellVertPos(posBase, v[i])[2] < kCellZMin) return false;
  int32_t n[3];
  if (!CellNormal(posBase, v, n)) return false;
  double nx = n[0], ny = n[1], nz = n[2];
  double m = fabs(nx);
  if (fabs(ny) > m) m = fabs(ny);
  if (fabs(nz) > m) m = fabs(nz);
  if (m > kCellNormalScaleMax) {
    const double s = 1.0 / m;
    nx *= s;
    ny *= s;
    nz *= s;
  }
  const double len = sqrt(nx * nx + ny * ny + nz * nz);
  return !(len > 0.0 && nz / len > kCellNormalCullZ);
}

// 006487f0: append one cell as 3 layout-B vertices. Positions come from the
// projected int array, UVs are scaled 1/uvTable[material] (+0x8cc), depth is
// the shared constant and alpha the tint (param_7[3]). Returns true when the
// batch is full -> caller flushes (0064c8a0).
inline bool TerrainAppend(std::vector<VertexB>& batch, size_t cap,
                          const int32_t* posBase, const CellFace& f,
                          const int32_t* uvTable, float alpha,
                          float depth = kCellDepth) {
  if (batch.size() >= cap) return true;  // 006487f0 returns 2 (already full)
  const float s = uvTable ? 1.0f / (float)uvTable[f.material] : 1.0f;
  for (int i = 0; i < 3; ++i) {
    const int32_t* p = CellVertPos(posBase, f.v[i]);
    VertexB vb;
    vb.x = (float)p[0];
    vb.y = (float)p[1];
    vb.u = (float)f.uv[2 * i] * s;
    vb.v = (float)f.uv[2 * i + 1] * s;
    vb.depth = depth;
    vb.alpha = alpha;
    batch.push_back(vb);
  }
  return batch.size() >= cap;
}

// Screen-space triangle list for a grid heightfield, as layout-B vertices.
// Stand-in for the 004a7660 projection (still open): maps the grid into the
// viewport box and offsets y by the cell height. uv = normalised grid coords.
inline std::vector<VertexB> SyntheticGridVerts(
    int gw, int gh, float vpW, float vpH,
    const std::vector<float>* height = nullptr, float hscale = 24.f) {
  std::vector<VertexB> out;
  if (gw < 2 || gh < 2 || vpW <= 0.f || vpH <= 0.f) return out;
  out.reserve((size_t)(gw - 1) * (gh - 1) * 6);
  auto H = [&](int x, int y) -> float {
    if (!height || height->empty()) return 0.f;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= gw) x = gw - 1;
    if (y >= gh) y = gh - 1;
    return (*height)[(size_t)y * gw + x];
  };
  auto vert = [&](int x, int y) {
    VertexB vb;
    vb.x = (float)x / (float)(gw - 1) * vpW;
    vb.y = (float)y / (float)(gh - 1) * vpH - H(x, y) * hscale;
    vb.u = (float)x / (float)(gw - 1);
    vb.v = (float)y / (float)(gh - 1);
    vb.depth = kCellDepth;
    vb.alpha = 1.f;
    return vb;
  };
  for (int y = 0; y + 1 < gh; ++y)
    for (int x = 0; x + 1 < gw; ++x) {
      VertexB a = vert(x, y), b = vert(x + 1, y), c = vert(x, y + 1),
              d = vert(x + 1, y + 1);
      out.push_back(a);
      out.push_back(b);
      out.push_back(c);
      out.push_back(b);
      out.push_back(d);
      out.push_back(c);
    }
  return out;
}

}  // namespace render3d
