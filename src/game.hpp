// Offline single-player ice-fishing loop (playable, no net).
// Player pos in 8.24 world units (+-128, matches OBJECT_POSITIONS range).
// Keys (input::Keys bits): 0 up, 1 down, 2 left, 3 right (arrows/WASD
// mapped by caller), 4 line up (PgUp), 5 line down (PgDn), 6 drill
// (Enter/E), 8 jig/strike (Space). ids 0-7 match WndProc bitmap.
// Bite model: wait scaled by DEPTH_TABLE threshold fraction at player
// cell + TYPEMAP value (conjecture). newMechanics gates speculative
// extras only (jig attract speedup, depth-scaled weight); base loop
// (drill -> wait -> strike -> count) always on. Fish states:
// idle wander / bite window / hooked (instant catch) / flee on timeout.
// Deterministic LCG RNG, Seed() for tests. SetLake(null) = empty safe.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "input.hpp"
#include "ves.hpp"

namespace game {

inline constexpr int kDepthTable[17] = {0,   30,  60,  130, 160, 200,
                                        250, 300, 350, 400, 500, 700,
                                        900, 1000, 1500, 2000, 2500};
inline constexpr uint32_t kDrillBit = (uint32_t)1 << (6 & 0x1f);
inline constexpr uint32_t kJigBit = (uint32_t)1 << (8 & 0x1f);
inline constexpr float kWorldMax = 128.f;

enum class State { Idle, Bite, Hooked, Flee };

struct Fish {
  float x = 0, y = 0, w = 0.5f;
  State st = State::Idle;
};

struct Hole {
  float x = 0, y = 0;
};

struct Game {
  float x = 0, y = 0;        // player world pos
  float lineDepth = 5;       // m, 0..30 (PgUp/PgDn)
  float jigPhase = 0;        // cosmetic jig cycle
  std::vector<Hole> holes;
  std::vector<Fish> fishes;
  int catches = 0;
  double totalKg = 0, lastKg = 0;
  bool newMechanics = false;  // gate: jig speedup + depth-scaled weight
  bool justDrilled = false, justCaught = false, justBite = false;

  void Seed(uint32_t s) {
    rng_ = s ? s : 0x9e3779b9u;
    biteTimer_ = 1.5f + Rnd() * 2;
    biteWindow_ = 0;
  }

  // Null grids = no lake: movement + drill work, fish never bite.
  void SetLake(const ves::Grid *d, const ves::Grid *t,
               const std::vector<ves::ObjGroup> *g = nullptr) {
    hasLake_ = d && !d->cells.empty();
    if (d) depth_ = *d;
    if (t) type_ = *t;
    holes.clear();
    fishes.clear();
    catches = 0;
    totalKg = lastKg = 0;
    biting_ = false;
    if (hasLake_) {
      for (int i = 0; i < 10; i++) fishes.push_back(SpawnFish());
    } else if (g) {
      for (auto &gr : *g)
        for (auto &r : gr) {
          if ((int)fishes.size() >= 10) break;
          Fish f;
          f.x = std::max(-kWorldMax,
                         std::min(kWorldMax, r.x / 16777216.f));
          f.y = std::max(-kWorldMax,
                         std::min(kWorldMax, r.y / 16777216.f));
          f.w = 0.3f + Rnd() * 1.2f;
          fishes.push_back(f);
        }
      if (!fishes.empty()) hasLake_ = true;
    }
    biteTimer_ = 1.5f + Rnd() * 2;
  }

  bool Biting() const { return biting_; }

  bool AtHole(float r = 3.f) const {
    for (auto &h : holes) {
      float dx = h.x - x, dy = h.y - y;
      if (dx * dx + dy * dy <= r * r) return true;
    }
    return false;
  }

  // Test hook: open a strike window now (no-op unless fishing a lake).
  void ForceBite() {
    if (!hasLake_ || !AtHole() || biting_) return;
    StartBite();
  }

  float DepthFracAt(float wx, float wy) const {
    if (!hasLake_) return 0;
    int cx = (int)((wx + 128.f) / 256.f * depth_.w);
    int cy = (int)((wy + 128.f) / 256.f * depth_.h);
    cx = std::max(0, std::min((int)depth_.w - 1, cx));
    cy = std::max(0, std::min((int)depth_.h - 1, cy));
    uint8_t c = depth_.cells[(size_t)cy * depth_.w + cx];
    int idx = std::min(16, (int)c * 17 / 256);
    return kDepthTable[idx] / 2500.f;
  }

  float TypeAt(float wx, float wy) const {
    if (type_.cells.empty()) return 0;
    int cx = (int)((wx + 128.f) / 256.f * type_.w);
    int cy = (int)((wy + 128.f) / 256.f * type_.h);
    cx = std::max(0, std::min((int)type_.w - 1, cx));
    cy = std::max(0, std::min((int)type_.h - 1, cy));
    return type_.cells[(size_t)cy * type_.w + cx] / 255.f;
  }

  void Update(const input::Keys &k, float dt) {
    justDrilled = justCaught = justBite = false;
    if (dt < 0) dt = 0;
    // Move (diagonals normalized), clamp to +-128 world.
    float dx = (input::Down(k, 3) ? 1 : 0) - (input::Down(k, 2) ? 1 : 0);
    float dy = (input::Down(k, 1) ? 1 : 0) - (input::Down(k, 0) ? 1 : 0);
    if (dx && dy) {
      dx *= 0.7071f;
      dy *= 0.7071f;
    }
    x = std::max(-kWorldMax, std::min(kWorldMax, x + dx * 24 * dt));
    y = std::max(-kWorldMax, std::min(kWorldMax, y + dy * 24 * dt));
    // Line depth.
    if (input::Down(k, 4)) lineDepth -= 8 * dt;
    if (input::Down(k, 5)) lineDepth += 8 * dt;
    lineDepth = std::max(0.f, std::min(30.f, lineDepth));
    // Jig cycle (cosmetic unless newMechanics).
    bool jig = input::Down(k, 8);
    jigPhase = jig ? jigPhase + dt * 8 : std::max(0.f, jigPhase - dt * 4);
    // Drill on rising edge, dupe-suppressed within 1.5 units.
    bool drillWas = (prevBits_ & kDrillBit) != 0;
    if (input::Down(k, 6) && !drillWas && !NearHole(x, y, 1.5f)) {
      holes.push_back({x, y});
      justDrilled = true;
    }
    // Fish wander.
    for (auto &f : fishes)
      if (f.st == State::Idle) {
        f.x = std::max(-kWorldMax,
                       std::min(kWorldMax, f.x + (Rnd() - 0.5f) * 4 * dt));
        f.y = std::max(-kWorldMax,
                       std::min(kWorldMax, f.y + (Rnd() - 0.5f) * 4 * dt));
      }
    // Bite cycle.
    if (!hasLake_ || !AtHole()) {
      biting_ = false;
      biteTimer_ = NextWait();
    } else if (!biting_) {
      float boost = (newMechanics && jig) ? 2.f : 1.f;
      biteTimer_ -= dt * boost;
      if (biteTimer_ <= 0) StartBite();
    } else {
      biteWindow_ -= dt;
      if (jig) {
        Catch();
      } else if (biteWindow_ <= 0) {
        for (auto &f : fishes)
          if (f.st == State::Bite) {
            f.st = State::Idle;  // ponytail: teleport = flee, respawn if crowded
            f.x = std::max(-kWorldMax,
                           std::min(kWorldMax, f.x + 20 * (Rnd() - 0.5f)));
            f.y = std::max(-kWorldMax,
                           std::min(kWorldMax, f.y + 20 * (Rnd() - 0.5f)));
          }
        biting_ = false;
        biteTimer_ = NextWait();
      }
    }
    prevBits_ = k.bits;
  }

 private:
  ves::Grid depth_, type_;
  bool hasLake_ = false;
  uint32_t rng_ = 0x9e3779b9u, prevBits_ = 0;
  float biteTimer_ = 2, biteWindow_ = 0;
  bool biting_ = false;

  float Rnd() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return (rng_ >> 8) * (1 / 16777216.f);
  }

  bool NearHole(float wx, float wy, float r) const {
    for (auto &h : holes) {
      float dx = h.x - wx, dy = h.y - wy;
      if (dx * dx + dy * dy <= r * r) return true;
    }
    return false;
  }

  float NextWait() const {
    // Base 1.2..4s by depth fraction, TYPEMAP-rich cells wait less.
    float f = hasLake_ ? DepthFracAt(x, y) : 0;
    float t = hasLake_ ? TypeAt(x, y) : 0;
    return (4.f - 2.8f * f) * (1.2f - 0.4f * t);
  }

  Fish SpawnFish() {
    Fish f;
    for (int t = 0; t < 8; t++) {  // accept deep cells, keep shallow fallback
      size_t i = (size_t)(Rnd() * depth_.cells.size()) % depth_.cells.size();
      uint8_t c = depth_.cells[i];
      f.x = (float)(i % depth_.w) / depth_.w * 256 - 128;
      f.y = (float)(i / depth_.w) / depth_.h * 256 - 128;
      if (c * 17 / 256 > 1 || t == 7) break;
    }
    f.w = 0.3f + Rnd() * 1.2f;
    return f;
  }

  void StartBite() {
    Fish *best = nullptr;
    float bd = 1e30f;
    for (auto &f : fishes) {
      if (f.st != State::Idle) continue;
      float dx = f.x - x, dy = f.y - y, d = dx * dx + dy * dy;
      if (d < bd) {
        bd = d;
        best = &f;
      }
    }
    if (!best) {
      biteTimer_ = 1.f;
      return;
    }
    best->st = State::Bite;
    best->x = x + (Rnd() - 0.5f);
    best->y = y + (Rnd() - 0.5f);
    biting_ = true;
    justBite = true;
    biteWindow_ = 1.5f;
  }

  void Catch() {
    for (auto &f : fishes)
      if (f.st == State::Bite) {
        f.st = State::Idle;
        float wgt = 0.2f + Rnd() * 1.8f;
        if (newMechanics) wgt *= 0.5f + DepthFracAt(x, y);
        lastKg = wgt;
        totalKg += wgt;
        catches++;
        justCaught = true;
        f.x = (Rnd() - 0.5f) * 256 * 0.9f;  // respawn elsewhere
        f.y = (Rnd() - 0.5f) * 256 * 0.9f;
        break;
      }
    biting_ = false;
    biteTimer_ = NextWait() * (0.7f + 0.6f * Rnd());
  }
};

}  // namespace game
