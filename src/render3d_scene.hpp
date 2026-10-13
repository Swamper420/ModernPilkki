// Iteration 8 of the SDL_GPU port: a world-space lake scene, the first
// playable-ish 3D view. Builds a layout-A terrain mesh from a .ves DEPTHMAP
// grid and a clean-room camera (look-at + perspective), so the existing
// layout-A mesh pipeline can draw the lake under a moving camera.
//
// This is NOT the original 004a7660 CPU screen-projection (still open, see
// docs/render.md 8.1/13): the original projects each grid cell to screen-space
// layout-B triangles every frame. Here the grid becomes a static world-space
// mesh drawn with a view-projection matrix, which is the standard GPU shape and
// reuses the port's existing mesh path. Swap in 004a7660 when it is decompiled.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "render3d_layout.hpp"
#include "render3d_math.hpp"
#include "ves.hpp"

namespace render3d {

// Same 17 thresholds as terrain.hpp / game::kDepthTable (docs/file-formats.md).
inline constexpr int kSceneDepthTable[17] = {0,   30,  60,  130, 160, 200,
                                             250, 300, 350, 400, 500, 700,
                                             900, 1000, 1500, 2000, 2500};

// Cell byte -> height fraction (cell*17/256 index into the table / 2500).
inline float SceneHeight(uint8_t c) {
  int idx = (int)c * 17 / 256;
  if (idx > 16) idx = 16;
  return kSceneDepthTable[idx] / 2500.f;
}

// Camera: eye + target in world units (x/z = +-128 ground plane, y = up).
struct SceneCam {
  float ex = 0, ey = 40, ez = 90;
  float tx = 0, ty = 0, tz = 0;
  float fovDeg = 55.f, nearZ = 1.f, farZ = 400.f;
};

// D3D-style row-vector camera matrices, in the port's convention (v' = v * M,
// clip z in 0..1, NDC y-up -- verified against the rendered terrain, which
// appears below the horizon line).
inline Mat4 LookAt(const SceneCam& c) {
  float zx = c.tx - c.ex, zy = c.ty - c.ey, zz = c.tz - c.ez;
  float zl = sqrtf(zx * zx + zy * zy + zz * zz);
  if (zl > 0) { zx /= zl; zy /= zl; zz /= zl; }
  // x = up x z, up = (0,1,0)
  float xx = 1 * zz - 0 * zy, xy = 0 * zx - 0 * zz, xz = 0 * zy - 1 * zx;
  float xl = sqrtf(xx * xx + xy * xy + xz * xz);
  if (xl > 0) { xx /= xl; xy /= xl; xz /= xl; }
  // y = z x x
  float yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
  return Mat4{{xx, yx, zx, 0,
               xy, yy, zy, 0,
               xz, yz, zz, 0,
               -(xx * c.ex + xy * c.ey + xz * c.ez),
               -(yx * c.ex + yy * c.ey + yz * c.ez),
               -(zx * c.ex + zy * c.ey + zz * c.ez), 1}};
}

inline Mat4 Perspective(float fovDeg, float aspect, float nearZ, float farZ) {
  const float h = 1.f / tanf(fovDeg * (float)M_PI / 360.f);
  const float w = h / (aspect > 0 ? aspect : 1.f);
  return Mat4{{w, 0, 0, 0,
               0, h, 0, 0,
               0, 0, farZ / (farZ - nearZ), 1,
               0, 0, -nearZ * farZ / (farZ - nearZ), 0}};
}

// Combined view * projection (row-vector: v * View * Proj).
inline Mat4 ViewProj(const SceneCam& c, float aspect) {
  return Mul(LookAt(c), Perspective(c.fovDeg, aspect, c.nearZ, c.farZ));
}

// Layout-A terrain mesh: world positions, finite-difference normals, uv = grid
// coords normalised. `zScale` scales the 0..1 depth fraction to world units; the
// bed sits BELOW y=0 (deep cells are lower), so the lake reads as a basin.
inline std::vector<VertexA> BuildTerrainMesh(const ves::Grid& depth,
                                             float zScale = 12.f) {
  std::vector<VertexA> out;
  const int w = depth.w, h = depth.h;
  if (w < 2 || h < 2 || (int)depth.cells.size() < w * h) return out;
  auto H = [&](int x, int y) -> float {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= w) x = w - 1;
    if (y >= h) y = h - 1;
    return SceneHeight(depth.cells[(size_t)y * w + x]);
  };
  const float invW = 256.f / (float)(w - 1), invH = 256.f / (float)(h - 1);
  auto P = [&](int x, int y) {
    VertexA v{};
    v.px = (float)x * invW - 128.f;
    v.pz = (float)y * invH - 128.f;
    v.py = -H(x, y) * zScale;
    const float dx = (H(x + 1, y) - H(x - 1, y)) * zScale / (2.f * invW);
    const float dz = (H(x, y + 1) - H(x, y - 1)) * zScale / (2.f * invH);
    const float nx = dx, ny = 1.f, nz = dz;
    const float nl = sqrtf(nx * nx + ny * ny + nz * nz);
    v.nx = nx / nl; v.ny = ny / nl; v.nz = nz / nl;
    v.u = (float)x / (float)(w - 1);
    v.v = (float)y / (float)(h - 1);
    return v;
  };
  out.reserve((size_t)(w - 1) * (h - 1) * 6);
  for (int y = 0; y + 1 < h; ++y)
    for (int x = 0; x + 1 < w; ++x) {
      VertexA a = P(x, y), b = P(x + 1, y), c = P(x, y + 1), d = P(x + 1, y + 1);
      out.push_back(a); out.push_back(b); out.push_back(c);
      out.push_back(b); out.push_back(d); out.push_back(c);
    }
  return out;
}

// RGBA texture from the DEPTHMAP palette (256 RGB triplets), one texel per cell,
// so a cell-aligned uv samples the original depth shading.
inline std::vector<uint8_t> BuildTerrainTex(const ves::Grid& depth) {
  std::vector<uint8_t> px((size_t)depth.w * depth.h * 4, 0);
  const bool pal = depth.pal.size() >= 768;
  for (size_t i = 0; i < depth.cells.size(); ++i) {
    uint8_t r = 40, g = 90, b = 160;
    if (pal) {
      const uint8_t k = depth.cells[i];
      r = depth.pal[3 * k];
      g = depth.pal[3 * k + 1];
      b = depth.pal[3 * k + 2];
    }
    px[4 * i] = r; px[4 * i + 1] = g; px[4 * i + 2] = b; px[4 * i + 3] = 255;
  }
  return px;
}

}  // namespace render3d
