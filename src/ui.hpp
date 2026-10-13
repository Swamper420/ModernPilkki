// Offline UI flow (no net): TITLE -> LAKE_SELECT -> PLAY -> PAUSE -> RESULTS.
// States/menus/timer/HUD only. Fishing sim stays in game.hpp, scene in
// render.hpp, keys in input.hpp; main.cpp syncs HUD numbers each frame.
// Text via SDL_RenderDebugText (SDL3 built-in 8x8 font, no TTF dep).
// GAMETYPES section = per-type enable flags + 'c' pad (100 B, no durations
// in 48/48 lakes) -> time limit always default 600 s free fishing.
// STRINGINFO_FI field 0 = lake name; only hi bytes in 48 lakes are
// 0x84/0x94 (game-font a/o), mapped to ASCII a/o.
#pragma once
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "state.hpp"
#include "ves.hpp"

namespace ui {

enum class State { Title, LakeSelect, Play, Pause, Results };
// Original 00640370 mode ids (docs "Port mapping today"): Title=10,
// LakeSelect=11 (11/12 combined), Play=20, Pause=4, Results=21.
inline int StateId(State s) {
  switch (s) {
    case State::Title: return 10;
    case State::LakeSelect: return 11;
    case State::Play: return 20;
    case State::Pause: return 4;
    case State::Results: return 21;
  }
  return 10;
}
inline State IdState(int id) {
  switch (id) {
    case 4: return State::Pause;
    case 11: return State::LakeSelect;
    case 20: return State::Play;
    case 21: return State::Results;
    default: return State::Title;
  }
}
// SDLK_R edge, passed straight from the event loop (input bitmap ids 0..8
// are taken, so this lives outside it).
inline constexpr uint32_t kRestart = 9;
inline constexpr int kDefaultTimeSec = 600;

struct LakeEntry {
  std::string path;
  std::string name;
  std::string region;  // STRINGINFO_FI field 1, may be empty
};

struct Rules {
  bool hasGametypes = false;
  uint8_t flags[25] = {};
  int timeSec = kDefaultTimeSec;
};

// Copy enable flags (values 0/1; 'c' pad and rest ignored). No duration is
// stored in the section, so timeSec keeps the default.
inline Rules ParseGametypes(const uint8_t *p, size_t n) {
  Rules r;
  if (!p || !n) return r;
  r.hasGametypes = true;
  for (size_t i = 0; i < n && i < 25; i++) r.flags[i] = p[i] <= 1 ? p[i] : 0;
  return r;
}

inline char MapByte(uint8_t b) {
  if (b == 0x84) return 'a';
  if (b == 0x94) return 'o';
  if (b == '\n' || b == '\r') return ' ';
  if (b >= 0x20 && b < 0x7f) return (char)b;
  return '?';
}

// First two NUL fields of STRINGINFO_FI: lake name + region.
// Fallback = basename when missing.
inline std::pair<std::string, std::string> LakeNameRegion(
    const std::vector<ves::Section> &secs, const std::string &fallback) {
  for (auto &s : secs) {
    if (s.tag != "STRINGINFO_FI") continue;
    std::string name, region;
    int field = 0;
    for (uint8_t b : s.payload) {
      if (!b) {
        if (++field > 1) break;
        continue;
      }
      (field ? region : name) += MapByte(b);
    }
    if (!name.empty()) return {name, region};
  }
  auto i = fallback.find_last_of("/\\");
  std::string base =
      i == std::string::npos ? fallback : fallback.substr(i + 1);
  return {base, ""};
}

inline std::string LakeName(const std::vector<ves::Section> &secs,
                            const std::string &fallback) {
  return LakeNameRegion(secs, fallback).first;
}

inline std::vector<uint8_t> ReadAll(const std::string &p) {
  FILE *f = fopen(p.c_str(), "rb");
  if (!f) throw std::runtime_error("no such file");
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> d(n > 0 ? (size_t)n : 0);
  if (n > 0 && fread(d.data(), 1, (size_t)n, f) != (size_t)n) {
    fclose(f);
    throw std::runtime_error("read");
  }
  fclose(f);
  return d;
}

// Runtime scan of <dir>/*.ves; skips unreadable files; empty on missing dir.
inline std::vector<LakeEntry> ScanLakes(const std::string &dir) {
  std::vector<LakeEntry> out;
  std::error_code ec;
  for (auto &e : std::filesystem::directory_iterator(dir, ec)) {
    if (ec || !e.is_regular_file(ec) || e.path().extension() != ".ves")
      continue;
    try {
      auto secs = ves::Parse(ReadAll(e.path().string()));
      auto nr = LakeNameRegion(secs, e.path().filename().string());
      out.push_back(
          {e.path().string(), nr.first, nr.second});
    } catch (...) {
    }
  }
  std::sort(out.begin(), out.end(),
            [](const LakeEntry &a, const LakeEntry &b) { return a.name < b.name; });
  return out;
}

// Numbers shown in the HUD, synced from game/render state by main each frame.
struct Hud {
  int depthAt = -1;
  int holes = 0;
  int catches = 0;
  double kg = 0;
  double biggest = 0;
  int lineDepth = 10;  // game::kLineM bucket
  int baitLeft = 0;    // sum of the 4x16 bait grid
  bool bite = false;
};

struct Game {
  State state = State::Title;
  // Original mode driver (00640370 + 00632560 deferred apply); steady
  // Title: pending==active==10, frame phase running.
  state::Driver drv{10, 10, state::kFrame, 20};
  std::vector<LakeEntry> lakes;
  int sel = 0;
  int pendIdx = -1;  // LakeSelect choice waiting for main to load
  bool needLoad = false;
  bool needRestart = false;  // Results R: main resets sim, calls BeginPlay
  bool quit = false;
  std::string lakeName = "(none)";
  Rules rules;
  float timeLeft = kDefaultTimeSec;
};

inline void Init(Game &g, std::vector<LakeEntry> lakes) {
  g = Game{};
  g.lakes = std::move(lakes);
}

// Run the 00640370 pass: enter blocks flip g.state (frame/exit blocks
// hook here when fades land). Synchronous like the original: one call
// completes exit(old) + enter(new) before returning.
inline void Step(Game &g) {
  state::Pass(g.drv, [&](int id, int phase) {
    if (phase == state::kEnter) g.state = IdState(id);
  });
}

// Request a state change (00632560 applies it deferred, then Step
// runs the exit/enter phases in order).
inline void To(Game &g, State s) {
  g.drv.pending = StateId(s);
  Step(g);
}

inline void BeginPlay(Game &g, const std::string &name, Rules r) {
  Game fresh;  // reset run stats, keep scanned lake list + cursor
  fresh.lakes = std::move(g.lakes);
  fresh.sel = g.sel;
  g = std::move(fresh);
  g.lakeName = name;
  g.rules = r;
  g.timeLeft = (float)r.timeSec;
  To(g, State::Play);
}

// Menu edge input; ids 0/1/6/7 = input::Keys, kRestart = R key.
// Gameplay keys (drill/jig/zoom) stay in main + game::Game, gated on Play.
// Transitions go through the mode driver (To/step); Esc in Play uses the
// 7c8790 pause-return: state 4 (pause hub) resumes via ResumeId.
inline void Press(Game &g, uint32_t id) {
  switch (g.state) {
    case State::Title:
      if (id == 6 && !g.lakes.empty()) To(g, State::LakeSelect);
      if (id == 7) g.quit = true;
      break;
    case State::LakeSelect:
      if (g.lakes.empty()) {
        if (id == 7) To(g, State::Title);
        break;
      }
      if (id == 0)
        g.sel = (g.sel + (int)g.lakes.size() - 1) % (int)g.lakes.size();
      if (id == 1) g.sel = (g.sel + 1) % (int)g.lakes.size();
      if (id == 6) {
        g.pendIdx = g.sel;
        g.needLoad = true;
      }
      if (id == 7) To(g, State::Title);
      break;
    case State::Play:
      if (id == 7) {
        state::Pause(g.drv, StateId(State::Play), StateId(State::Pause));
        Step(g);
      }
      break;
    case State::Pause:
      if (id == 6 || id == 7) To(g, IdState(state::ResumeId(g.drv)));
      break;
    case State::Results:
      if (id == kRestart) g.needRestart = true;
      if (id == 7) g.quit = true;
      if (id == 6) To(g, State::LakeSelect);
      break;
  }
}

// Countdown in Play only; expiry -> Results. Main swallows held keys on
// expiry so a held Enter does not leak into Results (Enter = lakes there).
inline void Update(Game &g, float dt) {
  if (g.state != State::Play) return;
  g.timeLeft -= dt;
  if (g.timeLeft <= 0) {
    g.timeLeft = 0;
    To(g, State::Results);
  }
}

inline std::string MmSs(float t) {
  int s = (int)(t + 0.5f);
  char b[16];
  snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
  return b;
}

inline void Render(const Game &g, SDL_Renderer *r, const Hud &hud) {
  int h = 600;
  SDL_GetCurrentRenderOutputSize(r, nullptr, &h);
  SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
  auto line = [&](int row, const std::string &s) {
    SDL_RenderDebugText(r, 16, 16 + 12 * (float)row, s.substr(0, 90).c_str());
  };
  char b[128];
  switch (g.state) {
    case State::Title:
      line(8, "ModernPilkki (offline)");
      if (g.lakes.empty())
        line(10, "no lakes in extracted/lakes");
      else {
        snprintf(b, sizeof b, "Enter: fish (%zu lakes)   Esc: quit",
                 g.lakes.size());
        line(10, b);
      }
      break;
    case State::LakeSelect: {
      line(0, "Pick lake (Up/Down+Enter, Esc back)");
      int rows = std::max(5, (h - 60) / 12 - 2);
      int n = (int)g.lakes.size();
      int start = n <= rows ? 0 : std::min(std::max(0, g.sel - rows / 2), n - rows);
      for (int i = 0; i < rows && start + i < n; i++) {
        int j = start + i;
        line(2 + i, (j == g.sel ? "> " : "  ") + g.lakes[(size_t)j].name);
      }
      break;
    }
    case State::Play:
    case State::Pause:
    case State::Results:
      line(0, g.lakeName);
      snprintf(b, sizeof b, "depth %d  holes %d  fish %d (%.1f kg)", hud.depthAt,
               hud.holes, hud.catches, hud.kg);
      line(1, b);
      line(2, "time " + MmSs(g.timeLeft));
      if (g.state == State::Play)
        line(3, "Arrows move / Enter drill / Space jig / PgUp PgDn line / wheel +- zoom / M mute / Esc pause");
      if (g.state == State::Pause) line(4, "PAUSED - Enter/Esc resume");
      if (g.state == State::Results)
        line(4, "RESULTS - R restart / Enter lakes / Esc quit");
      break;
  }
}

}  // namespace ui
