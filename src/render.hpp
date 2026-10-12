// Lake renderer: background + objects + player/holes/line overlays.
// Single header, SDL3 renderer only. Reuses ves/pcx/objects parsers,
// never rewrites them. Boring by design: dots now, sprites when asset
// paths resolve (objects.dat gfx paths need extracted/ dir at runtime).
// Background: VISUAL_MAP payload tried as PCX; on failure DEPTHMAP
// palette fallback (palette IS the depth shading, plus a slight
// deep-blue lerp so EGA palettes still read as water) + TYPEMAP green
// tint where type cell != 0. World extent is +-128 in x/y (8.24 fixed
// in OBJECT_POSITIONS); the bg grid is assumed to cover it fully.
// Camera: zoom>=1 subrect of bg texture, letterboxed to window.
#pragma once
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "models.hpp"
#include "objects.hpp"
#include "pcx.hpp"
#include "ves.hpp"

namespace render {

struct Camera {
  float x = 0, y = 0;  // world center, +-128
  float zoom = 1;      // 1 = whole lake, up to 8
  float pitch = 0;     // 0 flat .. 1 full tilt (camera::PitchForZoom)
};

struct Player {
  float x = 0, y = 0;
};

struct Hole {
  float x = 0, y = 0;
};

struct Bg {
  int w = 0, h = 0;
  std::vector<uint8_t> rgb;  // w*h*3
};

inline float ClampF(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// 2.5D tilt: squash screen Y toward dst center. Pure math.
inline float PitchY(float sy, const SDL_FRect &dst, float pitch) {
  float k = 1 - 0.3f * ClampF(pitch, 0.f, 1.f);
  float cy = dst.y + dst.h / 2;
  return cy + (sy - cy) * k;
}

// Background build. visual = raw VISUAL_MAP payload or nullptr.
// ponytail: VISUAL blob format open; PCX try + palette fallback is the
// ceiling, swap in real decoder when decomp pins it.
inline Bg BuildBackground(const ves::Grid &depth, const ves::Grid *type,
                          const std::vector<uint8_t> *visual) {
  if (visual && !visual->empty()) {
    try {
      pcx::Image im = pcx::Decode(*visual);
      if (im.w > 0 && im.h > 0 &&
          (size_t)im.w * im.h <= (size_t)64 * 1024 * 1024) {
        Bg b{im.w, im.h, {}};
        b.rgb.resize((size_t)im.w * im.h * 3);
        for (size_t i = 0; i < (size_t)im.w * im.h; i++) {
          uint8_t r, g, bl;
          if (im.planes == 1) {
            uint8_t k = im.idx[i];
            r = im.pal[3 * k];
            g = im.pal[3 * k + 1];
            bl = im.pal[3 * k + 2];
          } else {
            size_t o = (size_t)i * (im.planes == 3 ? 3 : 4);
            r = im.px[o];
            g = im.px[o + 1];
            bl = im.px[o + 2];
          }
          b.rgb[3 * i] = r;
          b.rgb[3 * i + 1] = g;
          b.rgb[3 * i + 2] = bl;
        }
        return b;
      }
    } catch (...) {
      // fall through to palette grid
    }
  }
  Bg b;
  if (depth.cells.empty() || depth.w == 0 || depth.h == 0) return b;
  b.w = depth.w;
  b.h = depth.h;
  b.rgb.resize((size_t)b.w * b.h * 3);
  bool hasType = type && type->cells.size() == depth.cells.size();
  for (size_t i = 0; i < depth.cells.size(); i++) {
    uint8_t c = depth.cells[i];
    float r = depth.pal[3 * c], g = depth.pal[3 * c + 1],
          bl = depth.pal[3 * c + 2];
    // Depth shading: lerp toward deep blue with cell value.
    float f = c / 255.f * 0.35f;
    r = r * (1 - f) + 10 * f;
    g = g * (1 - f) + 40 * f;
    bl = bl * (1 - f) + 120 * f;
    if (hasType && type->cells[i]) {  // TYPEMAP tint: shore/vegetation
      r = r * 0.8f;
      g = g * 0.85f + 25;
      bl = bl * 0.8f;
    }
    b.rgb[3 * i] = (uint8_t)(r > 255 ? 255 : r);
    b.rgb[3 * i + 1] = (uint8_t)(g > 255 ? 255 : g);
    b.rgb[3 * i + 2] = (uint8_t)(bl > 255 ? 255 : bl);
  }
  return b;
}

// Camera src subrect of bg texture, clamped inside. Pure math (tested).
inline SDL_FRect SrcRect(const Camera &c, int bgW, int bgH) {
  float z = ClampF(c.zoom, 1.f, 8.f);
  float w = bgW / z, h = bgH / z;
  float cx = (ClampF(c.x, -128.f, 128.f) + 128.f) / 256.f * bgW;
  float cy = (ClampF(c.y, -128.f, 128.f) + 128.f) / 256.f * bgH;
  float x = ClampF(cx - w / 2, 0.f, (float)bgW - w);
  float y = ClampF(cy - h / 2, 0.f, (float)bgH - h);
  if (bgW <= 0 || bgH <= 0) return {(float)x, (float)y, 0, 0};
  return {x, y, w, h};
}

// Letterboxed dst rect preserving bg aspect. Pure math (tested).
inline SDL_FRect DstRect(int bgW, int bgH, int winW, int winH) {
  if (bgW <= 0 || bgH <= 0 || winW <= 0 || winH <= 0)
    return {0, 0, (float)winW, (float)winH};
  float s = SDL_min((float)winW / bgW, (float)winH / bgH);
  float w = bgW * s, h = bgH * s;
  return {(winW - w) / 2, (winH - h) / 2, w, h};
}

// World (+-128) to bg pixel. Y same orientation as old dot loop.
inline void WorldToBg(float wx, float wy, int bgW, int bgH, float &bx,
                      float &by) {
  bx = (wx + 128.f) / 256.f * bgW;
  by = (wy + 128.f) / 256.f * bgH;
}

inline bool BgToScreen(float bx, float by, const SDL_FRect &src,
                       const SDL_FRect &dst, float &sx, float &sy) {
  if (src.w <= 0 || src.h <= 0) return false;
  if (bx < src.x || by < src.y || bx > src.x + src.w || by > src.y + src.h)
    return false;
  sx = dst.x + (bx - src.x) / src.w * dst.w;
  sy = dst.y + (by - src.y) / src.h * dst.h;
  return true;
}

inline bool Mapped(const objects::ObjTable *tab, uint8_t cls) {
  return tab && cls < tab->rows.size() && !tab->rows[cls].file.empty() &&
         tab->rows[cls].file != "disabled";
}

// Model pass: nearest kModelCap mapped records draw as software-projected
// meshes (objects.dat yaw/scale), the rest + load failures fall back to
// class-colored dots. Screen pos reuses WorldToBg/BgToScreen + PitchY.
// tilt maps cam.pitch (0..0.5 squash) to a real view angle.
inline constexpr size_t kModelCap = 200;

inline void DrawObjects(SDL_Renderer *r, models::Bank &bank,
                        models::TexCache &texc,
                        const std::vector<ves::ObjGroup> &groups,
                        const objects::ObjTable *tab, const Camera &cam,
                        int bgW, int bgH, const SDL_FRect &src,
                        const SDL_FRect &dst, int winW, int winH) {
  struct Cand {
    float d2 = 0, sx = 0, sy = 0, yaw = 0, pix = 0;
    const models::Mesh *m = nullptr;
    uint8_t cr = 0, cg = 0, cb = 0;
  };
  float tilt = 0.35f + cam.pitch;
  float zoom = ClampF(cam.zoom, 1.f, 8.f);
  auto dotAt = [&](float sx, float sy, uint8_t cr, uint8_t cg, uint8_t cb,
                   float size) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_FRect d{sx - size / 2, sy - size / 2, size, size};
    SDL_RenderFillRect(r, &d);
  };
  std::vector<Cand> cs;
  for (auto &g : groups)
    for (auto &rec : g) {
      float wx = rec.x / 16777216.f, wy = rec.y / 16777216.f;
      float bx, by, sx, sy;
      if (bgW > 0 && bgH > 0) {
        WorldToBg(wx, wy, bgW, bgH, bx, by);
        if (!BgToScreen(bx, by, src, dst, sx, sy)) continue;
      } else {
        sx = (wx + 128.f) / 256.f * winW;
        sy = (wy + 128.f) / 256.f * winH;
      }
      sy = PitchY(sy, dst, cam.pitch);
      uint8_t cr = 255, cg = (uint8_t)(80 + (rec.cls * 37) % 175),
              cb = (uint8_t)(60 + (rec.cls * 91) % 195);
      if (!Mapped(tab, rec.cls)) {
        dotAt(sx, sy, 255, 0, 0, 3);
        continue;
      }
      const objects::ObjDef &row = tab->rows[rec.cls];
      Cand c;
      c.d2 = (wx - cam.x) * (wx - cam.x) + (wy - cam.y) * (wy - cam.y);
      c.sx = sx;
      c.sy = sy;
      c.yaw = (float)row.angle;
      float pix = 22.f * zoom * (float)(row.scale > 0 ? row.scale : 1.0);
      c.pix = pix > 160 ? 160 : pix;
      c.m = &bank.Get(row.file);
      c.cr = cr;
      c.cg = cg;
      c.cb = cb;
      cs.push_back(c);
    }
  size_t n = cs.size();
  if (n > kModelCap) {
    std::nth_element(cs.begin(), cs.begin() + kModelCap, cs.end(),
                     [](const Cand &a, const Cand &b) { return a.d2 < b.d2; });
    n = kModelCap;
  }
  for (size_t i = 0; i < n; i++) {
    Cand &c = cs[i];
    if (!c.m || !c.m->ok ||
        !models::DrawOne(r, bank, texc, *c.m, c.yaw, tilt, c.pix, c.sx, c.sy))
      dotAt(c.sx, c.sy, c.cr, c.cg, c.cb, 3);
  }
  for (size_t i = n; i < cs.size(); i++)
    dotAt(cs[i].sx, cs[i].sy, cs[i].cr, cs[i].cg, cs[i].cb, 3);
}

// Full frame: bg texture (or clear) + objects (models or dots) + holes +
// player (human model or squares) + line.
// bank/texc null = old dot path. No-op (false) when r is null.
inline bool Draw(SDL_Renderer *r, SDL_Texture *bg, int bgW, int bgH,
                 const std::vector<ves::ObjGroup> &groups,
                 const objects::ObjTable *tab, const Camera &cam,
                 const Player &pl, const std::vector<Hole> &holes,
                 bool lineOut, float lineX, float lineY, int winW, int winH,
                 models::Bank *bank = nullptr,
                 models::TexCache *texc = nullptr) {
  if (!r) return false;
  SDL_FRect src = SrcRect(cam, bgW, bgH);
  SDL_FRect dst = DstRect(bgW, bgH, winW, winH);
  if (bg && bgW > 0 && bgH > 0) {
    SDL_RenderTexture(r, bg, &src, &dst);
  } else {
    SDL_SetRenderDrawColor(r, 20, 40, 60, 255);
    SDL_RenderClear(r);
  }
  auto dot = [&](float wx, float wy, uint8_t cr, uint8_t cg, uint8_t cb,
                 float size) {
    float bx, by, sx, sy;
    if (bgW > 0 && bgH > 0) {
      WorldToBg(wx, wy, bgW, bgH, bx, by);
      if (!BgToScreen(bx, by, src, dst, sx, sy)) return;
    } else {  // empty scene: old direct world->window mapping
      sx = (wx + 128.f) / 256.f * winW;
      sy = (wy + 128.f) / 256.f * winH;
    }
    sy = PitchY(sy, dst, cam.pitch);
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_FRect d{sx - size / 2, sy - size / 2, size, size};
    SDL_RenderFillRect(r, &d);
  };
  if (tab) {
    if (bank && texc)
      DrawObjects(r, *bank, *texc, groups, tab, cam, bgW, bgH, src, dst, winW,
                  winH);
    else
      for (auto &g : groups)
        for (auto &rec : g) {
          float wx = rec.x / 16777216.f, wy = rec.y / 16777216.f;
          if (Mapped(tab, rec.cls))
            dot(wx, wy, 255, (uint8_t)(80 + (rec.cls * 37) % 175),
                (uint8_t)(60 + (rec.cls * 91) % 195), 3);
          else
            dot(wx, wy, 255, 0, 0, 3);
        }
  }
  for (auto &h : holes) {
    float bx, by, sx, sy;
    bool ok = true;
    if (bgW > 0 && bgH > 0) {
      WorldToBg(h.x, h.y, bgW, bgH, bx, by);
      ok = BgToScreen(bx, by, src, dst, sx, sy);
    } else {
      sx = (h.x + 128.f) / 256.f * winW;
      sy = (h.y + 128.f) / 256.f * winH;
    }
    if (!ok) continue;
    sy = PitchY(sy, dst, cam.pitch);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_FRect rim{sx - 4, sy - 4, 8, 8};
    SDL_RenderFillRect(r, &rim);
    SDL_SetRenderDrawColor(r, 10, 10, 15, 255);
    SDL_FRect inner{sx - 3, sy - 3, 6, 6};
    SDL_RenderFillRect(r, &inner);
  }
  {  // player: assembled human model, else white square + red center
    float bx, by, sx, sy;
    bool ok = true;
    if (bgW > 0 && bgH > 0) {
      WorldToBg(pl.x, pl.y, bgW, bgH, bx, by);
      ok = BgToScreen(bx, by, src, dst, sx, sy);
    } else {
      sx = (pl.x + 128.f) / 256.f * winW;
      sy = (pl.y + 128.f) / 256.f * winH;
    }
    if (ok) {
      sy = PitchY(sy, dst, cam.pitch);
      bool drew = false;
      if (bank && texc) {
        const models::Mesh &h = bank->Human();
        if (h.ok) {
          float hpix = ClampF(30.f * ClampF(cam.zoom, 1.f, 8.f), 12.f, 150.f);
          drew = models::DrawOne(r, *bank, *texc, h, 35, 0.35f + cam.pitch,
                                  hpix, sx, sy);
        }
      }
      if (!drew) {
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_FRect d{sx - 3, sy - 3, 6, 6};
        SDL_RenderFillRect(r, &d);
        SDL_SetRenderDrawColor(r, 220, 30, 30, 255);
        SDL_FRect c{sx - 1, sy - 1, 2, 2};
        SDL_RenderFillRect(r, &c);
      }
      if (lineOut) {
        float ex, ey, lx, ly;
        if (bgW > 0 && bgH > 0) {
          WorldToBg(lineX, lineY, bgW, bgH, ex, ey);
          if (BgToScreen(ex, ey, src, dst, lx, ly)) {
            ly = PitchY(ly, dst, cam.pitch);
            SDL_SetRenderDrawColor(r, 230, 230, 230, 255);
            SDL_RenderLine(r, sx, sy, lx, ly);
          }
        } else {
          lx = (lineX + 128.f) / 256.f * winW;
          ly = (lineY + 128.f) / 256.f * winH;
          ly = PitchY(ly, dst, cam.pitch);
          SDL_SetRenderDrawColor(r, 230, 230, 230, 255);
          SDL_RenderLine(r, sx, sy, lx, ly);
        }
      }
    }
  }
  return true;
}

}  // namespace render
