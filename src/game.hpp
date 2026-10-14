// Offline single-player ice-fishing sim. One system, one authority:
// game.hpp drives the player; the verified modules own the formulas.
//
//  - world/walk/click unchanged (docs/sim-walk.md): 600-unit DEPTHMAP
//    cells, click-to-move 0.34 units/ms (0.725 in run modes), hole snap
//    at 300 with a 90-unit stand-off; PortX/PortY bridge the +-128 frame.
//  - move/mode machine + drill chain: timeline.hpp (docs/sim-timeline.md).
//    Drill start (004764b0) -> 6 -> stance -> chain 9->10->0xc->0xe->0x10
//    (1000 ms stages, hold 0047a1c0) -> outcome 0x12 -> 0x13 hole creation
//    (00480820) -> 0x18 sitting/fishing. A hole drill takes ~12 s like the
//    original. Round end: mode-set rising edge + kEndHoldMs = 2300
//    (00650fe0 end timer; the old 2000 was the ramp target).
//  - bites: bite.hpp (docs/sim-bite.md): bite::BuildRecord over
//    fishstock::Stock populations (docs/sim-stock.md), 64-slot ring
//    (00471940), bite::RateWindow request limiter (avg gap >= 900 ms).
//    Attempts roll ctrl::BiteRoll (00479700): rec == 0 -> rand(1000) <=
//    500 (Ambient path), else thresholds 125/250/500/always on the record
//    weight grams (the old "activity" label was wrong).
//  - fight: controller.hpp (docs/sim-controller.md): the 0xd drill run
//    (ctrl::Session: InitDrill/DrillStepAdvance/DrillEnd) resolves the
//    hooked fish; forceRise = 3000 ms strike hold aborts into the forced
//    power*0.75 run. Line depth words +0x414/+0x418/+0x41c/+0x420 and the
//    004ac620 bucket ladder drive the run depths.
//  - RNG: one stream, bite::Rng (= game::MsvcRand); ctrl::'s LCG copy is
//    state-synced at the boundary (same generator).
//  - newMechanics gates ONLY port extras (wandering fish dots).
// Frame slicing (delta 1) is tick::Slices in main.cpp. ForceBite is a
// non-original test hook. SetLake(null) = empty safe.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "bite.hpp"
#include "controller.hpp"
#include "fishstock.hpp"
#include "input.hpp"
#include "timeline.hpp"
#include "ves.hpp"
#include "walkgrid.hpp"

namespace game {

// ---- constants (docs/sim.md section 7 cheat-sheet) ----
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kCell = 600.0;      // world cell (grid divisor)
inline constexpr double kWalkK = 0.34;      // 006d9f80 walk step per ms
inline constexpr double kWalkKFast = 0.725; // 006d9fa0 run modes 0x1e/0x1f
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
inline constexpr int kLineM[4] = {10, 20, 30, 40};   // line metres (lure class)
inline constexpr double kDeepestTable = 2500;  // .ves DEPTH_TABLE max cm
inline constexpr uint32_t kForceRiseMs = 3000; // "forceRise" hold (0x400->0x800)
// Round-end hold -> Results (00650fe0 end timer 0x8fc; 2000 is the fade
// ramp target, docs/sim-timeline.md section 5).
inline constexpr int kEndHoldMs = timeline::kEndTimerMs;  // 2300
// "Nosto" fish-lift timer: 00650fe0("Nosto", 0, 1000, 00487560(+0x340), 0,
// 8) — the duration is the 00487560(+0x340) formula, not 1000 ms.
// ponytail: 00487560 unported (+0x340 semantics open); the lift runs on
// the NOSTO move durations (0x25/0x26, 500 ms) until that lands.

// .ves DEPTH_TABLE display thresholds (presentation only, NOT the species
// selector — those are bite::SpeciesTable, indexed by weight).
inline constexpr int kDepthTable[17] = {0,   30,  60,  130, 160, 200,
                                        250, 300, 350, 400, 500, 700,
                                        900, 1000, 1500, 2000, 2500};

// Move/mode ids (human2.3ds STARTMOVES; docs/sim-timeline.md section 2).
enum Mode : int {
  kModeWalk1 = 0,   // KAVELY1 walk cycle
  kModeWalk2 = 1,   // KAVELY2 walk cycle; no-lure fallback
  kModeAction = 5,  // PAIKALLAAN standing — drill start allowed (004764b0)
  kModeDrill = 6,   // KAIRAUSASENTOON1 drill stance (004764b0 -> 6)
  kModeHole = 0x13, // KAANNY_JATTAAKSEEN hole creation pass (00480820)
  kModeSit = 0x18,  // JUST_SITTING sitting/fishing at hole (00477310 -> 0x18)
  kModeRunA = 0x1e, // JUOKSU1 run (fast step K = 0.725)
  kModeRunB = 0x1f, // JUOKSU2 run
  kModeLift = 0x25, // NOSTO1 fish lift (00477880 -> 0x25)
  kModeMend = 0x27, // KORJAILU_ASENTOON mending stance (00477220 -> 0x27)
};

// MSVC rand, bit-exact (00676d2c / 0059db20). ONE implementation shared
// with the modules: game::MsvcRand IS bite::Rng. ctrl:: keeps its own
// copy of the same LCG (ctrl::MsvcRand) — synced at the call boundary,
// see CtrlRng/SyncRng below.
using MsvcRand = bite::Rng;

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

struct Game {
  Game() : mach_(timeline::HumanScript()) {}
  Game(const Game &) = delete;  // stock depth callback captures this
  Game &operator=(const Game &) = delete;

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
  bool rateWarn = false;  // "Bite too often!" fired this slice
  // 4 bait types x 16 lures grid (0058e5d0 set / 0058f760 clear /
  // 0058f7c0 consume on catch, delta 14).
  // ponytail: per-slot counts unported; 10 keeps the empty-lure gate
  // reachable but non-trivial.
  static constexpr int kBaitTypes = 4, kLureSlots = 16, kBaitPerSlot = 10;
  int bait[kBaitTypes][kLureSlots] = {};
  // Drill skill 0042bd00() for the chain hold (0047a1c0); 0 = the plain
  // 1000 ms stages. Chain outcome bytes +0x4e8 (bottom) / +0x6c0 (alt).
  int drillSkill = 0;
  bool drillBottom = false, drillAlt = false;

  // ---- lifecycle ----
  void Seed(uint32_t s) {
    seed_ = s;
    rng_.Seed(s);
    ClearTimers();
  }

  // Null grids = no lake: movement works, drill/fish guarded off
  // (original drill guard needs a lake, +0x528). With a lakePath the
  // fish stock comes from the .ves DATA1 section + data0.dat species
  // tables; missing/corrupt files degrade to the documented synthetic
  // stand-in (bite::SyntheticStock) — never crash, never zero bites.
  // ponytail: synthetic stand-in until main wires the paths (I2); the
  // upgrade is passing real paths here and this branch just disappears.
  void SetLake(const ves::Grid *d, const ves::Grid *t,
               const std::vector<ves::ObjGroup> *g = nullptr,
               const std::string &lakePath = std::string(),
               const std::string &speciesPath = std::string()) {
    (void)t;  // typemap drives terrain tint only, not sim (delta 3)
    (void)g;  // object positions are scene decoration, not fish (delta 12)
    hasLake_ = d && !d->cells.empty() && d->w > 0 && d->h > 0;
    if (hasLake_)
      depth_ = *d;
    else
      depth_ = ves::Grid{};
    BuildStock(lakePath, speciesPath);
    holes.clear();
    fishes.clear();
    catches = 0;
    totalKg = lastKg = biggest = 0;
    ClearTimers();
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

  // Corner-byte walk grid (004af230). The builder (004b6990, from
  // data/type.dat + depth.dat) is unported, so hosts pass a grid once a
  // source exists; without one Walkable keeps the bounds behaviour.
  void SetWalkGrid(const walkgrid::GridView &gv) { walk_ = gv; }

  // ---- queries ----
  bool Biting() const { return biteActive_; }
  bool LineOut() const { return lineOut_; }
  int Mode() const { return mach_.Mode(); }
  bool Walking() const { return walking_; }
  bool Arrived() const { return arrived_; }
  double GridW() const { return hasLake_ ? (double)depth_.w : 2.0; }
  double GridH() const { return hasLake_ ? (double)depth_.h : 2.0; }
  bool ActiveLure() const { return bait[activeType_][activeLure_] > 0; }

  // Round-end mode sets (00476180 || 00476160) + rising edge (the end
  // timer is created on the edge; docs/sim-timeline.md section 5).
  bool RoundEndMode() const {
    int m = Mode();
    return timeline::IsRoundEndMode(m) || timeline::IsLiftMode(m);
  }
  bool EndModeEdge() const { return endModeEdge_; }

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

  // 004af230 walkability. With a loaded corner grid the 16-case mask rule
  // decides (walkgrid.hpp); the world ends at (w-1)*600 x (h-1)*600
  // (004b8b20 extents) in the bounds fallback, which is what makes Step
  // stop at the wall.
  bool Walkable(double wx, double wy) const {
    if (walk_.data && walk_.w > 0 && walk_.h > 0)
      return walkgrid::Walkable(walk_, wx, wy);
    double mx = (GridW() - 1) * kCell, my = (GridH() - 1) * kCell;
    return wx >= 0 && wy >= 0 && wx <= mx && wy <= my;
  }

  // 004875f0 walk/stand-up anim timer (states 4/8). The sim's walk is the
  // continuous 0.34/ms step; this is the timer formula for hooks/tests.
  static int WalkMs(int players, bool doubled) {
    return ctrl::WalkMs(players, doubled);
  }

  // ---- input actions ----

  // Rod slot keys 1/2/3 -> line buckets 10/20/30 m (delta 6).
  void SetSlot(int slot) {
    if (slot < 0 || slot > 2) return;
    lineBucket_ = slot;
    lineDepth = kLineM[slot];
  }

  // 0047f530/00476c00 click-to-move order (delta 2, docs/sim-walk.md).
  // Verified verdict: an out-of-bounds click (x,y >= 0) is ACCEPTED at
  // order entry -- 0047f530 rejects only negative coordinates and
  // 004b0bb0 checks obstacles/holes, not grid bounds -- never clamped;
  // the walk grid then stops the walker mid-walk (Step keeps the last
  // legal step). Clicks closer than 1500 units (00799154) are ignored
  // unless already arrived (+0x698); a click near an open hole snaps to
  // the 90-unit stand-off point (00476c00) and toggles the run pose
  // (0x1e <-> 0x1f).
  void Click(double wx, double wy) {
    Hole *h = NearestOpenHole(wx, wy, kHoleSnap);  // 004aeee0 (90000 sq)
    walkgrid::HoleView hv;
    const walkgrid::HoleView *hp = nullptr;
    if (h) {
      hv.x = h->x;
      hv.y = h->y;
      hv.busy = false;  // record +0x10 (004af1b0) unported on Hole
      hp = &hv;
    }
    walkgrid::ClickOrder o = walkgrid::Decide(wx, wy, x, y, arrived_,
                                              kMinClickDist, hp, -dirX_, -dirY_);
    if (o.kind == walkgrid::kReject) return;
    if (o.kind == walkgrid::kHoleSnap) {
      atHoleTarget_ = true;  // +0x4bc
      if (mach_.Mode() == kModeRunA)
        JumpMode(kModeRunB);  // 00476c00 pose toggle 0x1e <-> 0x1f
      else if (mach_.Mode() == kModeRunB)
        JumpMode(kModeRunA);
      walkX_ = o.tx;
      walkY_ = o.ty;
      walking_ = true;
      arrived_ = false;
      return;
    }
    atHoleTarget_ = false;
    walkX_ = o.tx;
    walkY_ = o.ty;
    walking_ = true;
    arrived_ = false;
  }

  // 004764b0 drill start + chain reset (00480820's driver): guards are
  // mode 5, lake present, +0x4bc == 0, no hole within 300 of the drill
  // point pos + dir*75. On success mode -> 6 and the drilling chain
  // stamps +0x450/+0x470 with +0x4e4 = 2 (plain drill start).
  void DrillStart() {
    if (!hasLake_ || atHoleTarget_ || mach_.Mode() != kModeAction) return;
    double dpx = x + dirX_ * kDrillLook, dpy = y + dirY_ * kDrillLook;
    if (NearestOpenHole(dpx, dpy, kHoleSnap)) return;  // hole already here
    modeChain_.mode = kModeAction;
    modeChain_.holeOpen = lineOut_;  // +0x4bc: line is down a hole
    if (modeChain_.DrillStart(true) != 1 ||
        modeChain_.mode != ctrl::MDrillStart)
      return;
    drillX_ = dpx;
    drillY_ = dpy;
    holePending_ = true;
    chain_ = timeline::Chain{};
    chainIn_ = timeline::ChainInput{2, drillBottom, drillAlt};
    JumpMode(ctrl::MDrillStart);
    walking_ = false;
  }

  // Strike / jig action (Space): a hooked fish enters the 0xd fight
  // (ctrl::Session drill run); otherwise an action bite attempt through
  // the 900 ms request limiter (004538a0).
  void Strike() {
    if (fightActive_) return;
    if (biteActive_) {
      StartFight();
      return;
    }
    if (!lineOut_ || !ActiveLure() || !AtHole()) return;
    uint32_t gap = nowMs_ - lastReqMs_;
    lastReqMs_ = nowMs_;
    bool ok = rateWin_.Allow((int)gap);
    rateWarn = !ok && gap > 1000;  // "Bite too often!" log condition
    if (!ok) return;
    BiteAttempt(true);
  }

  // 00479700 bite attempt (public: bot hooks). action = the strike/jig
  // request path (record-driven thresholds on its weight grams); !action
  // = the Ambient timer path (rec == 0, 50 % roll first, then the record
  // build — the 004ba5e0 candidate pick happens on success).
  bool BiteAttempt(bool action) {
    if (!hasLake_ || biteActive_ || fightActive_) return false;
    if (!lineOut_ || !ActiveLure() || !AtHole()) return false;
    if (!action) {
      ctrl::MsvcRand cr(1);
      cr.s = rng_.s;
      bool pass = ctrl::BiteRoll(cr, false, 0);  // rec == 0: 50 %
      rng_.s = cr.s;
      if (!pass) return false;
    }
    bite::Record &rec = ring_.Push(rng_, kLineM[lineBucket_]);
    if (!BuildBite(rec)) return false;
    if (action) {
      ctrl::MsvcRand cr(1);
      cr.s = rng_.s;
      bool pass = ctrl::BiteRoll(cr, true, rec.V());  // thresholds on grams
      rng_.s = cr.s;
      if (!pass) return false;
    }
    biteActive_ = true;
    justBite = true;
    ambientMs_ = -1;  // 00479700 kills "Ambient" on success
    return true;
  }

  // 00473320/00473280 landed/catch processing: weight tally, bait
  // consume, per-hole counter. Release effect 470/670 + msg 0x1f stay
  // unported (docs/sim-bite.md integration notes).
  bool LandFish() {
    const bite::Record *r = ActiveBite();
    if (!r) return false;
    lastKg = r->W;
    totalKg += r->W;
    catches++;
    if (r->W > biggest) biggest = r->W;
    ConsumeBait();  // 0058f7c0 bait usage
    Hole *h = NearestOpenHole(x, y, kAtHoleDist);
    if (h) h->landCount++;  // hole +0x18
    justCaught = true;
    ClearBite();
    ambientMs_ = -1;
    return true;
  }

  // Reel in (walk-away / dry lure): line up, stand pose.
  void Reel() {
    lineOut_ = false;
    atHoleTarget_ = false;
    modeChain_.holeOpen = false;
    JumpMode(kModeAction);
    ambientMs_ = -1;
  }

  // Mode edges (00477310 / 00477220 / 00477880) for hooks + the fight.
  int BoreBegin() {
    modeChain_.mode = mach_.Mode();
    int m = modeChain_.BoreBegin();
    JumpMode(m);
    return m;
  }
  int BoreEnd(bool busy = false) {
    modeChain_.mode = mach_.Mode();
    int m = modeChain_.BoreEnd(busy);
    JumpMode(m);
    return m;
  }
  void LineStage() {
    modeChain_.mode = ctrl::MLineStage;  // 00477880: mode -> 0x25
    JumpMode(ctrl::MLineStage);
  }

  // ---- fight (0xd) ----
  bool Fighting() const { return fightActive_; }
  // The run as InitDrill built it (snapshotted on its init frame, after
  // that frame's 200 ms tick — hence ticks is expect or expect-1). The
  // live run clears its depth array on exit, the snapshot keeps it.
  const ctrl::DrillRun &Fight() const { return fightRun_; }
  ctrl::FightEnd LastFightEnd() const { return lastEnd_; }

  // Hooked fish -> the 0xd drill run (kala_force). The run is fully timer
  // driven (docs/sim-controller.md section 2); forceRise (strike held
  // >= kForceRiseMs) restarts it forced at power*0.75.
  void StartFight() {
    const bite::Record *r = ActiveBite();
    if (!r || fightActive_) return;
    holeDepth_ = (int)DepthCm(x, y);  // +0x420 (cm via DEPTH_TABLE)
    sess_ = ctrl::Session{};
    sess_.inputSettled = true;  // 0048ff90: nothing pending
    sess_.verdict = 0;          // +0x33c: land path
    // +0x414 at entry: the lure drop (004abf20 bucket 2 ~ 0.9*d).
    // ponytail: the +0x414 ease chain (state 7 ramp) unported; one rung
    // below the hole bottom stands in.
    sess_.depth = ctrl::BucketDepth(holeDepth_, 2);
    sess_.target = sess_.depth + 0x14;  // +0x418 = +0x414 + 20
    sess_.bucket = ctrl::Bucket(holeDepth_, sess_.target);  // 004ac620
    sess_.holeDepth = holeDepth_;
    sess_.fishW = r->W;          // record +0x10 double
    sess_.fishType = r->type;    // record +8 (type-3 check)
    sess_.pGood = r->catchP;     // working copy +0x310
    sess_.pBad = r->failP;       // working copy +0x308
    sess_.recWeight = r->V();    // 00590c10(record)
    sess_.state = ctrl::SFightPrep;
    sess_.pending = ctrl::SFightPrep;
    LineStage();  // 00477880: mode -> 0x25 (NOSTO lift)
    fightActive_ = true;
    fightCaptured_ = false;
    fightRun_ = ctrl::DrillRun{};
    fightMs_ = 0;
    fightHoldMs_ = 0;
    forced_ = false;
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
    if (!fightActive_) {
      // Walk order: click target beats held-direction keys (0047f530).
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
    }
    StepTimeline(ms);
    AdvanceSim(ms);
    fightStrikeHeld_ = input::Down(k, 8) && fightActive_;
    StepFight(ms);
    // Cosmetic bobber cycle (angler::JigOffset; presentation only).
    if (input::Down(k, 8) && lineOut_)
      jigPhase += dt * 8;
    else
      jigPhase = std::max(0.f, jigPhase - dt * 4);
    if (newMechanics) WanderDots(ms);
    bool em = RoundEndMode();
    endModeEdge_ = em && !prevEndMode_;
    prevEndMode_ = em;
  }

  // Non-original test hook: guaranteed-catch bite record now (delta 19).
  void ForceBite() {
    if (!hasLake_ || !AtHole() || biteActive_) return;
    bite::Record &rec = ring_.Push(rng_, kLineM[lineBucket_]);
    if (!BuildBite(rec)) {
      // Synthetic fallback record (type 2); ring state (seq/rolls) kept.
      rec.type = 2;
      rec.SetV(1000);
      rec.idx = bite::SpeciesTable(rec.type, rec.V())[0];
      rec.species = (uint32_t)StockProvider().SpeciesId(rec.idx);
      rec.W = bite::Weight(rec.V(), kLineM[lineBucket_]);
      rec.timing = bite::Timing32(rec.V());
    }
    rec.catchP = 1;
    rec.failP = 0;
    biteActive_ = true;
    justBite = true;
    ambientMs_ = -1;
  }

  // ---- bot hook reads (botai::Hooks) ----
  const bite::Record *ActiveBite() const {
    return biteActive_ ? ring_.Find(ring_.rec[ring_.idx].seq) : nullptr;
  }
  int BiteCount() const { return (int)ring_.counter; }

 private:
  ves::Grid depth_;
  bool hasLake_ = false;
  MsvcRand rng_;  // the one stream (bite::Rng)
  bite::RateWindow rateWin_{16};
  uint32_t lastReqMs_ = 0;
  fishstock::Stock stock_;          // DATA1 populations
  bite::SyntheticStock synth_;      // documented stand-in (no lake file)
  bool useSynth_ = true;
  walkgrid::GridView walk_;         // corner bytes (004af230), optional
  timeline::Machine mach_;          // move/mode machine (00467210 clock)
  timeline::Chain chain_;           // drill chain (00480820, +0x450/+0x470)
  timeline::ChainInput chainIn_;
  ctrl::ModeChain modeChain_;       // 004764b0/00477310/00477220 edges
  ctrl::Session sess_;              // 0xd fight state machine (0048af00)
  ctrl::DrillRun fightRun_;         // snapshot of the run InitDrill built
  bool fightCaptured_ = false;
  bool fightActive_ = false, fightStrikeHeld_ = false, forced_ = false;
  double fightMs_ = 0, fightHoldMs_ = 0;
  ctrl::FightEnd lastEnd_ = ctrl::FightEnd::Normal;
  bool endModeEdge_ = false, prevEndMode_ = false;
  uint32_t prevBits_ = 0, nowMs_ = 0;
  double heading_ = 0;         // +0x520, degrees
  double dirX_ = 0, dirY_ = 1; // +0x508/+0x510 velocity dir
  bool walking_ = false, arrived_ = true, atHoleTarget_ = false;
  double walkX_ = 0, walkY_ = 0;
  double drillX_ = 0, drillY_ = 0;
  bool holePending_ = false;
  int holeDepth_ = 0;  // +0x420
  bool lineOut_ = false;
  int activeType_ = 0, activeLure_ = 0;
  int lineBucket_ = 0;
  double ambientMs_ = -1;  // "Ambient" timer, -1 = not armed (+0x638)
  bite::Ring ring_;        // 64 x 0x70 bite ring (session+0x738)
  bool biteActive_ = false;
  uint32_t activeHoleId_ = 0;  // session current hole id (+0x4c0)
  uint32_t seed_ = 0;

  // ctrl:: keeps a copy of the MSVC LCG (same generator); one stream is
  // shared by syncing the state word at the boundary.
  ctrl::MsvcRand CtrlRng() {
    ctrl::MsvcRand r(1);
    r.s = rng_.s;
    return r;
  }
  void SyncRng(ctrl::MsvcRand &r) { rng_.s = r.s; }

  // 00464410 jump + apply now (a bare Jump leaves cur = -1 until the next
  // clock tick, which would make Mode() read stale inside the same call).
  void JumpMode(int m) {
    mach_.Jump(m);
    mach_.Update(0);
  }

  void BuildStock(const std::string &lakePath, const std::string &speciesPath) {
    stock_ = fishstock::Stock{};
    bool ok = false;
    if (!lakePath.empty() && hasLake_) {
      ok = stock_.LoadLake(lakePath);
      if (ok) {
        stock_.LoadSpecies(speciesPath.empty()
                               ? std::string("extracted/data/nyky/data0.dat")
                               : speciesPath);
        stock_.SetSeed(seed_);
        stock_.SetDepth([this](int px, int py) {
          return (int)DepthAt((double)px, (double)py);
        });
        stock_.Place((int)GridW(), (int)GridH());
      }
    }
    synth_ = bite::SyntheticStock((int)GridW(), (int)GridH());
    useSynth_ = !ok || stock_.fish.empty();  // empty DATA1 -> stand-in
  }
  const bite::Stock &StockProvider() const {
    return useSynth_ ? (const bite::Stock &)synth_ : (const bite::Stock &)stock_;
  }

  void ClearTimers() {
    mach_ = timeline::Machine(timeline::HumanScript());
    mach_.Queue(kModeAction);
    mach_.Update(0);  // mode 5 immediately (PAIKALLAAN)
    chain_ = timeline::Chain{};
    chainIn_ = timeline::ChainInput{};
    modeChain_ = ctrl::ModeChain{};
    sess_ = ctrl::Session{};
    fightRun_ = ctrl::DrillRun{};
    fightCaptured_ = false;
    fightActive_ = false;
    fightStrikeHeld_ = forced_ = false;
    fightMs_ = fightHoldMs_ = 0;
    endModeEdge_ = prevEndMode_ = false;
    prevBits_ = 0;
    nowMs_ = 0;
    lastReqMs_ = 0;
    heading_ = 0;
    dirX_ = 0;
    dirY_ = 1;
    walking_ = false;
    arrived_ = true;
    atHoleTarget_ = false;
    walkX_ = walkY_ = 0;
    drillX_ = drillY_ = 0;
    holePending_ = false;
    holeDepth_ = 0;
    lineOut_ = false;
    activeType_ = activeLure_ = 0;
    ambientMs_ = -1;
    ring_ = bite::Ring{};
    biteActive_ = false;
    rateWin_ = bite::RateWindow{16};
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

  // 004770a0 walk step: s = ms * K (0.34; 0.725 in run modes).
  void Step(double dirx, double diry, double ms) {
    int m = mach_.Mode();
    bool fast = m == kModeRunA || m == kModeRunB;
    double s = ms * (fast ? kWalkKFast : kWalkK);
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
    // The original advances the walk every slice (004770a0) and treats
    // arrival as an after-the-fact flag (0047f740: +0x698 at 600 sq);
    // stepping first closes onto the snap/stand-off point instead of
    // freezing one arrival-radius short of it.
    double l = std::sqrt(d2);
    if (l > 1e-9) {
      Step(dx / l, dy / l, ms);
      dx = walkX_ - x;
      dy = walkY_ - y;
      d2 = dx * dx + dy * dy;
    }
    if (d2 <= kArriveSq) {  // 006da068 arrival radius 600
      arrived_ = true;
      walking_ = false;
    }
  }

  // Move/mode clock (00467210) + drill chain (00480820). Chunked at
  // 100 ms so short passes (0x13 hole creation, 450 ms) are never hopped
  // over inside one big slice.
  void StepTimeline(double ms) {
    int total = (int)ms;
    if (total <= 0) {
      mach_.Update(0);
      return;
    }
    for (int rem = total; rem > 0;) {
      int s = std::min(rem, 100);
      mach_.Update(s);
      rem -= s;
      int mode = mach_.Mode();
      if (mode >= 9 && mode <= 0x11) {
        int next = chain_.Step(mode, (int)nowMs_, timeline::HoldMs(drillSkill),
                               chainIn_);
        if (next != -1) mach_.Queue(next);
      }
      if (mode == kModeHole && holePending_) {  // 00480820 hole pass
        CreateHole(drillX_, drillY_);
        modeChain_.HolePass();
        holePending_ = false;
      }
    }
  }

  void AdvanceSim(double ms) {
    // Line drop: sitting at the hole (0x18) — or standing at one — with a
    // lure (00488060 gates line-out on the lure).
    if (!lineOut_ && !fightActive_ &&
        (mach_.Mode() == kModeSit || mach_.Mode() == kModeAction)) {
      Hole *h = NearestOpenHole(x, y, kAtHoleDist);
      if (h && ActiveLure()) {
        activeHoleId_ = h->id;
        lineOut_ = true;
        modeChain_.holeOpen = true;
      }
    }
    if (lineOut_) {
      Hole *h = NearestOpenHole(x, y, kAtHoleDist);
      if (!h || !ActiveLure()) {
        if (!fightActive_) Reel();  // walked away / bait dry -> line in
      } else {
        activeHoleId_ = h->id;
      }
    }
    if (lineOut_ && ActiveLure() && !biteActive_ && !fightActive_) {
      if (ambientMs_ < 0)  // +0x640 && +0x638 == -1: create "Ambient"
        ambientMs_ = (double)(rng_.RandMax(75000) + 20000);  // 20..95 s
      ambientMs_ -= ms;
      if (ambientMs_ <= 0) {
        if (BiteAttempt(false))
          ambientMs_ = -1;  // timer killed on success
        else
          ambientMs_ = (double)(rng_.RandMax(75000) + 20000);
      }
    }
  }

  // The 0xd fight step (docs/sim-controller.md section 2). Session timers
  // tick in 200 ms quanta so any Update slice resolves on time.
  void StepFight(double ms) {
    if (!fightActive_) return;
    fightMs_ += ms;
    unsigned bits = 0;
    if (fightStrikeHeld_) {
      fightHoldMs_ += ms;
      if (fightHoldMs_ >= (double)kForceRiseMs && !forced_) {
        bits |= ctrl::AHoldLong;  // 0x800: forceRise fired
        forced_ = true;
      }
    }
    for (int rem = (int)ms; rem > 0;) {
      int step = std::min(rem, 200);
      bool was = sess_.state == ctrl::SFight || sess_.state == ctrl::SFightPrep;
      ctrl::MsvcRand cr = CtrlRng();
      sess_.Step(bits, step, cr);
      SyncRng(cr);
      bits = 0;
      rem -= step;
      if (!fightCaptured_ && sess_.fight.count > 0) {
        fightRun_ = sess_.fight;  // init frame done: pristine run
        fightCaptured_ = true;
      }
      bool now = sess_.state == ctrl::SFight || sess_.state == ctrl::SFightPrep;
      if (was && !now) {
        FinishFight();
        return;
      }
    }
    // ponytail: hard cap; the Stall loop (step timer mid-segment) is
    // bounded by stepMs in practice — raise only if a run ever hangs.
    if (fightMs_ > 600000.0) FinishFight();
  }

  // 0xd exit mapping: 0xe landed / 0x10 retract-after-catch both land the
  // fish (00473320/00473280); anything else is the 004732d0 escape.
  void FinishFight() {
    if (!fightActive_) return;
    fightActive_ = false;
    if (sess_.state == ctrl::SLanded || sess_.state == ctrl::SRetractCatch) {
      lastEnd_ = sess_.state == ctrl::SRetractCatch ? ctrl::FightEnd::Catch
                                                    : ctrl::FightEnd::Normal;
      LandFish();
    } else {
      lastEnd_ = ctrl::FightEnd::Escape;
      ClearBite();
      ambientMs_ = -1;
    }
    BoreBegin();  // 00477310 from 0x25 -> 0x18 (sit/fish again)
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

  // 004ba5e0 record build over the stock (candidate walk, selection
  // shuffle, field formulas, fallback synth) — see bite::BuildRecord.
  bool BuildBite(bite::Record &rec) {
    if (!hasLake_) return false;
    const bite::Stock &st = StockProvider();
    bite::Context ctx;
    ctx.x = (int)x;
    ctx.y = (int)y;
    int d = st.DepthAt(ctx.x, ctx.y);
    // Lure at ~0.9 of the hole depth: the fallback synth's depth gates
    // compare z against d*0.35/0.75 (same 004a5700 units).
    ctx.z = (int)((double)d * 0.9);
    ctx.state8 = -1;               // +8: normal line rule
    ctx.line = kLineM[lineBucket_];
    ctx.f58 = 0;
    ctx.f60 = 1;  // fallback gates open (004ba5e0 no-candidate path)
    ctx.f68 = 0;
    ctx.flag80 = 0;
    return bite::BuildRecord(ctx, st, rng_, rec);
  }

  void ConsumeBait() {  // 0058f7c0: consume, then advance a dry slot
    if (bait[activeType_][activeLure_] > 0) bait[activeType_][activeLure_]--;
    for (int j = 0; j < kLureSlots && !ActiveLure(); j++)
      activeLure_ = (activeLure_ + 1) % kLureSlots;
  }

  void ClearBite() {
    biteActive_ = false;
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
};

}  // namespace game
