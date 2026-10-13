#pragma once
// bite.hpp — fish stock access and bite record building (004ba5e0) plus
// bite attempts (00479700), ring (00471940/00473a70) and the request rate
// limiter (00592160/005922e0). Clean-room port of documented behaviour;
// analysis in docs/sim-bite.md, constants verified from .rdata. No SDL.

#include <cstdint>
#include <utility>
#include <vector>

namespace bite {

// MSVC rand, bit-exact (00676d2c / 0059db20: (int)(u30 * 2^-30 * n),
// clamp n-1; n<=0 -> 0).
struct Rng {
  uint32_t s = 0;
  void Seed(uint32_t v) { s = v; }
  uint32_t Rand() {
    s = s * 214013u + 2531011u;
    return (s >> 16) & 0x7fffu;
  }
  int RandMax(int n) {
    if (n <= 0) return 0;
    uint32_t r1 = Rand() & 0x7fff, r2 = Rand() & 0x7fff;
    uint32_t u = (r2 << 15) | r1;
    int r = (int)((double)u * 9.313225746154785e-10 * (double)n);
    return r > n - 1 ? n - 1 : r;
  }
};

// ---- verified constant tables (docs/sim-bite.md 4-5) ----

// Weight W = min(v * k, cap) per line bucket (004ba5e0 switch on
// context+0x50 = line metres 10/20/30/40).
inline double WeightK(int line) {
  switch (line) {
    case 10: return 0.016666666666666666;    // 006d09b8 1/60
    case 20: return 0.008695652173913044;    // 006dc5e8 1/115
    case 30: return 0.00625;                 // 006dc5d8 1/160
    case 40: return 0.004347826086956522;    // 006dc5d0 1/230
    default: return 0.011111111111111112;    // 006dc5f0 ("Unsupported line")
  }
}
inline double WeightCap(int line) {
  return line == 10 ? 300.0 : 30.0;  // 006da020 / 007778d8
}
inline double Weight(int v, int line) {
  double w = (double)v * WeightK(line);
  double cap = WeightCap(line);
  return w < cap ? w : cap;
}

// Record +0x20: v <= 300 -> 0.05; 300 < v <= 1000 -> ramp; else 0.16666.
inline double Timing32(int v) {
  if (v >= 1001) return 0.16666;              // 006dc608
  if (v > 300)                                  // 006d1618, 006dc5b0
    return 0.05 + (double)(v - 300) * 0.00016665714285714285;
  return 0.05;
}

// Per-line fight rule for record +0x28 (004ba5e0). u32 gates the bonus;
// base/scale differ for fish type 3. context+8 != -1 forces the 40 m row.
struct LineRule {
  int u32;
  double base3, scale3, base, scale;
};
inline const LineRule &LineRuleFor(int line) {
  static const LineRule k10{300, 0.4, 5.0, 0.0, 6.0};    // 006d9f88/006db128/006d1648
  static const LineRule k20{700, 0.2, 3.0, 0.0, 1.5};    // 006d0568/006d9fc0/006d1638
  static const LineRule k30{1200, 0.05, 2.0, 0.0, 1.0};  // base/scale stalls *(inferred)*
  static const LineRule k40{1500, 0.0, 0.5, 0.0, 0.5};   // 00777878
  switch (line) {
    case 10: return k10;
    case 20: return k20;
    case 30: return k30;
    default: return k40;
  }
}
inline double Bonus(const LineRule &r, int type, int v) {
  double b = 0.0;
  if (v > r.u32)  // integer division by 100 (asm magic 0x51eb851f)
    b = (double)((v - r.u32) / 100) * 0.01 * (type == 3 ? r.scale3 : r.scale);
  return b + (type == 3 ? r.base3 : r.base);
}

// Depth-band catch/fail p, record +0x30/+0x38 (006dd0b0/c0/d0), chosen by
// v against the line rule's u32.
inline void Bands(const LineRule &r, int v, double &catchP, double &failP) {
  if (v < r.u32 / 2) {
    catchP = 0.7;
    failP = 0.1;
  } else if (v < r.u32) {
    catchP = 0.9;
    failP = 0.4;
  } else {
    catchP = 0.95;
    failP = 0.5;
  }
}

// 005f77d0 difficulty ramp, 100 -> 0 between the per-type thresholds
// (table 00746278, row stride 16 = {A_lo, A_hi, B_lo, B_hi}; column A for
// flag 0, B otherwise). Fish types 0..32 are tabulated.
inline void RampRow(int type, int &lo, int &hi, int flag) {
  static const int kTab[33][4] = {
      {0, 0, 0, 0},          {800, 1450, 2000, 6500},   {800, 1450, 2000, 6500},
      {1200, 2250, 4200, 16500}, {1100, 1500, 2000, 8500}, {1200, 2150, 3500, 7400},
      {1200, 2150, 3500, 7400},  {1200, 2150, 3500, 7400},  {1200, 2150, 3500, 7400},
      {1200, 2150, 3500, 7400},  {1200, 2150, 3500, 7400},  {1650, 2300, 4200, 16500},
      {1650, 2300, 4200, 16500}, {1650, 2300, 4200, 16500}, {1650, 2300, 4200, 16500},
      {1650, 2300, 4200, 16500}, {1650, 2300, 4200, 16500}, {750, 1200, 1750, 4200},
      {750, 1200, 1750, 4200},   {750, 1200, 1750, 4200},   {1200, 1850, 3500, 7000},
      {1200, 1850, 3500, 7000},  {1650, 2300, 4200, 16500}, {1200, 1850, 3500, 7000},
      {1200, 1850, 3500, 7000},  {850, 1400, 1750, 5200},   {1250, 1900, 2600, 7200},
      {1250, 1900, 2600, 7200},  {800, 1450, 2000, 6500},   {1650, 2300, 4200, 16500},
      {850, 1400, 1750, 5200},   {1650, 2300, 4200, 16500}, {1650, 2300, 4200, 16500},
  };
  if (type < 0 || type > 32) type = 0;  // original indexes unchecked
  lo = kTab[type][flag == 0 ? 0 : 2];
  hi = kTab[type][flag == 0 ? 1 : 3];
}
inline int Ramp(int flag, int type, int v) {
  int lo, hi;
  RampRow(type, lo, hi, flag);
  if (v <= lo) return 100;
  if (v > hi) return 0;
  double frac = (double)(v - lo) / (double)(hi - lo);
  int r = 100 - (int)(frac * 100.0 + 0.5);
  if (r < 0) r = 0;
  if (r > 100) r = 100;
  return r;
}

// Species pick tables (004acc80; 8 entries, verified 006dc8x0 pairs).
inline const int *SpeciesTable(int type, int v) {
  static const int t3[8] = {6, 6, 6, 6, 7, 7, 4, 4};    // 006dc8a0++006dc860
  static const int t15[8] = {3, 4, 5, 5, 9, 9, 3, 8};   // 006dc870++006dc8d0
  static const int t16[8] = {3, 9, 5, 5, 9, 9, 3, 8};   // 006dc880++006dc8d0
  static const int t30[8] = {1, 1, 1, 2, 2, 2, 3, 8};   // 006dc820++006dc8c0
  static const int t200[8] = {1, 1, 2, 2, 3, 4, 7, 8};  // 006dc840++006dc8e0
  static const int tbig[8] = {6, 7, 7, 7, 9, 1, 1, 2};  // 006dc8b0++006dc830
  if (type == 3 || type == 5) return t3;
  if (type == 0xf) return t15;
  if (type == 0x16) return t16;
  if (v < 30) return t30;
  if (v < 200) return t200;
  return tbig;
}

// Outcome byte, record +0x54 (004ba5e0 candidate path).
// 0 = none, 1 = clean strike, 2/3 = miss classes. Both rolls rand(101).
inline int OutcomeThreshold(int line, int type) {
  switch (line) {
    case 10: return 95;   // 0x5f
    case 20: return 60;   // 0x3c
    case 30: return 30;   // 0x1e
    default: return type == 3 ? 10 : 0;
  }
}
inline int OutcomeByte(int flag, int type, int v, int line, Rng &rng) {
  int ramp = Ramp(flag, type, v);
  if (ramp == 100) return 0;
  int r = rng.RandMax(101);
  if ((flag == 1 && v < 16500) || r < ramp) return 1;
  int thr = OutcomeThreshold(line, type);
  int r2 = rng.RandMax(101);
  return (r2 >= thr) ? 3 : 2;
}

// 004ba5e0 param_1: the bite request context built by the server handler
// (004538a0). Field offsets cited from the decompilation.
struct Context {
  int x = 0, y = 0, z = 0;  // +0x3c / +0x40 / +0x44
  int state8 = -1;          // +8: != -1 forces the 40 m line rule
  int line = 10;            // +0x50: line metres {10,20,30,40}
  int f58 = 0;              // +0x58: fallback gate (0 or 1)
  int f60 = 0, f68 = 0;     // +0x60 / +0x68: fallback gates
  int flag80 = 0;           // +0x80: ramp column select (00590c00)
};

// Bite record, 0x70 bytes (session+0x738 ring). Only the fields 004ba5e0
// writes are owned here; seq/minW/rollOff/rollDiv/f64 are ring-init state
// (00471940/00590ce0) and survive a record build, exactly like the
// handler's partial word copy.
struct Record {
  uint32_t species = 0;   // +0x00 lake+0x28a94 + idx*4; 0 = no bite
  int32_t idx = 0;        // +0x04 species table index
  int32_t type = 0;       // +0x08 fish type (entry+0x190; 2 = fallback)
  uint32_t vEnc = 0;      // +0x0c v ^ 0x0E5FAF5F (00590c10/00590e70)
  double W = 0;           // +0x10 min(v*k, cap)
  int32_t tag = -1;       // +0x18 entry+0xe4c (005f7680(3)); -1 fallback
  double timing = 0;      // +0x20 second timing value
  double bonus = 0;       // +0x28 fight bonus
  double catchP = 0, failP = 0;  // +0x30 / +0x38
  int32_t x = 0, y = 0, z = 0;   // +0x44/48/4c
  int32_t f50 = 0;        // +0x50 context+0x60
  uint8_t outcome = 0;    // +0x54
  uint32_t seq = 0;       // +0x58 ring sequence (00471940)
  int16_t minW = 0;       // +0x5c s16: rand(80) + 0058ed70(line)
  uint16_t rollOff = 0;   // +0x5e rand(400)
  uint16_t rollDiv = 1;   // +0x60 rand(5)+1
  int32_t f64 = 0;        // +0x64 rand(101)
  int32_t outcome2 = 0;   // +0x6c outcome copy (handler word 0x1b)
  int V() const { return (int)(vEnc ^ 0x0E5FAF5Fu); }
  void SetV(int v) { vEnc = (uint32_t)v ^ 0x0E5FAF5Fu; }
};

// Per-candidate evaluation seam. Stands in for the unported 005f8ff0
// chain (docs/sim-bite.md section 6): returns whether the fish bites and,
// if not, whether it was rejected early (hardReject = out+0x40 == 1,
// which still allows the fallback synthesis) or late.
struct Eval {
  bool bite = false;
  bool hardReject = true;
  int type = 0;
  int v = 0;   // rolled weight grams
  int tag = -1;
};

// Fish population provider. The synthetic implementation below stands in
// for the DATA1-built per-cell lists until the lake loader is ported.
struct Stock {
  virtual ~Stock() = default;
  int gridW = 1, gridH = 1;  // lake+0x1517c / +0x15180
  virtual int Count(int cx, int cy) const = 0;
  virtual int TypeAt(int cx, int cy, int idx) const = 0;     // entry+0x190
  virtual int WeightAt(int cx, int cy, int idx) const = 0;   // grams
  virtual int TagAt(int cx, int cy, int idx) const { (void)cx; (void)cy; (void)idx; return -1; }
  virtual int SpeciesId(int idx) const = 0;                  // lake+0x28a94 + idx*4
  virtual int DepthAt(int x, int y) const = 0;               // 004a5700
  virtual Eval Evaluate(int cx, int cy, int idx, const Context &ctx,
                        Rng &rng) const = 0;                 // 005f8ff0
  virtual double AmbientActivity() const { return 1.0; }     // 0049de50(2, ...)
  virtual bool Stocked() const { return true; }              // lake+0x15128

  // 004a9490: strict cell bounds (cx*600 < x <= (cx+1)*600, likewise y);
  // returns cx + gridW*cy or -1.
  virtual int CellOf(int x, int y) const {
    if (x <= 0 || y <= 0) return -1;
    int cx = (x - 1) / 600, cy = (y - 1) / 600;
    if (cx >= gridW || cy >= gridH) return -1;
    return cx + gridW * cy;
  }
  // 004af230 walk grid (corner bits) unported; cell existence stands in.
  virtual bool Walkable(int x, int y) const { return CellOf(x, y) >= 0; }
};

// 9-step candidate walk (004ba5e0 step 2): the static 3x3 neighbourhood
// around the seed cell. dx/dy from 006dd290/006dc790/006dc7a0/006dc7f0
// plus the literal (1,1); the seed is constant across all 9 steps (the
// loop reloads it every iteration - docs/sim-bite.md 2).
inline std::vector<std::pair<int, int>> WalkCells(int seed, int w, int h) {
  static const int kD[9][2] = {{-1, -1}, {0, -1}, {-1, 0}, {0, 0}, {1, -1},
                               {1, 0},   {-1, 1}, {0, 1}, {1, 1}};
  std::vector<std::pair<int, int>> out;
  if (seed < 0) return out;
  int sx = seed % w, sy = seed / w;
  for (int i = 0; i < 9; i++) {
    int cx = kD[i][0] + sx, cy = kD[i][1] + sy;
    if (cx >= 0 && cx < w && cy >= 0 && cy < h) out.push_back({cx, cy});
  }
  return out;
}

// 004ba5e0: build one bite record into `out`. Returns true when out.species
// != 0 (a fish or the fallback synth produced a bite). Leaves out.seq /
// minW / rollOff / rollDiv / f64 untouched (ring state).
inline bool BuildRecord(const Context &ctx, const Stock &stock, Rng &rng,
                        Record &out) {
  out.x = ctx.x;
  out.y = ctx.y;
  out.z = ctx.z;
  out.f50 = ctx.f60;
  out.species = 0;
  if (!stock.Walkable(ctx.x, ctx.y)) return false;  // 004af230

  // Candidate search: collect every fish of the 3x3 neighbourhood into a
  // shuffled selection (each inserted at rand(size), 0058acc0), then take
  // the first that passes Evaluate (005f8ff0). Soft rejects (out+0x40 ==
  // 0) set the flag that suppresses the fallback synth.
  struct Cand { int cx, cy, i; };
  std::vector<Cand> sel;
  for (const auto &c : WalkCells(stock.CellOf(ctx.x, ctx.y), stock.gridW,
                                 stock.gridH)) {
    int n = stock.Count(c.first, c.second);
    for (int i = 0; i < n; i++) {
      size_t pos = (size_t)rng.RandMax((int)sel.size());
      sel.insert(sel.begin() + pos, Cand{c.first, c.second, i});
    }
  }
  bool softSeen = false;
  Eval win;
  bool have = false;
  int wcx = 0, wcy = 0, wi = 0;
  for (const Cand &c : sel) {
    Eval e = stock.Evaluate(c.cx, c.cy, c.i, ctx, rng);
    if (e.bite) {
      win = e;
      have = true;
      wcx = c.cx;
      wcy = c.cy;
      wi = c.i;
      break;
    }
    if (!e.hardReject) softSeen = true;
  }
  (void)wcx;
  (void)wcy;
  (void)wi;

  const LineRule &rule =
      LineRuleFor(ctx.state8 != -1 ? 40 : ctx.line);  // +8 forces 40 m

  if (!have) {
    // Fallback synth (004ba5e0 no-candidate path), exact gate order.
    if (softSeen) return false;
    if (!stock.Stocked()) return false;
    if (ctx.f58 != 0 && ctx.f58 != 1) return false;
    if (ctx.state8 < 1 && (ctx.f60 < 1 || ctx.f68 > 1)) return false;
    if (rng.RandMax(110) != 0) return false;                 // 1/110
    if (stock.AmbientActivity() <= 0.01) return false;       // 006d0550
    int d = stock.DepthAt(ctx.x, ctx.y);
    if (d > 130) {
      int lo, hi;
      if (d <= 250) {
        lo = (int)((double)d * 0.25);
        hi = (int)((double)d * 0.85);
      } else {
        if (ctx.z <= (int)((double)d * 0.35)) return false;
        if (ctx.z <= (int)((double)d * 0.75)) return false;
        lo = (int)((double)d * 0.75);
        hi = (int)((double)d * 0.85);
      }
      // 004aba00 ramp at z; the synth needs the upper band (t >= 81).
      int t = (lo == hi) ? 100
                         : (int)((double)(ctx.z - lo) / (double)(hi - lo) * 100.0);
      if (t < 0) t = 0;
      if (t > 100) t = 100;
      if (t < 81) return false;
    }
    out.type = 2;
    {
      int a = rng.RandMax(7), b = rng.RandMax(7);  // asm order a then b
      out.SetV(a + b + 9);                        // v in 9..22 g
    }
    out.idx = SpeciesTable(out.type, out.V())[rng.RandMax(8)];
    out.species = (uint32_t)stock.SpeciesId(out.idx);
    out.tag = -1;
    out.W = Weight(out.V(), ctx.line);
    out.timing = 0.05;
    out.bonus = 0.0;
    out.catchP = 1.0;  // docs/sim-bite.md 6: the original leaves these
    out.failP = 1.0;   // uninitialised; deterministic stand-in
    out.outcome = 0;
    out.outcome2 = 0;
    return out.species != 0;
  }

  // Candidate won: species roll, then the field formulas.
  out.type = win.type;
  out.tag = win.tag;
  out.SetV(win.v);
  out.idx = SpeciesTable(out.type, out.V())[rng.RandMax(8)];
  out.species = (uint32_t)stock.SpeciesId(out.idx);
  out.W = Weight(out.V(), ctx.line);
  out.timing = Timing32(out.V());
  out.bonus = Bonus(rule, out.type, out.V());
  Bands(rule, out.V(), out.catchP, out.failP);
  if (ctx.line == 40 && out.type != 3) {
    // 004ba5e0 40 m combine: t = 1-(1-b)*(1-t); b = 0.
    out.timing = 1.0 - (1.0 - out.bonus) * (1.0 - out.timing);
    out.bonus = 0.0;
  }
  out.outcome = (uint8_t)OutcomeByte(ctx.flag80, out.type, out.V(), ctx.line, rng);
  out.outcome2 = out.outcome;
  return true;
}

// 64 x 0x70 bite ring (session+0x738, 00471940/00473a70).
inline constexpr int kRingSize = 64;
struct Ring {
  Record rec[kRingSize];
  int idx = -1;           // session+0x734
  uint32_t counter = 0;   // session+0x730

  // 00471940 alloc + 00590ce0 field init + 0058ed70 line adjust
  // (004538a0): minW += 5 (lines 10/20), -= 20 (lines 30/40).
  Record &Push(Rng &rng, int line) {
    idx = (idx + 1) % kRingSize;
    counter++;
    Record &r = rec[idx];
    r = Record{};
    r.minW = (int16_t)rng.RandMax(80);
    r.rollOff = (uint16_t)rng.RandMax(400);
    r.rollDiv = (uint16_t)(rng.RandMax(5) + 1);
    r.f64 = rng.RandMax(101);
    r.seq = counter;
    if (line == 10 || line == 20) r.minW = (int16_t)(r.minW + 5);
    if (line == 30 || line == 40) r.minW = (int16_t)(r.minW - 20);
    return r;
  }
  // 00473a70: newest slot first, then a full 64-slot scan.
  const Record *Find(uint32_t seq) const {
    if (seq == 0) return nullptr;
    if (idx >= 0 && rec[idx].seq == seq) return &rec[idx];
    for (int i = 0; i < kRingSize; i++)
      if (rec[i].seq == seq) return &rec[i];
    return nullptr;
  }
};

// 00479700 attempt threshold by activity (obfuscated session+0xc);
// -1 = always. Ambient (action == 0) uses 500.
inline int AttemptThreshold(int act) {
  if (act < 500) return 125;
  if (act < 1000) return 250;
  if (act < 1700) return 500;
  return -1;
}
// roll = rand(1000), always consumed; success iff roll >= thr (or always).
inline bool Attempt(bool action, int activity, Rng &rng) {
  int roll = rng.RandMax(1000);
  int thr = action ? AttemptThreshold(activity) : 500;
  return thr < 0 || roll >= thr;
}

// 00592160/005922e0 sliding sum of request gaps (004538a0 limiter).
// ponytail: capacity 16 (inferred), the original's window init is open.
struct RateWindow {
  explicit RateWindow(int cap = 16) : buf((size_t)cap, 0), cap(cap) {}
  void Add(int gap) {
    sum += gap - buf[(size_t)head];
    buf[(size_t)head] = gap;
    head = (head + 1) % cap;
    if (count < cap) count++;
  }
  double Avg() const { return count == 0 ? 0.0 : (double)sum / (double)count; }
  bool Allow(int gap) {
    Add(gap);
    return Avg() >= 900.0;  // 006d6b80
  }
  std::vector<int> buf;
  int head = 0, cap = 16, count = 0;
  long long sum = 0;
};

// 00590a40 auto-bite percent (rolled in 004876d0 as rand(100) < p).
// out3 (when non-null) receives the auxiliary threshold.
inline double AutoBitePercent(int line, int p2, int *out3) {
  if (out3) *out3 = 0;
  double p = 1.0;  // 00777888
  if (line == 10) {
    if (p2 < 5) p = 1.6;        // 006d6b78
    else if (p2 < 9) p = 1.3;   // 006db108
    if (out3) {
      if (p2 > 20) *out3 = 80;
      else if (p2 > 19) *out3 = 70;
      else if (p2 > 17) *out3 = 60;
      else if (p2 > 15) *out3 = 50;
      else if (p2 > 13) *out3 = 40;
      else if (p2 > 12) *out3 = 30;
      else if (p2 > 9) *out3 = 20;
    }
  } else if (line == 20) {
    p = 1.17;                   // 0073f500
  } else if (line == 30) {
    if (p2 < 3) p = 0.3;        // 006d9f70
    else if (p2 < 4) p = 0.5;
    else if (p2 < 5) p = 0.7;   // 006db0f8
    else if (p2 < 7) p = 0.8;   // 006d0580
  } else {
    if (p2 < 5) p = 0.05;
    else if (p2 < 8) p = 0.2;
    else if (p2 < 10) p = 0.4;
  }
  return p;
}

// Deterministic stand-in populations for tests and until the DATA1 loader
// is ported. ponytail: synthetic stock, swap in lake-built populations
// when the loader is ported (docs/sim-bite.md 1).
struct SyntheticStock : Stock {
  explicit SyntheticStock(int w = 4, int h = 4) {
    gridW = w;
    gridH = h;
  }
  int Count(int cx, int cy) const override {
    return (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) ? 0 : 2 + (cx + cy) % 3;
  }
  int TypeAt(int cx, int cy, int idx) const override {
    return 1 + (cx * 3 + cy * 5 + idx) % 3;
  }
  int WeightAt(int cx, int cy, int idx) const override {
    return 200 + ((cx * 7 + cy * 11 + idx * 13) % 2000);
  }
  int TagAt(int, int, int) const override { return 7; }
  int SpeciesId(int idx) const override { return idx + 1; }
  int DepthAt(int x, int y) const override {
    (void)x;
    (void)y;
    return 100;  // <= 130: fallback depth gates pass
  }
  Eval Evaluate(int cx, int cy, int idx, const Context &, Rng &rng) const override {
    Eval e;
    e.hardReject = false;
    if (rng.RandMax(3) == 0) {
      e.bite = true;
      e.type = TypeAt(cx, cy, idx);
      e.v = WeightAt(cx, cy, idx);
      e.tag = 7;
    }
    return e;
  }
};

}  // namespace bite
