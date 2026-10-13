// Offline single-player ice-fishing sim. One system, one authority:
// docs/sim.md section 6 deltas applied 1:1 against ProPilkki2 (clean-room).
//
//  - sim runs in ORIGINAL world units (600-unit DEPTHMAP cells); the
//    render/camera layer keeps the port's +-128 8.24 frame, bridged by
//    PortX/PortY/FromPortX/FromPortY (delta 2/3).
//  - movement is click-to-move: target >= 1500 units, hole snap at 300,
//    arrival 600, stand-off 90 behind the hole; walk step
//    dir(heading deg) * K * ms, K = 0.34 (0.725 in at-hole states).
//  - drill is a mode machine: 5 -> 6 -> 0x13 (hole object) -> 0x18 ->
//    0x27 -> 0x25 line stage -> 0x1e/0x1f at-hole pose (delta 4/5).
//  - line depth is a discrete bucket {10,20,30,40} m (delta 6).
//  - bites: "Ambient" timer rand(75000)+20000 ms with a 50% roll, or an
//    action attempt (thresholds 125/250/500/always) through the 900 ms
//    avg-gap server limiter; candidates come from a 9-cell walk over
//    per-cell fish populations (delta 7/12/13).
//  - catch is probabilistic (rand(10000) vs depth-band p 0.7/0.9/0.95),
//    weight is W = min(v*k_line, cap_line), species from band tables
//    (delta 8/9/10).
//  - RNG is MSVC rand exactly (delta 11), asserted for seed 0x1234.
//  - newMechanics gates ONLY port extras (wandering fish dots); the old
//    jig speedup + depth-scaled weight were deleted with deltas 8/9.
//    Base behaviour is always original (delta 19).
// Frame slicing (delta 1) is tick::Slices in main.cpp. ForceBite is a
// non-original test hook (delta 19). SetLake(null) = empty safe.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "input.hpp"
#include "ves.hpp"

namespace game {

// ---- constants (docs/sim.md section 7 cheat-sheet) ----
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kCell = 600.0;      // world cell (grid divisor)
inline constexpr double kWalkK = 0.34;      // 006d9f80 walk step per ms
inline constexpr double kWalkKFast = 0.725; // 006d9fa0 at-hole states
inline constexpr double kMinClickDist = 1500.0;  // 00799154 min click dist
inline constexpr double kArrive = 600.0;         // 006da068 arrival radius
inline constexpr double kGuidance = 180.0;       // 006da060 guidance radius
inline constexpr double kHoleSnap = 300.0;       // 004af040 hole radius
inline constexpr double kStandOff = 90.0;        // 006da098 stand behind hole
inline constexpr double kDrillLook = 75.0;       // 006da090 drill look-ahead
// At-hole distance: arrival 600 keeps you within 600 of the snap point,
// which sits 90 from the hole (see Click below).
inline constexpr double kAtHoleDist = kArrive + kStandOff;
inline constexpr double kArriveSq = kArrive * kArrive;         // 360000
inline constexpr double kGuidanceSq = kGuidance * kGuidance;   // 32400
inline constexpr double kHoleSnapSq = kHoleSnap * kHoleSnap;   // 90000
inline constexpr int kLineM[4] = {10, 20, 30, 40};   // discrete line depth
inline constexpr double kWeightDiv[4] = {60.0, 115.0, 160.0, 230.0};
inline constexpr double kWeightCap[4] = {300.0, 30.0, 30.0, 30.0};
inline constexpr double kDeepestTable = 2500;  // .ves DEPTH_TABLE max cm
// Drill/bore durations: move-script table (+0x118/+0x52aec) unported,
// values tuned to the original's few-second drill (delta 20 open).
inline constexpr double kDrillMs = 2000.0, kBoreMs = 1000.0;
inline constexpr uint32_t kMinGapMs = 900;  // 006d6b80 bite request limiter
// Timing-engine periods (delta 18; the full 00650fe0 named-timer engine
// is not ported, only the periods the sim actually consumes).
inline constexpr uint32_t kForceRiseMs = 3000;  // "forceRise" timer
inline constexpr uint32_t kNostoMs = 1000;      // "Nosto" reel timer
inline constexpr uint32_t kEndHoldMs = 2000;    // round-end hold -> Results

// .ves DEPTH_TABLE display thresholds (delta 10: display table only, NOT
// the runtime species selector - those are the band tables below).
inline constexpr int kDepthTable[17] = {0,   30,  60,  130, 160, 200,
                                        250, 300, 350, 400, 500, 700,
                                        900, 1000, 1500, 2000, 2500};

// Game modes referenced by the sim (move timeline ids).
enum Mode : int {
  kModeFree0 = 0,        // free-walk pose (0 <-> 1)
  kModeFree1 = 1,        // free-walk pose; also no-lure fallback
  kModeAction = 5,       // drill/start allowed (004764b0 guard)
  kModeDrill = 6,        // drilling (msg 0xd)
  kModeHole = 0x13,      // hole creation pass (00480820)
  kModeAtHoleA = 0x1e,   // at-hole pose A (K = 0.725)
  kModeAtHoleB = 0x1f,   // at-hole pose B
  kModeBore = 0x18,      // bore/drilling (00477310)
  kModeLineStage = 0x25, // line/lure stage (00477880)
  kModeBoreEnd = 0x27,   // bore end (00477220)
};

// Strike resolution outcomes (00473280/004732d0/00473320 paths).
enum StrikeResult { kResNormal = 0, kResEscape = 1, kResLand = 2 };

// MSVC rand, bit-exact (00676d2c / 0059db20).
struct MsvcRand {
  uint32_t s = 0;
  void Seed(uint32_t v) { s = v; }
  uint32_t Rand() { s = s * 214013u + 2531011u; return (s >> 16) & 0x7fffu; }
  int RandMax(int n) {  // (int)(u30 * 2^-30 * n), clamp n-1; n<=0 -> 0
    if (n <= 0) return 0;
    uint32_t r1 = Rand() & 0x7fff, r2 = Rand() & 0x7fff;
    uint32_t u = (r2 << 15) | r1;  // 30-bit
    double v = (double)u * 9.313225746154785e-10 * (double)n;
    int r = (int)v;
    return r > n - 1 ? n - 1 : r;
  }
};

// Presentation only (delta 12: original has no wandering fish list; the
// port's dots exist solely behind newMechanics).
enum class State { Idle, Bite };

struct Fish {
  float x = 0, y = 0;  // original units
  State st = State::Idle;
};

// Holes are objects (delta 5): id = (cellIdx<<16)|idx, open flag, and the
// +0x18 land counter.
struct Hole {
  double x = 0, y = 0;
  uint32_t id = 0;
  bool open = true;
  int landCount = 0;
};

// Bite record (64-slot ring at session+0x738, 0x70 each).
struct Bite {
  uint32_t seq = 0;
  int species = 0;      // index into lake+0x28a94 (id table unported)
  int v = 0;            // weight grams (00590c10 tamper field)
  int lineBucket = 0;
  double catchP = 0, failP = 0;  // depth-band probabilities
  double timing32 = 0;           // record +32 second timing value
};

// 004538a0 bite-request limiter: rolling window of request gaps, avg must
// be >= 900 ms else the request is rejected ("Bite too often!").
// ponytail: window length 16 unverified (00592160 ring size open).
struct RateGate {
  static constexpr int kN = 16;
  uint32_t gaps[kN] = {};
  int n = 0, at = 0;
  uint32_t last = 0;
  bool seen = false, warn = false;
  bool Allow(uint32_t now) {
    uint32_t gap = seen ? now - last : now;
    last = now;
    seen = true;
    gaps[at] = gap;
    at = (at + 1) % kN;
    if (n < kN) n++;
    double sum = 0;
    for (int i = 0; i < n; i++) sum += gaps[i];
    bool ok = (sum / n) >= (double)kMinGapMs;
    warn = !ok && gap > 1000;  // "Bite too often!" log condition
    return ok;
  }
};

struct Game {
  // ---- public sim state ----
  double x = 0, y = 0;   // world pos, original units (004770a0 doubles)
  int ix = 0, iy = 0;    // +0x4c8/+0x4cc integer pos
  int lineDepth = 10;    // m, discrete bucket (delta 6)
  float jigPhase = 0;    // cosmetic bobber cycle (angler)
  std::vector<Hole> holes;
  std::vector<Fish> fishes;  // presentation only (newMechanics, delta 12)
  int catches = 0;
  double totalKg = 0, lastKg = 0, biggest = 0;  // +0x354/358/35c tallies
  bool newMechanics = false;  // gates port extras only (delta 19)
  bool justDrilled = false, justCaught = false, justBite = false;
  int activity = 0;       // obfuscated +0xc session field (setter open)
  bool rateWarn = false;  // "Bite too often!" fired this slice
  // 4 bait types x 16 lures grid (0058e5d0 set / 0058f760 clear /
  // 0058f7c0 consume on catch, delta 14).
  // ponytail: per-slot counts unported; 10 keeps the empty-lure gate
  // reachable but non-trivial.
  static constexpr int kBaitTypes = 4, kLureSlots = 16, kBaitPerSlot = 10;
  int bait[kBaitTypes][kLureSlots] = {};

  // ---- lifecycle ----
  void Seed(uint32_t s) {
    rng_.Seed(s);
    ClearTimers();
  }

  // Null grids = no lake: movement works, drill/fish guarded off
  // (original drill guard needs a lake, +0x528).
  void SetLake(const ves::Grid *d, const ves::Grid *t,
               const std::vector<ves::ObjGroup> *g = nullptr) {
    (void)t;  // typemap drives terrain tint only, not sim (delta 3)
    (void)g;  // object positions are scene decoration, not fish (delta 12)
    hasLake_ = d && !d->cells.empty() && d->w > 0 && d->h > 0;
    if (hasLake_) depth_ = *d;
    holes.clear();
    fishes.clear();
    catches = 0;
    totalKg = lastKg = biggest = 0;
    ClearTimers();
    mode_ = kModeAction;
    lineBucket_ = 0;
    lineDepth = kLineM[0];
    x = GridW() * kCell * 0.5;  // start at grid centre (old frame centre)
    y = GridH() * kCell * 0.5;
    ix = (int)x;
    iy = (int)y;
    for (int i = 0; i < kBaitTypes; i++)  // join hook 00445970 fills grid
      for (int j = 0; j < kLureSlots; j++) bait[i][j] = kBaitPerSlot;
    if (newMechanics)
      for (int i = 0; i < 10; i++) fishes.push_back(PresentationFish());
  }

  // ---- queries ----
  bool Biting() const { return biteActive_; }
  bool LineOut() const { return lineOut_; }
  int Mode() const { return mode_; }
  bool Walking() const { return walking_; }
  bool Arrived() const { return arrived_; }
  double GridW() const { return hasLake_ ? (double)depth_.w : 2.0; }
  double GridH() const { return hasLake_ ? (double)depth_.h : 2.0; }
  bool ActiveLure() const { return bait[activeType_][activeLure_] > 0; }

  bool AtHole(float r = (float)kAtHoleDist) const {
    double r2 = (double)r * r;
    for (auto &h : holes) {
      if (!h.open) continue;
      double dx = h.x - x, dy = h.y - y;
      if (dx * dx + dy * dy <= r2) return true;
    }
    return false;
  }

  bool ActiveHole(double &hx, double &hy) const {
    const Hole *h = FindOpenHole(x, y, kAtHoleDist);
    if (!h) return false;
    hx = h->x;
    hy = h->y;
    return true;
  }

  // Original units -> port +-128 render frame (delta 3): a 600-unit cell
  // maps to one grid step of the +-128 frame.
  float PortX(double gx) const {
    return (float)(gx * (256.0 / (kCell * GridW())) - 128.0);
  }
  float PortY(double gy) const {
    return (float)(gy * (256.0 / (kCell * GridH())) - 128.0);
  }
  double FromPortX(float px) const {
    return ((double)px + 128.0) * (kCell * GridW() / 256.0);
  }
  double FromPortY(float py) const {
    return ((double)py + 128.0) * (kCell * GridH() / 256.0);
  }

  // 004a5700 bilinear depth at position (600-unit cells, raw cell scale).
  float DepthAt(double wx, double wy) const {
    if (!hasLake_) return 0;
    int w = depth_.w, h = depth_.h;
    double fx = wx / kCell, fy = wy / kCell;
    fx = std::max(0.0, std::min(fx, (double)w - 1));
    fy = std::max(0.0, std::min(fy, (double)h - 1));
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    double tx = fx - x0, ty = fy - y0;
    double a = depth_.cells[(size_t)y0 * w + x0];
    double b = depth_.cells[(size_t)y0 * w + x1];
    double c = depth_.cells[(size_t)y1 * w + x0];
    double d2 = depth_.cells[(size_t)y1 * w + x1];
    return (float)((a * (1 - tx) + b * tx) * (1 - ty) +
                   (c * (1 - tx) + d2 * tx) * ty);
  }

  // Display depth in cm via the .ves DEPTH_TABLE (presentation only).
  float DepthCm(double wx, double wy) const {
    int c = (int)DepthAt(wx, wy);
    return (float)kDepthTable[std::min(16, c * 17 / 256)];
  }

  // 004af230 walkability. Bounds from the cell grid stand in for the lake
  // walk grid (lake+0x15168 corner bit 0x10) until that structure is
  // parsed (delta 3 open); in-bounds is always walkable today.
  bool Walkable(double wx, double wy) const {
    double mx = (GridW() - 1) * kCell, my = (GridH() - 1) * kCell;
    return wx >= 0 && wy >= 0 && wx <= mx && wy <= my;
  }

  // ---- pure formulas (docs/sim.md section 4) ----

  // 004538a0 catch/fail probability bands by depth.
  static void CatchBand(double depth, double &catchP, double &failP) {
    if (depth < 150) {
      catchP = 0.7;
      failP = 0.1;
    } else if (depth < 300) {
      catchP = 0.9;
      failP = 0.4;
    } else {
      catchP = 0.95;
      failP = 0.5;
    }
  }

  // 00478210 strike resolution: rand(10000) vs stored probabilities.
  // escape 004732d0, land 00473320, normal = fish stays hooked.
  static int ResolveStrike(int roll, double catchP, double failP) {
    if (roll < failP * 10000) return kResEscape;
    if (roll < catchP * 10000) return kResLand;
    return kResNormal;
  }

  // 00479700 attempt-B threshold by activity; -1 = always.
  static int BiteThreshold(int act) {
    if (act < 500) return 125;
    if (act < 1000) return 250;
    if (act < 1700) return 500;
    return -1;
  }

  // Weight: W = min(v * k_line, cap_line) where v = tamper-decoded grams
  // (delta 9). ponytail: output unit follows the original formula; the
  // results builder 004c2ce0 that consumes it is unported (open).
  static double WeightG(int v, int bucket) {
    if (bucket < 0) bucket = 0;
    if (bucket > 3) bucket = 3;
    return std::min((double)v / kWeightDiv[bucket], kWeightCap[bucket]);
  }

  // Bite record +32 second timing value.
  static double Timing32(int v) {
    if (v >= 1000) return 0.16666;
    if (v > 300) return 0.05 + (v - 300) * 0.0001666571;
    return 0.05;
  }

  // Section 4.4.5 species pick tables (indices into lake+0x28a94 + idx*4;
  // the id table itself is unported). Fallback fish are type 2.
  static const int *SpeciesTableFor(int type, double depth) {
    static const int t30[8] = {1, 1, 1, 2, 9, 1, 1, 2};    // v < 30
    static const int t200[8] = {1, 1, 2, 2, 0, 1, 1, 3};   // v < 200
    static const int tbig[8] = {6, 7, 7, 7, 2, 2, 3, 8};   // else
    static const int t3[8] = {6, 6, 6, 6, 6, 7, 7, 7};     // type 3 (5 same)
    static const int t15[8] = {3, 4, 5, 5, 3, 9, 5, 5};    // type 0xf
    static const int t16[8] = {3, 9, 5, 5, 1, 4, 5, 6};    // type 0x16
    if (type == 3 || type == 5) return t3;
    if (type == 0xf) return t15;
    if (type == 0x16) return t16;
    if (depth < 30) return t30;
    if (depth < 200) return t200;
    return tbig;
  }

  // Verified pre-round countdown chain (delta 15): each hop when elapsed
  // >= session+0x46c; the durations live in the unported move-script
  // table, so the port enters play directly at mode 5 (open, delta 20).
  // 9 -> 10 -> 0xc -> 0xe -> 0x10 -> {0x12 | 0x31 | 0x33}.
  static int CountdownNext(int mode) {
    switch (mode) {
      case 9: return 10;
      case 10: return 0xc;
      case 0xc: return 0xe;
      case 0xe: return 0x10;
      case 0x10: return 0x12;  // 0x31/0x33 alternates unported (open)
      default: return -1;
    }
  }

  // ---- input actions ----

  // Rod slot keys 1/2/3 -> line buckets 10/20/30 m (delta 6).
  void SetSlot(int slot) {
    if (slot < 0 || slot > 2) return;
    lineBucket_ = slot;
    lineDepth = kLineM[slot];
  }

  // 0047f530/00476c00 click-to-move order (delta 2). Clicks closer than
  // 1500 units are ignored unless already arrived (+0x698); a click on a
  // hole snaps to the 90-unit stand-off point and toggles the at-hole
  // pose (0x1e <-> 0x1f).
  void Click(double wx, double wy) {
    double dx = wx - x, dy = wy - y, d2 = dx * dx + dy * dy;
    Hole *h = NearestOpenHole(wx, wy, kHoleSnap);  // 004aeee0 (90000 sq)
    if (!h && d2 < kMinClickDist * kMinClickDist && !arrived_) return;
    if (h) {
      atHoleTarget_ = true;  // +0x4bc
      if (mode_ == kModeAtHoleA)
        mode_ = kModeAtHoleB;  // 00476c00 pose toggle 0x1e <-> 0x1f
      else if (mode_ == kModeAtHoleB)
        mode_ = kModeAtHoleA;
      double ux = h->x - x, uy = h->y - y;
      double ul = std::sqrt(ux * ux + uy * uy);
      if (ul < 1e-9) {  // standing on the hole: use current dir
        ux = -dirX_;
        uy = -dirY_;
        ul = 1.0;
      }
      walkX_ = h->x - ux / ul * kStandOff;  // hole + unit * (-90)
      walkY_ = h->y - uy / ul * kStandOff;
      walking_ = true;
      arrived_ = false;
      return;
    }
    if (!Walkable(wx, wy)) return;  // 004b0bb0 route check (bounds stand-in)
    atHoleTarget_ = false;
    walkX_ = wx;
    walkY_ = wy;
    walking_ = true;
    arrived_ = false;
  }

  // 004764b0 drill start: mode 5, lake present, no open hole within 300
  // of the drill point pos + dir*75. Mode -> 6 (delta 4).
  void DrillStart() {
    if (mode_ != kModeAction || !hasLake_ || atHoleTarget_) return;
    double dpx = x + dirX_ * kDrillLook, dpy = y + dirY_ * kDrillLook;
    if (NearestOpenHole(dpx, dpy, kHoleSnap)) return;  // hole already here
    drillX_ = dpx;
    drillY_ = dpy;
    drillMsLeft_ = kDrillMs;
    walking_ = false;
    mode_ = kModeDrill;
  }

  // Strike / jig action (Space): resolves an active bite record, else an
  // action bite attempt through the 900 ms limiter (delta 7/8).
  void Strike() {
    if (biteActive_) {
      const Bite &b = ring_[activeSlot_];
      int r = ResolveStrike(rng_.RandMax(10000), b.catchP, b.failP);
      if (r == kResNormal) return;  // fish still hooked, strike again
      if (r == kResLand) LandFish(b);
      ClearBite();
      ambientMs_ = -1;  // recreate "Ambient" with a fresh random period
      return;
    }
    if (!lineOut_ || !ActiveLure() || !AtHole()) return;
    if (!rateGate_.Allow(nowMs_)) {
      rateWarn = rateGate_.warn;
      return;
    }
    StartBite(true);
  }

  // Per-slice update. dt in seconds, pre-sliced <= 1 s by tick::Slices
  // (delta 1, 00471810) exactly like the original's entity slicer.
  void Update(const input::Keys &k, float dt) {
    justDrilled = justCaught = justBite = false;
    rateWarn = false;
    if (dt < 0) dt = 0;
    double ms = (double)dt * 1000.0;
    nowMs_ += (uint32_t)ms;
    uint32_t edges = k.bits & ~prevBits_;
    prevBits_ = k.bits;
    // Edge actions on the input bitmap (0063ca80 shape; the VK->command
    // map of 00495930 is unported, delta 20 open).
    if (edges & ((uint32_t)1 << 6)) DrillStart();   // Enter/E drill
    if (edges & ((uint32_t)1 << 8)) Strike();       // Space strike/jig
    if (edges & ((uint32_t)1 << 4)) LineBucket(-1); // PgUp shallower
    if (edges & ((uint32_t)1 << 5)) LineBucket(+1); // PgDn deeper
    // Walk order: click target beats held-direction keys (0047f530 feed).
    if (walking_) {
      WalkStepToTarget(ms);
    } else {
      double dx = (input::Down(k, 3) ? 1 : 0) - (input::Down(k, 2) ? 1 : 0);
      double dy = (input::Down(k, 1) ? 1 : 0) - (input::Down(k, 0) ? 1 : 0);
      if (dx != 0 || dy != 0) {
        double l = std::sqrt(dx * dx + dy * dy);
        Step(dx / l, dy / l, ms);
      }
    }
    AdvanceModes(ms);
    FishingUpkeep(ms);
    // Cosmetic bobber cycle (angler::JigOffset; presentation only).
    if (input::Down(k, 8) && lineOut_)
      jigPhase += dt * 8;
    else
      jigPhase = std::max(0.f, jigPhase - dt * 4);
    if (newMechanics) WanderDots(ms);
  }

  // Non-original test hook: guaranteed-catch bite record now (delta 19).
  void ForceBite() {
    if (!hasLake_ || !AtHole() || biteActive_) return;
    Bite rec;
    if (!MakeBiteRecord(rec)) {  // synthetic record when cells are empty
      rec.v = 1000;
      rec.species =
          SpeciesTableFor(2, DepthAt(x, y))[0];
    }
    rec.catchP = 1;
    rec.failP = 0;
    rec.timing32 = Timing32(rec.v);
    PushBite(rec);
  }

 private:
  ves::Grid depth_;
  bool hasLake_ = false;
  MsvcRand rng_;
  RateGate rateGate_;
  int mode_ = kModeAction;
  uint32_t prevBits_ = 0, nowMs_ = 0;
  double heading_ = 0;         // +0x520, degrees
  double dirX_ = 0, dirY_ = 1; // +0x508/+0x510 velocity dir
  bool walking_ = false, arrived_ = true, atHoleTarget_ = false;
  double walkX_ = 0, walkY_ = 0;
  double drillX_ = 0, drillY_ = 0, drillMsLeft_ = 0, boreMsLeft_ = 0;
  bool lineOut_ = false;
  int activeType_ = 0, activeLure_ = 0;
  int lineBucket_ = 0;
  double ambientMs_ = -1;  // "Ambient" timer, -1 = not armed (+0x638)
  static constexpr int kRingSize = 64;  // bite ring at session+0x738
  Bite ring_[kRingSize];
  int ringNext_ = -1, activeSlot_ = -1;
  uint32_t biteSeq_ = 0;
  bool biteActive_ = false;

  void ClearTimers() {
    mode_ = kModeAction;
    prevBits_ = 0;
    nowMs_ = 0;
    heading_ = 0;
    dirX_ = 0;
    dirY_ = 1;
    walking_ = false;
    arrived_ = true;
    atHoleTarget_ = false;
    walkX_ = walkY_ = 0;
    drillMsLeft_ = boreMsLeft_ = 0;
    lineOut_ = false;
    activeType_ = activeLure_ = 0;
    ambientMs_ = -1;
    ringNext_ = activeSlot_ = -1;
    biteSeq_ = 0;
    biteActive_ = false;
    rateGate_ = RateGate{};
    activity = 0;
    rateWarn = false;
  }

  void LineBucket(int delta) {  // PgUp/PgDn edges, clamp to {10..40} m
    int b = lineBucket_ + delta;
    if (b < 0) b = 0;
    if (b > 3) b = 3;
    lineBucket_ = b;
    lineDepth = kLineM[b];
  }

  void SetHeading(double tx, double ty) {  // +0x520 degrees
    heading_ = std::atan2(tx - x, ty - y) * 180.0 / kPi;
  }

  // 004770a0 walk step: s = ms * K (0.34; 0.725 in at-hole states).
  void Step(double dirx, double diry, double ms) {
    bool atHoleMode = mode_ == kModeAtHoleA || mode_ == kModeAtHoleB;
    double s = ms * (atHoleMode ? kWalkKFast : kWalkK);
    double nx = x + dirx * s, ny = y + diry * s;
    if (nx < 0) nx = 0;  // clamp >= 0 per axis
    if (ny < 0) ny = 0;
    bool moved = false;
    if (nx != x && Walkable(nx, y)) {
      x = nx;
      moved = true;
    }
    if (ny != y && Walkable(x, ny)) {
      y = ny;
      moved = true;
    }
    if (!moved) walking_ = false;  // wall (walk grid bounds, delta 3 open)
    dirX_ = dirx;
    dirY_ = diry;
    heading_ = std::atan2(dirx, diry) * 180.0 / kPi;
    ix = (int)x;
    iy = (int)y;
  }

  void WalkStepToTarget(double ms) {
    double dx = walkX_ - x, dy = walkY_ - y, d2 = dx * dx + dy * dy;
    if (d2 <= kArriveSq) {  // 006da068 arrival radius 600
      arrived_ = true;
      walking_ = false;
      return;
    }
    double l = std::sqrt(d2);
    Step(dx / l, dy / l, ms);
  }

  // 00480820-driven mode chain: drill 6 -> hole 0x13 -> bore 0x18 ->
  // bore end 0x27 -> line stage 0x25 -> at-hole pose 0x1e.
  void AdvanceModes(double ms) {
    if (mode_ == kModeDrill) {
      drillMsLeft_ -= ms;
      if (drillMsLeft_ <= 0) {
        mode_ = kModeHole;  // 0x13 hole creation pass
        CreateHole(drillX_, drillY_);
        boreMsLeft_ = kBoreMs;
        mode_ = kModeBore;  // 0x18 (00477310)
      }
    } else if (mode_ == kModeBore) {
      boreMsLeft_ -= ms;
      if (boreMsLeft_ <= 0) mode_ = kModeBoreEnd;  // 0x27 (00477220)
    }
    if ((mode_ == kModeBoreEnd || mode_ == kModeAction) && !lineOut_) {
      // 00477880 line stage: needs a reachable open hole + active lure
      // (00488060 gates line-out on the lure).
      Hole *h = NearestOpenHole(x, y, kAtHoleDist);
      if (h && ActiveLure()) {
        activeHoleId_ = h->id;
        lineOut_ = true;
        mode_ = kModeLineStage;  // 0x25
      } else if (mode_ == kModeBoreEnd && !ActiveLure()) {
        // ponytail: 00488060 sends mode 1 (free-walk pose) without a
        // lure; port returns to mode 5 so a restock can resume drilling.
        mode_ = kModeAction;
      }
    } else if (mode_ == kModeLineStage) {
      mode_ = kModeAtHoleA;  // 0x1e at-hole pose
    }
  }

  void FishingUpkeep(double ms) {
    if (lineOut_) {
      Hole *h = NearestOpenHole(x, y, kAtHoleDist);
      if (!h || !ActiveLure())
        ReelIn();  // walked away / bait dry -> line in, mode 5
      else
        activeHoleId_ = h->id;
    }
    if (lineOut_ && ActiveLure() && !biteActive_) {
      if (ambientMs_ < 0)  // +0x640 && +0x638 == -1: create "Ambient"
        ambientMs_ = (double)(rng_.RandMax(75000) + 20000);  // 20..95 s
      ambientMs_ -= ms;
      if (ambientMs_ <= 0) {
        if (rng_.RandMax(1000) >= 500) StartBite(false);  // 50% roll
        if (biteActive_)
          ambientMs_ = -1;  // timer killed on success
        else
          ambientMs_ = (double)(rng_.RandMax(75000) + 20000);
      }
    }
  }

  void ReelIn() {
    lineOut_ = false;
    atHoleTarget_ = false;
    mode_ = kModeAction;
    ambientMs_ = -1;
  }

  void CreateHole(double wx, double wy) {
    Hole h;
    h.x = wx;
    h.y = wy;
    h.open = true;  // record[3] == 0 = open, required by lookups
    h.landCount = 0;
    int idx = (int)holes.size() & 0xffff;
    h.id = ((uint32_t)CellIdx(wx, wy) << 16) | (uint32_t)idx;
    holes.push_back(h);
    justDrilled = true;
  }

  int CellIdx(double wx, double wy) const {  // 004a9490
    if (!hasLake_) return 0;
    return (int)(wy / kCell) * depth_.w + (int)(wx / kCell);
  }

  // 004a9490 seed -> 9-step candidate walk over depth cells (delta 7).
  // ponytail: docs/sim.md records the seed advance as "seed = cy"; taken
  // literally that wraps negative cells to uint and degenerates to
  // out-of-bounds cells only, so the cell index is rebuilt (cy*w+cx).
  // Re-verify against a captured run (delta 20 open).
  bool MakeBiteRecord(Bite &rec) {
    if (!hasLake_) return false;
    static const int kDx[9] = {-1, 0, 1, 1, 1, 1, -1, 0, 1};
    static const int kDy[9] = {-1, -1, -1, 0, -1, 0, 1, 1, 1};
    int w = depth_.w, h = depth_.h;
    int seed = CellIdx(x, y);
    for (int i = 0; i < 9; i++) {
      int cx = kDx[i] + seed % w;
      int cy = kDy[i] + seed / w;
      seed = cy * w + cx;
      if (cx < 0 || cy < 0 || cx >= w || cy >= h) continue;
      int n = CellCount(cx, cy);
      if (n <= 0) continue;
      rec.v = CellWeight(cx, cy, rng_.RandMax(n));  // rand(count) pick
      break;
    }
    if (rec.v <= 0) {
      // No-candidate fallback: depth gates + rare roll synthesizes a
      // type-2 fish (0049de50 distribution unported -> 1% stand-in).
      double d = DepthAt(x, y);
      if (d < 81 || d > 251) return false;
      if (rng_.RandMax(1000) >= 10) return false;
      rec.v = 100 + rng_.RandMax(3500);
    }
    double depth = DepthAt(x, y);
    CatchBand(depth, rec.catchP, rec.failP);
    rec.timing32 = Timing32(rec.v);
    rec.species = SpeciesTableFor(2, depth)[rng_.RandMax(8)];
    rec.lineBucket = lineBucket_;
    return rec.v > 0;
  }

  // Per-cell fish populations (lake+0x240c4 lists). Real populations come
  // from the lake file (unported, open); synthesised deterministically
  // per cell so bite selection is reproducible.
  static uint32_t CellHash(int cx, int cy) {
    uint32_t h = (uint32_t)cx * 374761393u + (uint32_t)cy * 668265263u + 1u;
    h ^= h >> 13;
    h *= 1274126177u;
    return h ^ (h >> 16);
  }
  static int CellCount(int cx, int cy) {
    return (int)(CellHash(cx, cy) >> 5) % 4;  // 0..3 fish per cell
  }
  int CellWeight(int cx, int cy, int i) {  // grams
    MsvcRand r;
    r.Seed(CellHash(cx, cy) ^ (uint32_t)(i * 0x9e3779b9 + 1));
    return 100 + r.RandMax(3500);
  }

  void PushBite(const Bite &rec) {
    ringNext_ = (ringNext_ + 1) % kRingSize;  // 64-slot ring +0x738
    ring_[ringNext_] = rec;
    activeSlot_ = ringNext_;
    biteActive_ = true;
    justBite = true;
    ambientMs_ = -1;
  }

  // 00479700 bite attempt: roll vs threshold (50% ambient, activity
  // thresholds for actions), then candidate search (004ba5e0 shape).
  bool StartBite(bool action) {
    int thr = action ? BiteThreshold(activity) : 500;
    if (thr >= 0 && rng_.RandMax(1000) < thr) return false;
    Bite rec;
    if (!MakeBiteRecord(rec)) return false;
    rec.seq = ++biteSeq_;
    PushBite(rec);
    return true;
  }

  // 00473320 landed fish: weight via line bucket, bait consumed, per-hole
  // counter, tally. Release-effect threshold is v >= 470 g (type 3: 670);
  // the swim-away spawn 004a0780 is render-only and unported (open).
  void LandFish(const Bite &b) {
    double w = WeightG(b.v, b.lineBucket);  // min(v*k_line, cap_line)
    lastKg = w;
    totalKg += w;
    catches++;
    if (w > biggest) biggest = w;
    ConsumeBait();  // 0058f7c0 bait usage
    Hole *h = NearestOpenHole(x, y, kAtHoleDist);
    if (h) h->landCount++;  // hole +0x18
    justCaught = true;
  }

  void ConsumeBait() {  // 0058f7c0: consume, then advance a dry slot
    if (bait[activeType_][activeLure_] > 0) bait[activeType_][activeLure_]--;
    for (int j = 0; j < kLureSlots && !ActiveLure(); j++)
      activeLure_ = (activeLure_ + 1) % kLureSlots;
  }

  void ClearBite() {
    biteActive_ = false;
    activeSlot_ = -1;
  }

  // 004af040 nearest open hole within radius r (linear scan; the original
  // prefilters by cell, hole counts stay tiny here).
  Hole *NearestOpenHole(double wx, double wy, double r) {
    Hole *best = nullptr;
    double bd = r * r;
    for (auto &h : holes) {
      if (!h.open) continue;
      double dx = h.x - wx, dy = h.y - wy, d = dx * dx + dy * dy;
      if (d <= bd) {
        bd = d;
        best = &h;
      }
    }
    return best;
  }
  const Hole *FindOpenHole(double wx, double wy, double r) const {
    const Hole *best = nullptr;
    double bd = r * r;
    for (auto &h : holes) {
      if (!h.open) continue;
      double dx = h.x - wx, dy = h.y - wy, d = dx * dx + dy * dy;
      if (d <= bd) {
        bd = d;
        best = &h;
      }
    }
    return best;
  }

  float Randf() { return (float)(rng_.Rand() * (1.0 / 32768.0)); }

  // newMechanics-only presentation dots; no logic reads them (delta 12).
  Fish PresentationFish() {
    Fish f;
    f.x = (float)rng_.RandMax((int)(GridW() * kCell));
    f.y = (float)rng_.RandMax((int)(GridH() * kCell));
    return f;
  }
  void WanderDots(double ms) {
    for (auto &f : fishes)
      if (f.st == State::Idle) {
        f.x += (Randf() - 0.5f) * (float)(kCell * ms / 150.0);
        f.y += (Randf() - 0.5f) * (float)(kCell * ms / 150.0);
      }
  }

  uint32_t activeHoleId_ = 0;  // session current hole id (+0x4c0)
};

}  // namespace game
