// Full-screen menu over PCX art (no TTF dep: SDL_RenderDebugText on top).
// Title + LakeSelect only; Play/Pause/Results stay in ui::Render.
// Bg: extracted/gfx/pic/menu/menu.pcx first, alku.pcx spare. Missing or
// bad art = gradient fallback, never hard-fails. Dummy-driver safe.
// Comp setup row: Left/Right = time limit, PgUp/PgDn = bots 0..8.
// Click: lake row = pick, setup halves = time cycle / bots +1.
#pragma once
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "pcx.hpp"
#include "ui.hpp"

namespace menu {

struct Setup {
  int timeSec = ui::kDefaultTimeSec;
  int bots = 5;
};

struct Back {
  SDL_Texture *tex = nullptr;
  bool tried = false;
};

inline constexpr int kRowH = 12;
inline constexpr int kListTop = 3;  // first lake row, in text rows
inline constexpr int kMaxBots = 8;
inline const int kTimes[] = {300, 600, 900, 1800, 3600};

inline void CycleTime(Setup &s, int dir) {
  int best = 1;
  for (int i = 0; i < 5; i++)
    if (abs(kTimes[i] - s.timeSec) < abs(kTimes[best] - s.timeSec)) best = i;
  s.timeSec = kTimes[(best + dir + 50 * 5) % 5];
}

// Keyboard: setup keys in LakeSelect, everything else -> ui::Press.
inline void MenuPress(ui::Game &g, Setup &s, uint32_t id) {
  if (g.state == ui::State::LakeSelect) {
    if (id == 2) return CycleTime(s, -1);
    if (id == 3) return CycleTime(s, +1);
    if (id == 4) {
      s.bots = std::max(0, s.bots - 1);
      return;
    }
    if (id == 5) {
      s.bots = std::min(kMaxBots, s.bots + 1);
      return;
    }
  }
  ui::Press(g, id);
}

inline int VisibleRows(int winh) {
  return std::max(5, (winh - kListTop * kRowH - 60) / kRowH);
}

inline int StartIndex(int n, int sel, int rows) {
  if (n <= rows) return 0;
  return std::min(std::max(0, sel - rows / 2), n - rows);
}

// Mouse. Returns true when the click did something.
inline bool Click(ui::Game &g, Setup &s, int x, int y, int winw, int winh) {
  if (g.state == ui::State::Title) {
    ui::Press(g, 6);
    return true;
  }
  if (g.state != ui::State::LakeSelect || g.lakes.empty()) return false;
  int rows = VisibleRows(winh);
  int idx = (y - kListTop * kRowH) / kRowH;
  int start = StartIndex((int)g.lakes.size(), g.sel, rows);
  if (idx >= 0 && idx < rows && start + idx < (int)g.lakes.size()) {
    g.sel = start + idx;
    g.pendIdx = g.sel;
    g.needLoad = true;
    return true;
  }
  if (y >= winh - 54 && y <= winh - 38) {  // setup row halves
    if (x < winw / 2)
      CycleTime(s, +1);
    else
      s.bots = s.bots >= kMaxBots ? 0 : s.bots + 1;
    return true;
  }
  return false;
}

// First decodable candidate wins; all fail -> tex stays null (gradient).
inline void EnsureBack(SDL_Renderer *r, Back &b) {
  if (b.tried || !r) return;
  b.tried = true;
  static const char *cands[] = {
      "extracted/gfx/pic/menu/menu.pcx",
      "extracted/gfx/pic/menu/alku.pcx",
      "extracted/gfx/title.pcx",
  };
  for (auto c : cands) {
    try {
      pcx::Image im = pcx::Decode(ui::ReadAll(c));
      if (im.w < 1 || im.h < 1) continue;
      std::vector<uint8_t> rgb((size_t)im.w * im.h * 3);
      if (im.planes == 1) {
        for (size_t i = 0; i < (size_t)im.w * im.h; i++) {
          rgb[3 * i] = im.pal[3 * im.idx[i]];
          rgb[3 * i + 1] = im.pal[3 * im.idx[i] + 1];
          rgb[3 * i + 2] = im.pal[3 * im.idx[i] + 2];
        }
      } else if (im.planes == 3) {
        rgb = im.px;
      } else {
        for (size_t i = 0; i < (size_t)im.w * im.h; i++) {
          rgb[3 * i] = im.px[4 * i];
          rgb[3 * i + 1] = im.px[4 * i + 1];
          rgb[3 * i + 2] = im.px[4 * i + 2];
        }
      }
      SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB24,
                                         SDL_TEXTUREACCESS_STATIC, im.w, im.h);
      if (!t) continue;
      if (!SDL_UpdateTexture(t, nullptr, rgb.data(), 3 * im.w)) {
        SDL_DestroyTexture(t);
        continue;
      }
      b.tex = t;
      return;
    } catch (...) {
    }
  }
}

inline void FreeBack(Back &b) {
  if (b.tex) {
    SDL_DestroyTexture(b.tex);
    b.tex = nullptr;
  }
}

inline void Draw(SDL_Renderer *r, Back &b, ui::Game &g, Setup &s, int winw,
                 int winh) {
  if (!r) return;
  if (g.state != ui::State::Title &&
      g.state != ui::State::LakeSelect)
    return;
  EnsureBack(r, b);
  if (b.tex) {
    SDL_RenderTexture(r, b.tex, nullptr, nullptr);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 110);
    SDL_FRect full{(float)0, (float)0, (float)winw, (float)winh};
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  } else {
    for (int i = 0; i < 32; i++) {  // gradient fallback, never hard-fails
      uint8_t t = (uint8_t)(i * 255 / 31);
      SDL_SetRenderDrawColor(r, 4 + t / 16, 8 + t / 12, 20 + t / 6, 255);
      SDL_FRect band{(float)0, (float)i * winh / 32, (float)winw,
                     (float)winh / 32 + 1};
      SDL_RenderFillRect(r, &band);
    }
  }
  SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
  auto put = [&](int row, const std::string &t) {
    SDL_RenderDebugText(r, 16, (float)row * kRowH, t.substr(0, 90).c_str());
  };
  auto putC = [&](int row, const std::string &t) {
    float x = (winw - (int)t.size() * 8) / 2.f;
    SDL_RenderDebugText(r, x < 8 ? 8 : x, (float)row * kRowH, t.c_str());
  };
  char buf[160];
  if (g.state == ui::State::Title) {
    int mid = winh / 2 / kRowH;
    putC(mid - 2, "ModernPilkki (offline)");
    if (g.lakes.empty()) {
      putC(mid, "no lakes in extracted/lakes");
    } else {
      snprintf(buf, sizeof buf, "Enter/click: fish (%zu lakes)   Esc: quit",
               g.lakes.size());
      putC(mid, buf);
    }
    putC(mid + 2, "Arrows+Enter keyboard, mouse click works");
    return;
  }
  put(0, "ModernPilkki - pick lake");
  put(1, "Up/Down+Enter keyboard, click lake, Esc back");
  int rows = VisibleRows(winh);
  int n = (int)g.lakes.size();
  int start = StartIndex(n, g.sel, rows);
  for (int i = 0; i < rows && start + i < n; i++) {
    int j = start + i;
    std::string t = (j == g.sel ? "> " : "  ") + g.lakes[(size_t)j].name;
    if (!g.lakes[(size_t)j].region.empty())
      t += " - " + g.lakes[(size_t)j].region;
    put(kListTop + i, t);
  }
  snprintf(buf, sizeof buf, "Time %s  Bots %d  (Left/Right time, PgUp/PgDn bots)",
           ui::MmSs((float)s.timeSec).c_str(), s.bots);
  SDL_RenderDebugText(r, 16, (float)(winh - 48), buf);
  SDL_RenderDebugText(r, 16, (float)(winh - 32),
                      "Enter/click drills into lake, Esc back");
  SDL_RenderDebugText(r, 16, (float)(winh - 16),
                      "CLI: ./pilkki lake.ves objects.dat skips menus");
}

}  // namespace menu
