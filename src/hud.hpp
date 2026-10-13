// In-game HUD (PLAY/PAUSE/RESULTS): depth gauge + timer + tally drawn from
// the original .spr assets over the PLAY background. Runtime load from
// extracted/ only; anything missing leaves the asset null and the caller
// falls back to ui::Render debug text (hud::Ready == fonts loaded).
// Bitmap font: fonts/fnt.spr (7x8) + fnt_iso.spr (18x23), glyph frame =
// ASCII - 0x20 (verified on the 181-frame sheets: frame 0 = space .. 94 =
// '~'); glyphs are drawn recoloured (source pixels are black/white with
// palette key 0x09 transparent) and tinted per draw via color mod.
// Layout is the port's design: the original panel placement (004bc9e0/
// 004bd330) is not decompiled yet. ponytail: fixed native-pixel layout,
// uniform scale only when the window is short.
#pragma once
#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

#include "spr.hpp"
#include "ui.hpp"

namespace hud {

// ---- pure helpers (tested) ----

// Glyph frame in the font atlas; unreadable bytes fall back to '.'.
inline int Glyph(unsigned char c, int nf) {
  int f = (int)c - 0x20;
  if (f < 0 || f >= nf) f = (int)'.' - 0x20;
  return f;
}

// Lure needle position over the 10..40 m line buckets (game::kLineM).
inline float NeedleFrac(float lineDepthM) {
  float f = (lineDepthM - 10.f) / 30.f;
  return f < 0 ? 0 : (f > 1 ? 1 : f);
}

// d_base.spr fill frame: deep water = tall fill (frame 0), shallow = last.
inline int BarFrame(float depthM, int nf) {
  if (nf < 2) return 0;
  float f = 1.f - depthM / 40.f;  // 40 m = deepest line bucket
  int i = (int)(f * (nf - 1) + 0.5f);
  return i < 0 ? 0 : (i >= nf ? nf - 1 : i);
}

struct Font {
  SDL_Texture *tex = nullptr;
  int gw = 0, gh = 0, nf = 0;  // glyph cell + frame count in the atlas
};

struct Assets {
  Font small, big;                          // fonts/fnt.spr, fonts/fnt_iso.spr
  SDL_Texture *up = nullptr, *down = nullptr, *needle = nullptr;
  std::vector<SDL_Texture *> bar;           // depthbar/d_base.spr frames
  std::vector<SDL_Texture *> icons;         // clock/clk_1..5.spr round icons
  int upW = 0, upH = 0, downW = 0, downH = 0;
  int barW = 0, barH = 0, needleW = 0, needleH = 0;
  int iconW = 0, iconH = 0;
};

inline void Free(Assets &a) {
  if (a.small.tex) SDL_DestroyTexture(a.small.tex);
  if (a.big.tex) SDL_DestroyTexture(a.big.tex);
  if (a.up) SDL_DestroyTexture(a.up);
  if (a.down) SDL_DestroyTexture(a.down);
  if (a.needle) SDL_DestroyTexture(a.needle);
  for (auto *t : a.bar)
    if (t) SDL_DestroyTexture(t);
  for (auto *t : a.icons)
    if (t) SDL_DestroyTexture(t);
  a = Assets{};
}

inline bool Ready(const Assets &a) { return a.small.tex && a.big.tex; }

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
  if (t) {
    SDL_UpdateTexture(t, nullptr, rgba.data(), w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  }
  return t;
}

// Font atlas: every glyph frame side by side, ink recoloured to white so
// SDL_SetTextureColorMod can tint it. No renderer = no texture (tests).
inline Font LoadFont(SDL_Renderer *r, const char *path) {
  Font f;
  auto d = ReadFile(path);
  if (d.empty()) return f;
  try {
    spr::Sprite s = spr::Decode(d.data(), d.size());
    if (s.bpp != 1 || s.nf < 95 || s.w <= 0 || s.h <= 0) return f;
    std::vector<uint8_t> rgba((size_t)s.w * s.h * s.nf * 4, 0);
    for (int fr = 0; fr < s.nf; fr++)
      for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++) {
          uint8_t k = s.idx[(size_t)fr * s.w * s.h + (size_t)y * s.w + x];
          if (k == (uint8_t)(s.key & 0xFF)) continue;  // key = transparent
          size_t o = ((size_t)y * s.w * s.nf + (size_t)fr * s.w + x) * 4;
          rgba[o] = rgba[o + 1] = rgba[o + 2] = 255;
          rgba[o + 3] = 255;
        }
    f.tex = MakeTex(r, rgba, s.w * s.nf, s.h);
    if (f.tex) {
      f.gw = s.w;
      f.gh = s.h;
      f.nf = s.nf;
    }
  } catch (...) {
  }
  return f;
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

inline void Load(Assets &a, SDL_Renderer *r) {
  if (!r) return;
  if (!a.small.tex) a.small = LoadFont(r, "extracted/gfx/spr/fonts/fnt.spr");
  if (!a.big.tex)
    a.big = LoadFont(r, "extracted/gfx/spr/fonts/fnt_iso.spr");
  if (!a.needle) {
    auto d = ReadFile("extracted/gfx/spr/depthbar/d_lure.spr");
    try {
      if (!d.empty()) {
        spr::Sprite s = spr::Decode(d.data(), d.size());
        std::vector<uint8_t> rgba;
        if (SprFrameRGBA(s, 0, rgba)) {
          a.needle = MakeTex(r, rgba, s.w, s.h);
          a.needleW = s.w;
          a.needleH = s.h;
        }
      }
    } catch (...) {
    }
  }
  if (!a.up) {
    auto d = ReadFile("extracted/gfx/spr/depthbar/d_up.spr");
    try {
      if (!d.empty()) {
        spr::Sprite s = spr::Decode(d.data(), d.size());
        std::vector<uint8_t> rgba;
        if (SprFrameRGBA(s, 0, rgba)) {
          a.up = MakeTex(r, rgba, s.w, s.h);
          a.upW = s.w;
          a.upH = s.h;
        }
      }
    } catch (...) {
    }
  }
  if (!a.down) {
    auto d = ReadFile("extracted/gfx/spr/depthbar/d_down.spr");
    try {
      if (!d.empty()) {
        spr::Sprite s = spr::Decode(d.data(), d.size());
        std::vector<uint8_t> rgba;
        if (SprFrameRGBA(s, 0, rgba)) {
          a.down = MakeTex(r, rgba, s.w, s.h);
          a.downW = s.w;
          a.downH = s.h;
        }
      }
    } catch (...) {
    }
  }
  if (a.bar.empty()) {
    auto d = ReadFile("extracted/gfx/spr/depthbar/d_base.spr");
    try {
      if (!d.empty()) {
        spr::Sprite s = spr::Decode(d.data(), d.size());
        for (int f = 0; f < s.nf && f < 8; f++) {
          std::vector<uint8_t> rgba;
          if (!SprFrameRGBA(s, f, rgba)) continue;
          a.bar.push_back(MakeTex(r, rgba, s.w, s.h));
        }
        a.barW = s.w;
        a.barH = s.h;
      }
    } catch (...) {
    }
  }
  for (int i = 1; i <= 5 && (int)a.icons.size() < 5; i++) {
    char p[96];
    snprintf(p, sizeof p, "extracted/gfx/spr/clock/clk_%d.spr", i);
    auto d = ReadFile(p);
    try {
      if (d.empty()) continue;
      spr::Sprite s = spr::Decode(d.data(), d.size());
      std::vector<uint8_t> rgba;
      if (!SprFrameRGBA(s, 0, rgba)) continue;
      SDL_Texture *t = MakeTex(r, rgba, s.w, s.h);
      if (t) {
        a.icons.push_back(t);
        a.iconW = s.w;
        a.iconH = s.h;
      }
    } catch (...) {
    }
  }
}

// ---- drawing ----

// Atlas glyph blit; scale keeps the crisp bitmap look.
inline void Text(SDL_Renderer *r, const Font &f, float x, float y, float s,
                 SDL_Color col, const std::string &str) {
  if (!r || !f.tex) return;
  SDL_SetTextureColorMod(f.tex, col.r, col.g, col.b);
  SDL_SetTextureAlphaMod(f.tex, col.a);
  for (size_t i = 0; i < str.size(); i++) {
    SDL_FRect src = {(float)(Glyph((unsigned char)str[i], f.nf) * f.gw), 0,
                     (float)f.gw, (float)f.gh};
    SDL_FRect dst = {x + (float)i * f.gw * s, y, f.gw * s, f.gh * s};
    SDL_RenderTexture(r, f.tex, &src, &dst);
  }
  SDL_SetTextureColorMod(f.tex, 255, 255, 255);
  SDL_SetTextureAlphaMod(f.tex, 255);
}

// White-on-shadow label so text stays readable over the lake.
inline void TextShade(SDL_Renderer *r, const Font &f, float x, float y,
                      float s, SDL_Color col, const std::string &str) {
  Text(r, f, x + 2 * s, y + 2 * s, s, SDL_Color{0, 0, 0, 160}, str);
  Text(r, f, x, y, s, col, str);
}

inline float TextW(const Font &f, float s, const std::string &str) {
  return (float)str.size() * f.gw * s;
}

inline void Blit(SDL_Renderer *r, SDL_Texture *t, float x, float y, float w,
                 float h) {
  if (!r || !t) return;
  SDL_FRect d = {x, y, w, h};
  SDL_RenderTexture(r, t, nullptr, &d);
}

// The full HUD. Returns false if the fonts are missing (caller falls back
// to ui::Render). Pause/Results draw the play HUD plus their overlay.
inline bool Draw(SDL_Renderer *r, int w, int h, const ui::Game &g,
                 const ui::Hud &hud, const Assets &a) {
  if (!Ready(a)) return false;
  const float s = h < 560 ? (float)h / 560.f : 1.f;
  const SDL_Color white{255, 255, 255, 255};
  const SDL_Color yellow{255, 220, 80, 255};
  const SDL_Color cyan{140, 220, 255, 255};
  char b[128];

  // Left: depth gauge (up button, fill bar, down button) + lure needle.
  const float gx = 20 * s;
  const float stack = (a.upH + a.barH + a.downH) * s;
  const float gy = ((float)h - stack) / 2;
  Blit(r, a.up, gx, gy, a.upW * s, a.upH * s);
  const float barY = gy + a.upH * s;
  if (!a.bar.empty()) {
    SDL_Texture *t = a.bar[(size_t)BarFrame(hud.depthAt < 0 ? 0.f
                                                           : (float)hud.depthAt,
                                            (int)a.bar.size())];
    Blit(r, t, gx, barY, a.barW * s, a.barH * s);
  }
  Blit(r, a.down, gx, barY + a.barH * s, a.downW * s, a.downH * s);
  if (a.needle) {  // 10..40 m buckets over the bar, needle stays inside
    float ny = barY + NeedleFrac((float)hud.lineDepth) *
                          (a.barH - a.needleH) * s;
    Blit(r, a.needle, gx + (a.barW - a.needleW) * s / 2, ny,
         a.needleW * s, a.needleH * s);
  }

  // Right of the gauge: lake, time, depth, line, tally.
  float tx = gx + a.barW * s + 20 * s;
  float ty = 20 * s;
  TextShade(r, a.big, tx, ty, s, white, g.lakeName);
  ty += 30 * s;
  TextShade(r, a.big, tx, ty, s, yellow, ui::MmSs(g.timeLeft));
  ty += 30 * s;
  snprintf(b, sizeof b, "syvyys %d m", hud.depthAt);
  TextShade(r, a.small, tx, ty, s * 1.5f, cyan, b);
  ty += 16 * s;
  snprintf(b, sizeof b, "siima  %d m", hud.lineDepth);
  TextShade(r, a.small, tx, ty, s * 1.5f, white, b);
  ty += 16 * s;
  snprintf(b, sizeof b, "kala   %d  %.1f kg", hud.catches, hud.kg);
  TextShade(r, a.small, tx, ty, s * 1.5f, white, b);
  ty += 16 * s;
  snprintf(b, sizeof b, "reiat  %d", hud.holes);
  TextShade(r, a.small, tx, ty, s * 1.5f, white, b);

  // Bottom right: round icon buttons (fish / bait / reel), count below each.
  if (!a.icons.empty()) {
    const float is = 48.f * s / (a.iconH ? a.iconH : 1);
    const float iw = a.iconW * is, ih = a.iconH * is;
    const float iy = (float)h - 124 * s, cy = iy + ih + 6 * s;
    snprintf(b, sizeof b, "%dm", hud.lineDepth);
    std::string cnt[3] = {b, std::to_string(hud.baitLeft),
                          std::to_string(hud.catches)};
    size_t pick[3] = {2, 1, 3};  // reel, bait, fish (fish rightmost)
    float ix = (float)w - 24 * s - iw;
    for (int k = 0; k < 3; k++) {
      size_t fi = pick[k] < a.icons.size() ? pick[k] : 0;
      Blit(r, a.icons[fi], ix, iy, iw, ih);
      TextShade(r, a.big, ix + (iw - TextW(a.big, s, cnt[k])) / 2, cy, s,
                white, cnt[k]);
      ix -= iw + 24 * s;
    }
  }

  // Bite flash + hints.
  if (g.state == ui::State::Play && hud.bite) {
    const std::string v = "VETO!";
    TextShade(r, a.big, ((float)w - TextW(a.big, s * 2, v)) / 2, 20 * s,
              s * 2, yellow, v);
    const std::string k = "vlily = vli";
    TextShade(r, a.small,
              ((float)w - TextW(a.small, s * 1.5f, k)) / 2, 70 * s,
              s * 1.5f, yellow, k);
  }

  const float hy = (float)h - 24 * s - 14 * s;
  switch (g.state) {
    case ui::State::Play:
      TextShade(r, a.small, gx, hy, s * 1.5f, white,
                "Nuolet liiku  Enter pora  Vlily kala  PgUp/PgDn siima  "
                "R hiiri  Esc tauko");
      break;
    case ui::State::Pause: {
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(r, 0, 0, 0, 120);
      SDL_FRect all = {0, 0, (float)w, (float)h};
      SDL_RenderFillRect(r, &all);
      const std::string p = "TAUKO";
      TextShade(r, a.big, ((float)w - TextW(a.big, s * 2, p)) / 2,
                (float)h / 2 - 60 * s, s * 2, white, p);
      const std::string k = "Enter / Esc jatka";
      TextShade(r, a.small, ((float)w - TextW(a.small, s * 1.5f, k)) / 2,
                (float)h / 2 + 20 * s, s * 1.5f, white, k);
      break;
    }
    case ui::State::Results: {
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(r, 0, 0, 0, 120);
      SDL_FRect all = {0, 0, (float)w, (float)h};
      SDL_RenderFillRect(r, &all);
      float ry = (float)h / 2 - 90 * s;
      TextShade(r, a.big, 40 * s, ry, s, white, g.lakeName);
      ry += 34 * s;
      snprintf(b, sizeof b, "Kalojen yhteispaino %.1f kg", hud.kg);
      TextShade(r, a.small, 40 * s, ry, s * 1.5f, yellow, b);
      ry += 20 * s;
      snprintf(b, sizeof b, "Isoin kala %.1f kg", hud.biggest);
      TextShade(r, a.small, 40 * s, ry, s * 1.5f, yellow, b);
      ry += 20 * s;
      snprintf(b, sizeof b, "Kaloja %d  Reikia %d", hud.catches, hud.holes);
      TextShade(r, a.small, 40 * s, ry, s * 1.5f, white, b);
      ry += 30 * s;
      TextShade(r, a.small, 40 * s, ry, s * 1.5f, white,
                "R uudelleen  Enter jaat  Esc poistu");
      break;
    }
    default:
      break;
  }
  return true;
}

}  // namespace hud
