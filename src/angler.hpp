// Visible angler + holes + rod/line overlay (SDL3 only).
// Reuses spr::Decode + pcx::Decode, never rewrites them. Runtime load from
// extracted/ only; anything missing falls back to drawn shapes, never
// hard-fails. Pause/menus freeze animation: caller passes simLive + dt and
// timers advance only when simLive is true.
// Sprite paths used:
//   extracted/gfx/txt/hum/HUMSHAD.PCX (64x64 shadow under angler)
//   extracted/gfx/spr/rods/rods.spr (7 frames, frame 0 rod icon by hands)
//   extracted/gfx/spr/lures/hook/hook_win_kala.spr (4 frames, catch popup)
// No hum body .spr exists in tree, so body always drawn (coat+hat), facing
// movement direction. Fish underwater dots kept as fallback only when the
// catch sprite is missing.
#pragma once
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "game.hpp"
#include "pcx.hpp"
#include "render.hpp"
#include "spr.hpp"

namespace angler {

// Pure helpers (tested, no renderer needed).

// Outer bounds of a drilled hole (chips rim included), screen px.
inline SDL_FRect HoleRect(float sx, float sy, float s) {
  float w = 22 * s, h = 14 * s;
  return {sx - w / 2, sy - h / 2, w, h};
}

// Jig bobber offset from game::jigPhase. Bounded +/-3.
inline float JigOffset(float phase) { return sinf(phase) * 3.f; }

// Facing from horizontal movement with deadband. Returns 1 or -1.
inline int FacingUpdate(float lastX, float x, int facing) {
  float d = x - lastX;
  if (d > 0.2f) return 1;
  if (d < -0.2f) return -1;
  return facing >= 0 ? 1 : -1;
}

struct Assets {
  SDL_Texture *shad = nullptr;
  SDL_Texture *rod = nullptr;
  std::vector<SDL_Texture *> fishFrames;
  int shadW = 0, shadH = 0, rodW = 0, rodH = 0, fishW = 0, fishH = 0;
  int facing = 1;
  float lastX = 0;
  bool haveLast = false;
  float t = 0;        // anim clock, advances only when simLive
  float catchT = 0;   // catch popup time left
  float catchKg = 0;  // last catch weight
};

inline void Free(Assets &a) {
  if (a.shad) SDL_DestroyTexture(a.shad);
  if (a.rod) SDL_DestroyTexture(a.rod);
  for (auto *t : a.fishFrames)
    if (t) SDL_DestroyTexture(t);
  a.shad = a.rod = nullptr;
  a.fishFrames.clear();
}

inline std::vector<uint8_t> ReadFile(const char *p) {
  FILE *f = fopen(p, "rb");
  if (!f) return {};
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> d(n > 0 ? (size_t)n : 0);
  if (n > 0 && fread(d.data(), 1, (size_t)n, f) != (size_t)n) d.clear();
  fclose(f);
  return d;
}

inline SDL_Texture *MakeTex(SDL_Renderer *r, const std::vector<uint8_t> &rgba,
                            int w, int h) {
  if (!r || w <= 0 || h <= 0 || rgba.size() != (size_t)w * h * 4)
    return nullptr;
  SDL_Texture *t =
      SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                        w, h);
  if (t) SDL_UpdateTexture(t, nullptr, rgba.data(), w * 4);
  return t;
}

// One .spr frame to RGBA, transparent where pixel == transkey.
inline bool SprFrameRGBA(const spr::Sprite &s, int f,
                         std::vector<uint8_t> &rgba) {
  size_t n = (size_t)s.w * s.h;
  if (f < 0 || f >= s.nf || n == 0) return false;
  rgba.resize(n * 4);
  for (size_t i = 0; i < n; i++) {
    uint8_t cr, cg, cb, a = 255;
    if (s.bpp == 2) {
      uint16_t v = s.px[(size_t)f * n + i];
      if (v == s.key) {
        a = 0;
        cr = cg = cb = 0;
      } else {
        cr = (uint8_t)((v >> 11) * 255 / 31);
        cg = (uint8_t)(((v >> 5) & 0x3F) * 255 / 63);
        cb = (uint8_t)((v & 0x1F) * 255 / 31);
      }
    } else {
      uint8_t k = s.idx[(size_t)f * n + i];
      if (k == (uint8_t)(s.key & 0xFF)) {
        a = 0;
        cr = cg = cb = 0;
      } else {
        cr = s.pal[3 * k];
        cg = s.pal[3 * k + 1];
        cb = s.pal[3 * k + 2];
      }
    }
    rgba[4 * i] = cr;
    rgba[4 * i + 1] = cg;
    rgba[4 * i + 2] = cb;
    rgba[4 * i + 3] = a;
  }
  return true;
}

// Missing files or undecodable blobs leave nullptrs (drawn fallback).
inline void Load(Assets &a, SDL_Renderer *r) {
  if (!r) return;
  if (!a.shad) {
    auto d = ReadFile("extracted/gfx/txt/hum/HUMSHAD.PCX");
    if (!d.empty()) {
      try {
        pcx::Image im = pcx::Decode(d);
        std::vector<uint8_t> rgba;
        if (im.planes == 1 && im.pal.size() >= 768 &&
            im.idx.size() == (size_t)im.w * im.h) {
          rgba.resize((size_t)im.w * im.h * 4);
          for (size_t i = 0; i < (size_t)im.w * im.h; i++) {
            uint8_t k = im.idx[i];
            rgba[4 * i] = im.pal[3 * k];
            rgba[4 * i + 1] = im.pal[3 * k + 1];
            rgba[4 * i + 2] = im.pal[3 * k + 2];
            // ponytail: 0xFE bg key + flat shadow alpha is the ceiling;
            // per-pixel shadow from palette needs decomp of blend mode
            rgba[4 * i + 3] = (k == 0xFE) ? 0 : 170;
          }
        }
        if (!rgba.empty()) {
          a.shad = MakeTex(r, rgba, im.w, im.h);
          if (a.shad) {
            a.shadW = im.w;
            a.shadH = im.h;
          }
        }
      } catch (...) {
      }
    }
  }
  if (!a.rod) {
    auto d = ReadFile("extracted/gfx/spr/rods/rods.spr");
    if (!d.empty()) {
      try {
        spr::Sprite s = spr::Decode(d.data(), d.size());
        std::vector<uint8_t> rgba;
        if (SprFrameRGBA(s, 0, rgba)) {
          a.rod = MakeTex(r, rgba, s.w, s.h);
          if (a.rod) {
            a.rodW = s.w;
            a.rodH = s.h;
          }
        }
      } catch (...) {
      }
    }
  }
  if (a.fishFrames.empty()) {
    auto d = ReadFile("extracted/gfx/spr/lures/hook/hook_win_kala.spr");
    if (!d.empty()) {
      try {
        spr::Sprite s = spr::Decode(d.data(), d.size());
        for (int f = 0; f < s.nf && f < 8; f++) {
          std::vector<uint8_t> rgba;
          if (!SprFrameRGBA(s, f, rgba)) continue;
          SDL_Texture *t = MakeTex(r, rgba, s.w, s.h);
          if (t) a.fishFrames.push_back(t);
        }
        if (!a.fishFrames.empty()) {
          a.fishW = s.w;
          a.fishH = s.h;
        }
      } catch (...) {
      }
    }
  }
}

inline bool Ellipse(SDL_Renderer *r, float cx, float cy, float w, float h,
                    uint8_t cr, uint8_t cg, uint8_t cb) {
  if (!r || w <= 0 || h <= 0) return false;
  SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
  float rx = w / 2, ry = h / 2;
  int steps = (int)ry;
  if (steps < 1) steps = 1;
  for (int dy = -steps; dy <= steps; dy++) {
    float t = (float)dy / ry;
    if (t > 1) t = 1;
    if (t < -1) t = -1;
    float hw = rx * sqrtf(1 - t * t);
    SDL_FRect d{cx - hw, cy + dy, hw * 2, 1.5f};
    SDL_RenderFillRect(r, &d);
  }
  return true;
}

// Drilled hole: dark water ellipse + ice chips rim + glint.
inline bool DrawHole(SDL_Renderer *r, float sx, float sy, float s) {
  if (!r) return false;
  SDL_SetRenderDrawColor(r, 200, 220, 235, 255);
  for (int i = 0; i < 8; i++) {
    float an = i * 0.7853982f;
    SDL_FRect c{sx + cosf(an) * 12 * s - 1.5f, sy + sinf(an) * 8 * s - 1,
                3, 2};
    SDL_RenderFillRect(r, &c);
  }
  Ellipse(r, sx, sy, 20 * s, 11 * s, 8, 12, 20);
  Ellipse(r, sx, sy, 12 * s, 6 * s, 5, 8, 14);
  SDL_SetRenderDrawColor(r, 120, 180, 220, 255);
  SDL_FRect g{sx - 4 * s, sy - 3 * s, 8 * s, 1.5f};
  SDL_RenderFillRect(r, &g);
  return true;
}

// Drawn angler fallback (coat+hat), shadow sprite when loaded.
inline bool DrawAngler(SDL_Renderer *r, Assets &a, float sx, float sy,
                       int facing) {
  if (!r) return false;
  if (a.shad) {
    SDL_FRect d{sx - 14, sy + 10, 28, 14};
    SDL_RenderTexture(r, a.shad, nullptr, &d);
  } else {
    Ellipse(r, sx, sy + 12, 24, 8, 0, 0, 0);
  }
  float f = facing >= 0 ? 1 : -1;
  SDL_SetRenderDrawColor(r, 30, 30, 40, 255);
  SDL_FRect l1{sx - 5, sy + 4, 4, 9}, l2{sx + 1, sy + 4, 4, 9};
  SDL_RenderFillRect(r, &l1);
  SDL_RenderFillRect(r, &l2);
  SDL_SetRenderDrawColor(r, 30, 90, 180, 255);
  SDL_FRect c{sx - 7, sy - 10, 14, 15};
  SDL_RenderFillRect(r, &c);
  SDL_SetRenderDrawColor(r, 20, 60, 130, 255);
  SDL_FRect cb{sx - 7, sy + 2, 14, 3};
  SDL_RenderFillRect(r, &cb);
  SDL_SetRenderDrawColor(r, 30, 90, 180, 255);
  SDL_FRect arm{sx + (f > 0 ? 5 : -9), sy - 8, 5, 4};
  SDL_RenderFillRect(r, &arm);
  SDL_SetRenderDrawColor(r, 230, 190, 150, 255);
  SDL_FRect hd{sx - 4, sy - 19, 8, 9};
  SDL_RenderFillRect(r, &hd);
  SDL_SetRenderDrawColor(r, 200, 30, 30, 255);
  SDL_FRect ht{sx - 5, sy - 23, 10, 4};
  SDL_RenderFillRect(r, &ht);
  return true;
}

// Rod from hands toward hole + thin line to hole + jig bobber.
inline bool DrawRod(SDL_Renderer *r, Assets &a, float hx, float hy, float tx,
                    float ty, float jigPhase) {
  if (!r) return false;
  float tipx = hx + (tx - hx) * 0.55f, tipy = hy + (ty - hy) * 0.55f - 6;
  if (a.rod) {
    SDL_FRect d{hx - 7, hy - 36, 14, 42};
    SDL_RenderTexture(r, a.rod, nullptr, &d);
  } else {
    SDL_SetRenderDrawColor(r, 120, 70, 30, 255);
    SDL_RenderLine(r, hx, hy, tipx, tipy);
    SDL_RenderLine(r, hx + 1, hy, tipx + 1, tipy);
  }
  SDL_SetRenderDrawColor(r, 230, 230, 230, 255);
  SDL_RenderLine(r, tipx, tipy, tx, ty);
  float bo = JigOffset(jigPhase);
  SDL_SetRenderDrawColor(r, 240, 240, 240, 255);
  SDL_FRect b{tx - 2, ty - 2 + bo, 4, 4};
  SDL_RenderFillRect(r, &b);
  SDL_SetRenderDrawColor(r, 220, 30, 30, 255);
  SDL_FRect b2{tx - 1, ty - 3 + bo, 2, 2};
  SDL_RenderFillRect(r, &b2);
  return true;
}

// Bite flash ring, expands + strobes with anim clock (frozen when paused).
inline bool DrawBite(SDL_Renderer *r, float tx, float ty, float t) {
  if (!r) return false;
  float ph = t * 1.5f - floorf(t * 1.5f);
  float rad = 9 + 7 * ph;
  bool alt = ((int)(t * 6) % 2) == 0;
  SDL_SetRenderDrawColor(r, alt ? 255 : 200, alt ? 200 : 80, 30, 255);
  SDL_RenderLine(r, tx - rad, ty - rad, tx + rad, ty - rad);
  SDL_RenderLine(r, tx + rad, ty - rad, tx + rad, ty + rad);
  SDL_RenderLine(r, tx + rad, ty + rad, tx - rad, ty + rad);
  SDL_RenderLine(r, tx - rad, ty + rad, tx - rad, ty - rad);
  return true;
}

// Caught fish flop icon + weight popup. Sprite frames cycle when loaded,
// drawn orange fish otherwise.
inline bool DrawCatch(SDL_Renderer *r, Assets &a, float sx, float sy) {
  if (!r || a.catchT <= 0) return false;
  float wob = sinf(a.t * 20) * 4;
  if (!a.fishFrames.empty()) {
    SDL_Texture *ft = a.fishFrames[(size_t)(a.t * 6) % a.fishFrames.size()];
    SDL_FRect d{sx + 10, sy - 52, 40 + wob, 32};
    SDL_RenderTexture(r, ft, nullptr, &d);
  } else {
    SDL_SetRenderDrawColor(r, 240, 140, 20, 255);
    SDL_FRect b{sx + 10, sy - 44, 36 + wob, 12};
    SDL_RenderFillRect(r, &b);
    SDL_FRect tail{sx + 10 + 36 + wob - 6, sy - 42, 8, 8};
    SDL_RenderFillRect(r, &tail);
  }
  char buf[32];
  snprintf(buf, sizeof buf, "+%.2fkg", (double)a.catchKg);
  SDL_RenderDebugText(r, sx + 10, sy - 16, buf);
  return true;
}

inline bool ToScreen(float wx, float wy, const render::Camera &cam, int bgW,
                     int bgH, int winW, int winH, float &sx, float &sy) {
  SDL_FRect dst = render::DstRect(bgW, bgH, winW, winH);
  bool ok;
  if (bgW > 0 && bgH > 0) {
    float bx, by;
    render::WorldToBg(wx, wy, bgW, bgH, bx, by);
    SDL_FRect src = render::SrcRect(cam, bgW, bgH);
    ok = render::BgToScreen(bx, by, src, dst, sx, sy);
  } else {
    sx = (wx + 128.f) / 256.f * winW;
    sy = (wy + 128.f) / 256.f * winH;
    ok = true;
  }
  if (ok) sy = render::PitchY(sy, dst, cam.pitch);
  return ok;
}

// Full overlay: holes + angler + rod/line + bite ring + catch popup.
// haveModel: 3D human already draws the body, overlay adds shadow only.
// Underwater fish dots drawn only as fallback (no catch sprite loaded).
// No-op (false) when r is null. Never throws.
inline bool DrawOverlay(SDL_Renderer *r, Assets &a,
                        const render::Camera &cam, int bgW, int bgH, int winW,
                        int winH, const game::Game &g, float px, float py,
                        bool lineOut, float lineX, float lineY, bool simLive,
                        float dt, bool haveModel = false) {
  if (!r) return false;
  if (dt < 0) dt = 0;
  if (simLive) {
    a.t += dt;
    if (!a.haveLast) {
      a.lastX = px;
      a.haveLast = true;
    }
    a.facing = FacingUpdate(a.lastX, px, a.facing);
    a.lastX = px;
    if (g.justCaught) {
      a.catchT = 2.f;
      a.catchKg = (float)g.lastKg;
    }
    if (a.catchT > 0) a.catchT -= dt;
  }
  for (auto &h : g.holes) {
    float hx, hy;
    if (ToScreen(h.x, h.y, cam, bgW, bgH, winW, winH, hx, hy))
      DrawHole(r, hx, hy, 1);
  }
  float sx, sy;
  if (!ToScreen(px, py, cam, bgW, bgH, winW, winH, sx, sy)) return true;
  // Rod target: last line pos, else nearest hole, else feet.
  float tx = sx, ty = sy;
  if (lineOut) {
    ToScreen(lineX, lineY, cam, bgW, bgH, winW, winH, tx, ty);
  } else if (!g.holes.empty()) {
    float bd = 1e30f;
    for (auto &h : g.holes) {
      float dx = h.x - px, dy = h.y - py, d = dx * dx + dy * dy;
      if (d < bd) {
        bd = d;
        ToScreen(h.x, h.y, cam, bgW, bgH, winW, winH, tx, ty);
      }
    }
  }
  if (g.Biting()) DrawBite(r, tx, ty, a.t);
  float hx, hy;
  if (haveModel) {  // hands on the 3D figure, same hpix as render::Draw
    float hpix =
        render::ClampF(30.f * render::ClampF(cam.zoom, 1.f, 8.f), 12.f, 150.f);
    hx = sx + (a.facing >= 0 ? 10 : -10);
    hy = sy - hpix * 0.5f;
    if (a.shad) {
      SDL_FRect d{sx - 14, sy + 10, 28, 14};
      SDL_RenderTexture(r, a.shad, nullptr, &d);
    } else {
      Ellipse(r, sx, sy + 12, 24, 8, 0, 0, 0);
    }
  } else {
    hx = sx + (a.facing >= 0 ? 9 : -9);
    hy = sy - 8;
    DrawAngler(r, a, sx, sy, a.facing);
  }
  DrawRod(r, a, hx, hy, tx, ty, g.jigPhase);
  DrawCatch(r, a, sx, sy);
  if (a.fishFrames.empty()) {  // fallback: underwater dots
    for (auto &f : g.fishes) {
      float fx, fy;
      if (!ToScreen(f.x, f.y, cam, bgW, bgH, winW, winH, fx, fy)) continue;
      bool hot = f.st != game::State::Idle;
      SDL_SetRenderDrawColor(r, hot ? 255 : 80, hot ? 140 : 220,
                             hot ? 20 : 80, 255);
      float s = hot ? 5 : 3;
      SDL_FRect d{fx - s / 2, fy - s / 2, s, s};
      SDL_RenderFillRect(r, &d);
    }
  }
  return true;
}

}  // namespace angler
