#pragma once
// fishstock.hpp — DATA1 fish-stock loader: section deobfuscation (00430f50),
// fish-entry record parse (005f6da0/005fa2b0), per-cell population build and
// placement (005fa100/005fa390/004a9490), and the data0.dat species tables
// (0059f870). Clean-room port of documented behaviour; analysis in
// docs/sim-stock.md. Implements bite::Stock (src/bite.hpp) so game.hpp can
// swap it in for bite::SyntheticStock. No SDL.

#include "bite.hpp"
#include "ves.hpp"

#include <dirent.h>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace fishstock {

// ---- robust file open (pattern from render3d_pipeline.hpp ResolveCase) ----

inline std::string ResolveCase(const std::string &path) {
  if (FILE *f = fopen(path.c_str(), "rb")) {
    fclose(f);
    return path;
  }
  size_t slash = path.find_last_of('/');
  const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
  std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
  for (auto &c : base) c = (char)tolower((unsigned char)c);
  DIR *d = opendir(dir.c_str());
  if (!d) return path;
  std::string found = path;
  for (dirent *e; (e = readdir(d));) {
    std::string n = e->d_name, ln = n;
    for (auto &c : ln) c = (char)tolower((unsigned char)c);
    if (ln == base) {
      found = dir + "/" + n;
      break;
    }
  }
  closedir(d);
  return found;
}

// Whole-file read; empty on failure.
inline std::vector<uint8_t> ReadWholeFile(const std::string &path) {
  std::vector<uint8_t> out;
  FILE *f = fopen(ResolveCase(path).c_str(), "rb");
  if (!f) return out;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n > 0) {
    out.resize((size_t)n);
    if (fread(out.data(), 1, (size_t)n, f) != (size_t)n) out.clear();
  }
  fclose(f);
  return out;
}

// ---- DATA1/DATA2 payload obfuscation (00430f50) ----
// Decode (flags 3, the loader's call at 004b805f): byte-reverse the whole
// payload, then XOR with a repeating key. Encode is the same two steps in
// the other order (XOR first, then reverse).

inline const char kData1Key[] = "fkjsafdBLKJBbljbefwlkj";  // .rdata 006dbfa8
inline const char kData2Key[] = "hjdfHGFAJKGghafkjhg";     // .rdata 006dbfd0

inline void Deobfuscate(std::vector<uint8_t> &buf, const char *key) {
  size_t kl = strlen(key);
  if (kl == 0) return;
  for (size_t i = 0, j = buf.size(); i + 1 < j; i++, j--) {
    uint8_t t = buf[i];
    buf[i] = buf[j - 1];
    buf[j - 1] = t;
  }
  for (size_t i = 0; i < buf.size(); i++) buf[i] ^= (uint8_t)key[i % kl];
}

inline void Obfuscate(std::vector<uint8_t> &buf, const char *key) {
  size_t kl = strlen(key);
  if (kl == 0) return;
  for (size_t i = 0; i < buf.size(); i++) buf[i] ^= (uint8_t)key[i % kl];
  for (size_t i = 0, j = buf.size(); i + 1 < j; i++, j--) {
    uint8_t t = buf[i];
    buf[i] = buf[j - 1];
    buf[j - 1] = t;
  }
}

// ---- fish entry records ----

// Weight mixture component (record +0x30 + 32*g -> entry +8 + 24*g,
// 005f67b0/005f8320/0059c070): Gaussian (mean, sd) in grams with a mix
// share *(inferred units; docs/sim-stock.md 3)*.
struct Mix {
  double mean = 0, sd = 0, share = 0;
};

// Spawn area (record +0x140 + 44*i, 005f6da0 -> 005fa120). Kind 1/3 is a
// centred shape (rx/ry clamped >= 50 there); other kinds are the box
// (x0,y0)..(x1,y1).
struct Area {
  int kind = 0;
  int rx = 50, ry = 50;
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct Entry {
  int type = 0;        // rec+0x08 -> entry+0x190: fish type = species 1..32
  int radius = 0;      // rec+0x0c -> entry+0xe78: attraction radius (map units)
  int ratioDen = 0;    // rec+0x10 -> entry+0xe28
  int ratioNum = 0;    // rec+0x14 -> entry+0xe2c/0xe30 (decoded by 005f8ff0)
  int mat[9] = {};     // rec+0x1c..0x24 -> entry+0xec4..0xee4 (3x3, transposed)
  int f25 = 0, f26 = 0, f28 = 0;  // rec+0x25..0x28 -> entry+0xfb4/0xfb5/0xfb8
  Mix mix[5];          // rec+0x30 + 32*g -> entry+8 + 24*g
  std::vector<Area> areas;
  int e60 = 0, e64 = 0, e68 = 0;  // tail after the areas -> entry+0xe60..
  // Placement runtime (005fa100/005fa390 -> entry+0xe54/0xe58/0xe5c,
  // slot 0 = cell, slot 3 = master index).
  int x = 0, y = 0, z = 0;
  int cell = -1;
  int tag = -1;
};

// Per-type default (mean, sd) in grams: ctor 005f58b0 table at .rdata
// 0x007465c0, 33 rows (mean, sd) in a fixed order *(row mapping inferred
// from the ctor's table read, values verified)*.
inline void DefaultMix(int type, double &mean, double &sd) {
  static const double kTab[33][2] = {
      {0, 0},       {100, 50},   {10, 5},     {500, 300},  {500, 300},
      {1000, 200},  {800, 300},  {600, 300},  {400, 200},  {600, 300},
      {1000, 300},  {150, 70},   {150, 70},   {30, 10},    {20, 10},
      {100, 50},    {100, 50},   {500, 300},  {100, 50},   {100, 50},
      {100, 50},    {100, 50},   {20, 10},    {100, 50},   {1000, 300},
      {100, 50},    {1000, 300}, {15, 5},     {30, 10},    {100, 50},
      {1000, 300},  {100, 50},   {15, 5},
  };
  if (type < 0 || type > 32) type = 0;
  mean = kTab[type][0];
  sd = kTab[type][1];
}

// Representative weight in grams: the mean of the first non-zero mixture
// component, else the per-type default mean.
inline int WeightG(const Entry &f) {
  for (const Mix &m : f.mix)
    if (m.share != 0.0) return (int)lround(m.mean);
  double mean, sd;
  DefaultMix(f.type, mean, sd);
  return (int)lround(mean);
}

namespace detail {
inline uint32_t U32(const std::vector<uint8_t> &d, size_t o) { return ves::U32(d, o); }
inline int32_t I32(const std::vector<uint8_t> &d, size_t o) {
  return (int32_t)ves::U32(d, o);
}
inline double F64(const std::vector<uint8_t> &d, size_t o) {
  double v = 0;
  memcpy(&v, d.data() + o, 8);
  return v;
}
}  // namespace detail

// Parse one deobfuscated DATA1 payload: u32 count, then count records of
// [u32 len][len bytes] (004b7f00..004b8340). Records are the FUN_005f6da0
// parameter block; len == 332 + 44*areaCount in all 48 lakes. Malformed
// records are skipped, never fatal.
inline bool ParseData1(const std::vector<uint8_t> &d, std::vector<Entry> &out) {
  if (d.size() < 4) return false;
  uint32_t count = detail::U32(d, 0);
  size_t off = 4;
  out.clear();
  out.reserve(count < 100000 ? count : 0);
  for (uint32_t i = 0; i < count; i++) {
    if (off + 4 > d.size()) return !out.empty();
    uint32_t len = detail::U32(d, off);
    size_t r = off + 4;
    if (len < 0x14c || r + len > d.size()) return !out.empty();
    const std::vector<uint8_t> rec(d.begin() + r, d.begin() + r + len);  // unaligned-safe
    Entry f;
    f.type = detail::I32(rec, 0x08);
    f.radius = detail::I32(rec, 0x0c);
    f.ratioDen = detail::I32(rec, 0x10);
    f.ratioNum = detail::I32(rec, 0x14);
    uint32_t narea = detail::U32(rec, 0x18);
    if (narea > 64 || 0x14c + 44 * (size_t)narea > len) return !out.empty();
    for (int b = 0; b < 9; b++) f.mat[b] = rec[0x1c + b];
    f.f25 = rec[0x25];
    f.f26 = rec[0x26];
    f.f28 = detail::I32(rec, 0x28);
    for (int g = 0; g < 5; g++) {
      f.mix[g].mean = detail::F64(rec, 0x30 + 32 * g);
      f.mix[g].sd = detail::F64(rec, 0x38 + 32 * g);
      f.mix[g].share = detail::F64(rec, 0x40 + 32 * g);
    }
    for (uint32_t a = 0; a < narea; a++) {
      size_t p = 0x140 + 44 * (size_t)a;
      Area ar;
      ar.kind = detail::I32(rec, p);
      ar.rx = detail::I32(rec, p + 0x04);
      ar.ry = detail::I32(rec, p + 0x08);
      ar.x0 = detail::I32(rec, p + 0x0c);
      ar.y0 = detail::I32(rec, p + 0x10);
      ar.x1 = detail::I32(rec, p + 0x18);
      ar.y1 = detail::I32(rec, p + 0x1c);
      if (ar.kind == 1 || ar.kind == 3) {
        // Centred shape: rx/ry clamped >= 50 (005fa120), centre (x0, y0).
        if (ar.rx < 50) ar.rx = 50;
        if (ar.ry < 50) ar.ry = 50;
        ar.x1 = ar.x0 + ar.rx;
        ar.y1 = ar.y0 + ar.ry;
        ar.x0 -= ar.rx;
        ar.y0 -= ar.ry;
      }
      f.areas.push_back(ar);
    }
    size_t t = 0x140 + 44 * (size_t)narea;
    f.e60 = detail::I32(rec, t);
    f.e64 = detail::I32(rec, t + 4);
    f.e68 = detail::I32(rec, t + 8);
    out.push_back(f);
    off = r + len;
  }
  return true;
}

// ---- species tables (data0.dat, 0059f870) ----
// 9 species objects of 0x310 B: <n_1> = 2 u32 (semantics open) at +0x304/
// +0x308, <n_2>/<n_3>/<n_4> = 255-byte u8 curves at +0x000/+0x0ff/+0x1fe
// (defaults 0x18/0x4d/0x66), +0x30c = 1.0f.

struct Species {
  uint32_t p0 = 0, p1 = 0;
  uint8_t c2[255], c3[255], c4[255];
  Species() {
    memset(c2, 0x18, sizeof c2);
    memset(c3, 0x4d, sizeof c3);
    memset(c4, 0x66, sizeof c4);
  }
};

inline bool ParseData0(const std::vector<uint8_t> &file, Species (&sp)[10]) {
  std::vector<ves::Section> secs;
  try {
    secs = ves::Parse(file);
  } catch (...) {
    return false;
  }
  int got = 0;
  for (const ves::Section &s : secs) {
    if (s.tag.size() != 3 || s.tag[1] != '_' || s.tag[0] < '1' || s.tag[0] > '9')
      continue;
    int n = s.tag[0] - '0';
    const std::vector<uint8_t> &p = s.payload;
    switch (s.tag[2]) {
      case '1':
        if (p.size() < 8) continue;
        sp[n].p0 = detail::U32(p, 0);
        sp[n].p1 = detail::U32(p, 4);
        break;
      case '2':
        if (p.size() < 255) continue;
        memcpy(sp[n].c2, p.data(), 255);
        break;
      case '3':
        if (p.size() < 255) continue;
        memcpy(sp[n].c3, p.data(), 255);
        break;
      case '4':
        if (p.size() < 255) continue;
        memcpy(sp[n].c4, p.data(), 255);
        break;
      default:
        continue;
    }
    got++;
  }
  return got > 0;
}

// ---- the bite::Stock provider ----

struct Stock : bite::Stock {
  std::vector<Entry> fish;              // master list (lake+0x240dc)
  std::vector<std::vector<int>> cells;  // gridW*gridH -> fish indices
  Species species[10];                  // data0.dat, idx 1..9 (0 unused)
  bool loaded = false;
  std::function<int(int, int)> depth;   // 004a5700(x,y,1); host wires it
  bite::Rng place;                      // placement RNG (deterministic)

  void SetSeed(uint32_t s) { place.Seed(s); }
  void SetDepth(std::function<int(int, int)> fn) {
    if (fn) depth = std::move(fn);
  }

  // Load + deobfuscate + parse the lake's DATA1. Empty stock on any
  // failure (missing file, bad hash, bad framing); never throws.
  bool LoadLake(const std::string &path) {
    fish.clear();
    cells.clear();
    loaded = false;
    std::vector<uint8_t> file = ReadWholeFile(path);
    if (file.empty()) return false;
    std::vector<ves::Section> secs;
    try {
      secs = ves::Parse(file);
    } catch (...) {
      return false;
    }
    for (ves::Section &s : secs) {
      if (s.tag != "DATA1") continue;
      std::vector<uint8_t> buf = std::move(s.payload);
      Deobfuscate(buf, kData1Key);
      if (!ParseData1(buf, fish)) fish.clear();
      loaded = true;
      return true;
    }
    return false;
  }

  bool LoadSpecies(const std::string &path) {
    std::vector<uint8_t> file = ReadWholeFile(path);
    if (file.empty()) return false;
    return ParseData0(file, species);
  }

  // Per-cell population build (005fa100 placement + 004a9490 cell lookup).
  // Uniform point in the first spawn area (no areas -> the whole grid);
  // the original resamples up to 500 candidates through walkable/depth
  // gates and a suitability score (005fa390) — that scoring is unported.
  // ponytail: one uniform sample per fish, resample-with-gates when bite
  // locality vs depth ever matters.
  void Place(int gw = 0, int gh = 0) {
    if (gw > 0) gridW_ = gw;
    if (gh > 0) gridH_ = gh;
    if (gridW_ <= 0 || gridH_ <= 0) {
      int mx = 1, my = 1;
      for (const Entry &f : fish) {
        if (!f.areas.empty()) {
          if (f.areas[0].x1 > mx) mx = f.areas[0].x1;
          if (f.areas[0].y1 > my) my = f.areas[0].y1;
        }
      }
      gridW_ = mx / 600 + 1;
      gridH_ = my / 600 + 1;
    }
    gridW = gridW_;  // bite::Stock grid (004a9490 cell lookup)
    gridH = gridH_;
    cells.assign((size_t)gridW * gridH, {});
    for (size_t i = 0; i < fish.size(); i++) {
      Entry &f = fish[i];
      f.tag = (int)i;
      const int maxX = gridW * 600, maxY = gridH * 600;
      if (!f.areas.empty()) {
        const Area &a = f.areas[0];
        int w = a.x1 - a.x0, h = a.y1 - a.y0;
        f.x = a.x0 + (w > 0 ? place.RandMax(w + 1) : 0);
        f.y = a.y0 + (h > 0 ? place.RandMax(h + 1) : 0);
      } else {
        f.x = 1 + place.RandMax(maxX - 1);
        f.y = 1 + place.RandMax(maxY - 1);
      }
      // Safety clamp: every fish must land inside the cell grid
      // (004a9490 needs x,y >= 1).
      if (f.x < 1) f.x = 1;
      if (f.y < 1) f.y = 1;
      if (f.x > maxX) f.x = maxX;
      if (f.y > maxY) f.y = maxY;
      f.cell = CellOf(f.x, f.y);
      if (f.cell >= 0) cells[(size_t)f.cell].push_back((int)i);
    }
  }

  // ---- bite::Stock ----

  int Count(int cx, int cy) const override {
    if (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) return 0;
    return (int)cells[(size_t)(cy * gridW + cx)].size();
  }
  int TypeAt(int cx, int cy, int idx) const override {
    return At(cx, cy, idx).type;
  }
  int WeightAt(int cx, int cy, int idx) const override {
    return WeightG(At(cx, cy, idx));
  }
  int TagAt(int cx, int cy, int idx) const override {
    return At(cx, cy, idx).tag;
  }
  int SpeciesId(int idx) const override {
    return (idx >= 1 && idx <= 9) ? idx : 0;  // pick tables 004acc80
  }
  // Host must SetDepth first (game.hpp BuildStock wires 004a5700); an unset
  // fn is a wiring bug, not a depth to invent.
  int DepthAt(int x, int y) const override {
    assert(depth && "SetDepth must run before the stock is used");
    return depth(x, y);
  }
  bool Stocked() const override {
    for (const Entry &f : fish)
      if (f.type == 2) return true;  // 005f7980 == 2 -> lake+0x15128
    return false;
  }

  // 005f8ff0 seam. The verified part is the distance gate (005f7ee0):
  // scaled = dist * 0.8333333 / (radius * 1.3) (007464f8 / 006db108),
  // reject when scaled > 1.2 (006dc668). The rest of the chain (shyness,
  // lure match, boat modes) is unported; the roll below keeps the
  // SyntheticStock shape so gameplay does not change under the loader.
  // ponytail: accept = rand(3)==0, single mixture roll; replace with the
  // 005f8ff0 chain + 005f8490 inverse-CDF size roll when ported.
  bite::Eval Evaluate(int cx, int cy, int idx, const bite::Context &ctx,
                      bite::Rng &rng) const override {
    bite::Eval e;
    const Entry &f = At(cx, cy, idx);
    double dx = (double)ctx.x - f.x, dy = (double)ctx.y - f.y;
    double dist = sqrt(dx * dx + dy * dy);
    if (f.radius <= 0 ? dist > 0
                      : dist * (0.8333333333333334 / ((double)f.radius * 1.3)) > 1.2) {
      e.hardReject = true;  // 005f7ee0: *out = 0, *(out+0x40) = 1
      return e;
    }
    e.hardReject = false;
    if (rng.RandMax(3) != 0) return e;  // soft reject
    e.bite = true;
    e.type = f.type;
    e.tag = f.tag;
    e.v = RollWeight(f, rng);
    return e;
  }

 private:
  int gridW_ = 0, gridH_ = 0;

  const Entry &At(int cx, int cy, int idx) const {
    static const Entry kNone;
    if (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) return kNone;
    const std::vector<int> &v = cells[(size_t)(cy * gridW + cx)];
    if (idx < 0 || idx >= (int)v.size()) return kNone;
    return fish[(size_t)v[(size_t)idx]];
  }

  // One Gaussian mixture sample in grams, floor 8 (005f8ff0's v = max(8,
  // roll)); the original draws 1..3 inverse-CDF samples (005f8490) and
  // keeps the max.
  static int RollWeight(const Entry &f, bite::Rng &rng) {
    double mean = 0, sd = 0, total = 0;
    for (const Mix &m : f.mix) {
      if (m.share <= 0) continue;
      total += m.share;
      if (mean == 0 && sd == 0) {
        mean = m.mean;
        sd = m.sd;
      }
    }
    if (total > 0) {
      // Pick a component weighted by share.
      double pick = (double)rng.RandMax(1000) / 1000.0 * total, acc = 0;
      for (const Mix &m : f.mix) {
        if (m.share <= 0) continue;
        acc += m.share;
        if (pick <= acc) {
          mean = m.mean;
          sd = m.sd;
          break;
        }
      }
    } else {
      DefaultMix(f.type, mean, sd);
    }
    if (sd > 0) {
      // Box-Muller from the 15-bit MSVC rand stream.
      double u1 = ((double)rng.RandMax(1000) + 1.0) / 1001.0;
      double u2 = (double)rng.RandMax(1000) / 1000.0;
      mean += sd * sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
    }
    int v = (int)lround(mean);
    return v < 8 ? 8 : v;
  }
};

}  // namespace fishstock
