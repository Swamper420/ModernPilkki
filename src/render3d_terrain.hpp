// Iteration 7 of the SDL_GPU port: the terrain cell pipeline, ported 1:1 from
//  0040e260  cull (cross product in integer screen coords, max-component
//            normalise, nz test), material/UV-scale select, 006487f0 append,
//            0064c8a0 flush signal
//  006487f0  layout-B append (3 verts: x,y,u,v,depth,alpha)
// CPU-only: no GPU objects here. Iteration 11 added the per-frame projection
// 004a7660 + its spawner 004bce00 (WeatherField below): a snow/rain particle
// field in screen space, NOT the depth grid (docs/render.md 8.4).
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "game.hpp"
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

// --- 004a7660 weather cells (it11) -------------------------------------------
// Snow/rain particle field, screen space. The world object owns the cell block
// (+0x151b4 count, +0x15234 records, 10 slots/cell; 004a7660 indexes cells
// from +0x15248). Spawner 004bce00 (weather id: 0 clear, 1 snow 166 cells,
// 2 rain 500; x1.5 + clamp 1000 when the window is wider than 1280). Per
// frame 004a7660 advances each cell on the game clock (7c695c), wraps it into
// the screen and projects one right triangle (leg = round((1 - 0.0008*z) *
// size), size = 16 px, x1.2 over 1280 / x1.5 over 1920 window width; x sways
// with sin(phase/2)+sin(phase)+sin(phase/10), phase wraps at 20*pi) into the
// mesh scratch verts (geometry+0x374) feeding 0040e5b0/0040e260. docs/
// render.md 8.4. Deviation: the x87 sine (_CIsin range guard 0067d495 +
// polynomial 00686210) is stdlib sin(); the result truncates to int anyway.

struct WeatherCell {
  float x, y, z;    // screen pos + life-depth 0..1000 (falls with dt*0.2)
  int32_t ts;       // last update, game ms
  float vx;         // x drift px/ms (spawned 0)
  float vy;         // fall px/ms = winH * 1/1800 (depth-scaled at runtime)
  float vz;         // depth rate px/ms (0.2)
  float phase;      // sway phase, rad (wrap 62.831852f)
  float phaseVel;   // rad/ms = 2pi_f / (rand(1000) + 3000.0f)
  float z0;         // depth restored when the cell wraps past the bottom
};
static_assert(sizeof(WeatherCell) == 0x28, "cell stride 0x28");

struct WeatherField {
  std::vector<WeatherCell> cells;
  bool havePrevCam = false;    // world+0x1ee80
  int32_t prevCam[3] = {};     // world+0x1ee74/78/7c (camera +0x64/0x68/0x6c)
  int32_t camDelta[3] = {};    // frame delta, cached at full-pass entry
};

// 004a7660 argument bundle (world fields the original reads through `this`).
struct WeatherFrame {
  int32_t now = 0;             // game clock ms (DAT_007c695c)
  const int32_t* cam = nullptr;  // 3 camera ints; null = no parallax
  double windX = 0.0, windY = 0.0;  // world+0x1ee88/+0x1ee90 (004c02d0 tables)
  double zoom = 0.0;           // world+0x1ee98, scales the sway amplitude
  int winW = 0, winH = 0;      // window client w/h (0061ecc0 / 0061ec90)
  double size = 16.0;          // DAT_006dc680 (the 004a5d00 caller's argument)
};

// 004bce00 param_3 == 0 pass: resize the field to the weather id's count and
// init only the NEW cells (rand draw order per cell: W, H+0x60, 500, 600,
// 1000). The weather-change ramp (00650fe0, 30 s, param_3 != 0) is not
// ported -- count jumps. Re-spawning the same id consumes no rands.
inline void SpawnWeather(WeatherField& wf, int type, int winW, int winH,
                         int32_t now, game::MsvcRand& rng) {
  int target = type == 1 ? 166 : type == 2 ? 500 : 0;
  if (winW > 0x4ff) {  // 0061f2e0
    target = (int)((double)target * 1.5);  // DAT_006d1638
    if (target > 1000) target = 1000;
  }
  const size_t old = wf.cells.size();
  wf.cells.resize((size_t)target);
  for (size_t i = old; i < wf.cells.size(); ++i) {
    WeatherCell c;
    c.x = (float)rng.RandMax(winW);
    c.y = (float)rng.RandMax(winH + 0x60);
    c.z = (float)rng.RandMax(500);
    c.ts = now;
    c.vx = 0.0f;
    c.vy = (float)winH * 0.0005555555690079927f;  // DAT_006d0870
    c.vz = 0.2f;
    c.phase = (float)rng.RandMax(600) * 0.01f;    // DAT_006d052c
    c.phaseVel =
        6.283180236816406f / ((float)rng.RandMax(1000) + 3000.0f);  // 006dc6c0/710
    c.z0 = c.z;
    wf.cells[i] = c;
  }
}

// The single face record 004bce00 bakes into the weather mesh geometry+0x37c:
// triangle over scratch verts 0/1/2, material 0 (rain.pcx), UVs (2,2)-
// (0x30,2)-(2,0x30), flags bit0 = skip cull.
inline const CellFace& WeatherFace() {
  static const CellFace f = [] {
    CellFace r{};
    r.v[0] = 0; r.v[1] = 1; r.v[2] = 2;
    r.unk[0] = 0x40; r.unk[1] = 0x40;
    r.material = 0;
    r.uv[0] = 2;  r.uv[1] = 2;  r.uv[2] = 0x30;
    r.uv[3] = 2;  r.uv[4] = 2;  r.uv[5] = 0x30;
    r.flags = 1;
    return r;
  }();
  return f;
}

// 004a7660: advance cells [i0, n), append each projected triangle via
// TerrainAppend. Camera delta is recomputed only at full-pass entry (i0 == 0,
// first pass = 0); resume passes cache it. Returns the first unprocessed cell
// (== cells.size() when done); on a full batch the caller flushes, clears and
// resumes at the returned index (0040e260 -> 0064c8a0).
inline size_t UpdateWeather(WeatherField& wf, const WeatherFrame& fr,
                            game::MsvcRand& rng, std::vector<VertexB>& batch,
                            size_t cap, const int32_t* uvTable = nullptr,
                            size_t i0 = 0) {
  static const int32_t kZeroCam[3] = {};
  const int32_t* cam = fr.cam ? fr.cam : kZeroCam;
  if (i0 == 0) {
    for (int k = 0; k < 3; ++k) {
      wf.camDelta[k] = wf.havePrevCam ? cam[k] - wf.prevCam[k] : 0;
      wf.prevCam[k] = cam[k];
    }
    wf.havePrevCam = true;
  }
  double size = fr.size;  // 0061f260/0061f2e0 window-width scale
  if (fr.winW > 0x77f) size *= 1.5;       // DAT_006d1638
  else if (fr.winW > 0x4ff) size *= 1.2;  // DAT_006dc668
  const float wx = (float)fr.windX, wy = (float)fr.windY;
  const float cdx = (float)wf.camDelta[0] * 0.1f;  // DAT_006db094
  const float cdy = (float)wf.camDelta[1] * 0.1f;
  const float cdz = (float)wf.camDelta[2] * 0.1f;
  const float Wf = (float)fr.winW, Hf = (float)(fr.winH + 0x60);
  for (size_t i = i0; i < wf.cells.size(); ++i) {
    WeatherCell& c = wf.cells[i];
    float s = (1000.0f - c.z) * 0.001f;  // DAT_006d05ac / DAT_006d051c
    if (s < 0.5f) s = 0.5f;              // DAT_0077782c
    const float dt = (float)(double)(fr.now - c.ts);  // 00777a20 table is 0.0f
    const float xn = dt * c.vx + c.x;
    float zc = dt * c.vz + c.z;
    if (zc >= 1000.0f) zc = 1000.0f;
    c.phase = dt * c.phaseVel + c.phase;
    c.y = dt * c.vy * s + c.y;
    c.z = zc;
    c.ts = fr.now;
    if (62.831853 < (double)c.phase) c.phase -= 62.831851959228516f;
    c.x = xn - (wx * dt + cdx) * s;
    c.y = (c.y - (wy * dt + cdy) * s) + cdz * s;
    // One-step screen wrap (not modulo: fast camera/wind moves need frames).
    if (c.x <= Wf) {
      if (c.x < 0.0f) c.x += Wf;
    } else {
      c.x -= Wf;
    }
    if (c.y <= Hf) {
      if (-48.0f <= c.y) {               // DAT_006dc778
        if (1000.0f <= c.z) {            // life done -> re-roll in place
          c.y = (float)rng.RandMax(fr.winH + 0x60);
          c.z = (float)rng.RandMax(300);
        }
      } else {
        c.y += Hf;                        // past the top -> to the bottom
      }
    } else {                              // past the bottom -> respawn on top
      c.z = c.z0;
      c.y -= Hf;
    }
    const double ph = (double)c.phase;
    // sway -> screen x, y top = trunc(y) - 0x30, leg = round((1 - 0.0008*z) * size)
    const double wob = (sin(ph * 0.5) + sin(ph) + sin(ph * 0.1)) *
                       (1.0 - fr.zoom) * 30.0;  // DAT_0077782c/006d12d0/007778d8
    const int X = (int)(wob + (double)c.x);
    const int Ytop = (int)c.y - 0x30;
    const int w = (int)((1.0 - (double)c.z * 0.0008) * size + 0.5);  // 006dc5c0/00777878
    const int32_t pos[9] = {X, Ytop, 0, X + w, Ytop, 0, X, Ytop + w, 0};
    if (TerrainAppend(batch, cap, pos, WeatherFace(), uvTable, 1.0f))
      return i + 1;  // triangle already in; caller flushes + clears, resumes here
  }
  return wf.cells.size();
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
