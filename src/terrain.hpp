// 2.5D lake terrain: heightfield baked into an SDL3 texture.
// Reuses ves::ParseGrid cells + render::BuildBackground base (VISUAL_MAP as
// PCX when decodable, else DEPTHMAP palette + TYPEMAP tint) and
// render::Camera/SrcRect/DstRect/Draw. No new deps, no GL.
// Height: cell -> DEPTH_TABLE idx (c*17/256, same as game::DepthFracAt) ->
// kDepthTable[idx]/2500. Relief = NW-light hill-shade, depth darkening,
// white foam on depth-0 cells touching water. Draw() forwards to
// render::Draw; the 3D-ish look is baked in BuildBackground.
#pragma once
#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

#include "objects.hpp"
#include "render.hpp"
#include "ves.hpp"

namespace terrain {

// Same 17 thresholds as game::kDepthTable (docs/file-formats.md, meri.ves).
inline constexpr int kDepthTable[17] = {0,   30,  60,  130, 160, 200,
                                        250, 300, 350, 400, 500, 700,
                                        900, 1000, 1500, 2000, 2500};

inline float HeightOf(uint8_t c) {
  int idx = (int)c * 17 / 256;
  if (idx > 16) idx = 16;
  return kDepthTable[idx] / 2500.f;
}

struct Mesh {
  int w = 0, h = 0;
  std::vector<float> z;  // w*h, 0..1
};

inline Mesh BuildMesh(const ves::Grid &depth) {
  Mesh m;
  m.w = depth.w;
  m.h = depth.h;
  m.z.resize(depth.cells.size());
  for (size_t i = 0; i < depth.cells.size(); i++)
    m.z[i] = HeightOf(depth.cells[i]);
  return m;
}

// Nearest cell at world coords (+-128). 0 on empty mesh.
inline float HeightAt(const Mesh &m, float wx, float wy) {
  if (m.w <= 0 || m.h <= 0 || m.z.empty()) return 0;
  int cx = (int)((wx + 128.f) / 256.f * m.w);
  int cy = (int)((wy + 128.f) / 256.f * m.h);
  if (cx < 0) cx = 0;
  if (cy < 0) cy = 0;
  if (cx >= m.w) cx = m.w - 1;
  if (cy >= m.h) cy = m.h - 1;
  return m.z[(size_t)cy * m.w + cx];
}

// Shaded background. Same contract as render::BuildBackground.
inline render::Bg BuildBackground(const ves::Grid &depth, const ves::Grid *type,
                                  const std::vector<uint8_t> *visual) {
  render::Bg b = render::BuildBackground(depth, type, visual);
  // ponytail: shading needs grid-matched pixels; a decoded VISUAL_MAP of
  // other dims is returned as-is (heights still available via BuildMesh).
  if (b.w != depth.w || b.h != depth.h || depth.cells.empty() ||
      b.rgb.empty())
    return b;
  int w = depth.w, h = depth.h;
  auto H = [&](int x, int y) -> float {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= w) x = w - 1;
    if (y >= h) y = h - 1;
    return HeightOf(depth.cells[(size_t)y * w + x]);
  };
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      size_t i = (size_t)y * w + x;
      float hg = H(x, y);
      float r = b.rgb[3 * i], g = b.rgb[3 * i + 1], bl = b.rgb[3 * i + 2];
      // Depth darkening: deep fades toward dark blue.
      r *= 1.f - 0.45f * hg;
      g *= 1.f - 0.45f * hg;
      bl = bl * (1.f - 0.2f * hg) + 20 * hg;
      // Hill-shade, light from NW: slope from clamped neighbors.
      float dx = H(x + 1, y) - H(x - 1, y);
      float dy = H(x, y + 1) - H(x, y - 1);
      float shade = 1.f - 2.f * (dx + dy);
      if (shade < 0.55f) shade = 0.55f;
      if (shade > 1.35f) shade = 1.35f;
      r *= shade;
      g *= shade;
      bl *= shade;
      // Shoreline foam: depth-0 cell touching water.
      uint8_t c = depth.cells[i];
      if (c == 0 &&
          ((x > 0 && depth.cells[i - 1]) || (x + 1 < w && depth.cells[i + 1]) ||
           (y > 0 && depth.cells[i - w]) ||
           (y + 1 < h && depth.cells[i + (size_t)w]))) {
        r = 235;
        g = 242;
        bl = 248;
      }
      b.rgb[3 * i] = (uint8_t)(r > 255 ? 255 : r);
      b.rgb[3 * i + 1] = (uint8_t)(g > 255 ? 255 : g);
      b.rgb[3 * i + 2] = (uint8_t)(bl > 255 ? 255 : bl);
    }
  return b;
}

// Full frame. Forwards to render::Draw (relief already baked in the
// texture); exists so the render path goes through terrain. Null-renderer
// safe like render::Draw.
inline bool Draw(SDL_Renderer *r, SDL_Texture *bg, int bgW, int bgH,
                 const std::vector<ves::ObjGroup> &groups,
                 const objects::ObjTable *tab, const render::Camera &cam,
                 const render::Player &pl, const std::vector<render::Hole> &holes,
                 bool lineOut, float lineX, float lineY, int winW, int winH,
                 models::Bank *bank = nullptr,
                 models::TexCache *texc = nullptr) {
  return render::Draw(r, bg, bgW, bgH, groups, tab, cam, pl, holes, lineOut,
                      lineX, lineY, winW, winH, bank, texc);
}

}  // namespace terrain
