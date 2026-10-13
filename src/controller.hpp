#pragma once
// controller.hpp — clean-room port of the Propilkki 2 player controller.
//
// Image VAs cited as 0048af00 = 0x0048af00. Analysis: docs/sim-controller.md.
// Ported 1:1 where the code is verified; external predicates (HUD, net, input
// device queries) are plain fields the host fills in before Step().
// No SDL, C++17, header-only.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ctrl {

// --- RNG: MSVC rand (00676d2c) and rand(max) (0059db20) -----------------
// ponytail: duplicate of game.hpp's MsvcRand, orchestrator dedupes later.
struct MsvcRand {
  unsigned s;
  explicit MsvcRand(unsigned seed = 1u) : s(seed) {}
  int operator()() {              // 00676d2c: s = s*214013 + 2531011
    s = s * 214013u + 2531011u;
    return (int)((s >> 16) & 0x7fffu);
  }
  int rand(int max) {             // 0059db20: two draws -> 30-bit u, u * 2^-30 * max
    if (max == 0) return 0;
    unsigned r1 = (unsigned)(*this)() & 0x7fffu;
    unsigned r2 = (unsigned)(*this)() & 0x7fffu;
    unsigned u = (r2 << 15) | r1;
    int v = (int)((double)u * 9.313225746154785e-10 * (double)max);
    return (v == max) ? max - 1 : v;
  }
};

// --- timer (00650fe0 create / 00651220 fired / 00651410 kill) -----------
// 00651220 verified: a missing or stale handle reads as FIRED (returns 1);
// that bootstraps the state-0xd tick/step timers on their first frame.
// ponytail: linear interpolation for every easing mode (8/9/0xb/0xc/0x1d);
// exact curves live in 00651620 (open). Only durations/firing drive states.
struct Timer {
  bool active = false;
  double from = 0, to = 0;
  int dur = 0, left = 0;
  int mode = 8;
  int* out = nullptr;

  void Start(double f, double t, int d, int* slot, int m) {
    from = f; to = t; dur = d; left = d; mode = m; out = slot; active = true;
    if (out) *out = (int)from;
  }
  void Kill() { active = false; if (out) *out = 0; out = nullptr; }
  bool Fired() const { return !active; }   // 00651220 semantics
  // Advance by dt ms; completes exactly once (Fired() then holds).
  void Tick(int dt) {
    if (!active) return;
    left -= dt;
    if (left > 0) return;
    if (out) *out = (int)to;
    active = false;
    out = nullptr;
  }
};

// --- line-depth bucket ladder ------------------------------------------
// 004ac620(holeDepth, target) -> bucket {0,1,2}
inline int Bucket(int holeDepth, int target) {
  if (holeDepth < 0x83) return 2;
  if (holeDepth < 0xfb) return ((holeDepth - 0x32) / 2 + 0x32 <= target) ? 2 : 0;
  if (target < 0x88) return 0;
  return ((holeDepth * 2) / 3 <= target) ? 2 : 1;
}
// 004acc40 — reel in one rung
inline int ReelIn(int holeDepth, int bucket) {
  if (holeDepth < 0x83) return 2;
  if (holeDepth < 0xfb) return (bucket == 2) ? 0 : bucket;
  if (bucket == 1) return 0;
  if (bucket == 2) return 1;
  return bucket;
}
// 004acbf0 — let out one rung
inline int LetOut(int holeDepth, int bucket) {
  if (holeDepth < 0x83) return 2;
  if (holeDepth < 0xfb) return (bucket == 0) ? 2 : bucket;
  if (bucket == 0) return 1;
  if (bucket == 1) return 2;
  return bucket;
}
// 004abf20 — target depth for a bucket
inline int BucketDepth(int holeDepth, int bucket) {
  double d = (double)holeDepth;
  if (holeDepth < 0x83) return (int)(d * 0.9);
  if (holeDepth < 0xfb) {
    if (bucket == 0) return (int)(d * 0.2 + 30.0);
    if (bucket == 1 || bucket == 2) return (int)(d * 0.95);
    return (int)(d * 0.9);
  }
  if (bucket == 0) return (int)(d * 0.1 + 40.0);
  if (bucket == 1) return (int)(d * 0.55);
  if (bucket == 2) return (int)(d * 0.95);
  return (int)(d * 0.9);
}

// --- duration helpers --------------------------------------------------
// 004875f0 — walk / stand-up timer ms (players = 004487e0 chain; the
// "player count" label is (inferred), the field chain is verified).
inline int WalkMs(int players, bool doubled) {
  int ms = (players < 8) ? (0x17 - players) * 0x19 : 400;
  return doubled ? ms * 2 : ms;
}

// 004873c0 — depth-ease duration (x = same +0x230 value, lureType = 10/30/40)
inline int DepthEaseMs(int diff, int x, int lureType) {
  if (x < 1) x = 1;
  double ramp;
  if (x == 1) ramp = -12.5;
  else if (x == 2) ramp = -10.0;
  else if (x < 9) ramp = -10.0 - (double)(x - 2) * -0.8333333333333333;
  else if (x < 16) ramp = -5.0 - (double)(x - 8) * -0.35714285714285715;
  else ramp = -2.5;
  double scale = 1.0;
  if (lureType == 10 && x < 6) scale = 0.5 + (double)(x - 1) * 0.125;
  else if (lureType == 30 && x < 8) scale = 3.0 - (double)(x - 1) * 0.3333333333333333;
  else if (lureType == 40 && x < 11) scale = 5.0 - (double)(x - 1) * 0.4444444444444444;
  return 500 - (int)((double)diff * ramp * scale);
}

// --- 00590a40 — auto-bite factor + percent -----------------------------
// lureType: 10 / 20 / 30 (line class), x = +0x230 value. Returns the factor;
// *outP = percent used as rand(100) < p for action bit 0x1000.
inline double AutoBite(int lureType, int x, int* outP) {
  double f = 1.0;
  if (outP) *outP = 0;
  if (lureType == 10) {
    f = (x < 5) ? 1.6 : (x < 9) ? 1.3 : 1.0;
    if (outP) {
      if (x > 20) *outP = 80;
      else if (x > 18) *outP = 70;
      else if (x > 17) *outP = 60;
      else if (x > 15) *outP = 50;
      else if (x > 13) *outP = 40;
      else if (x > 12) *outP = 30;
      else if (x > 9) *outP = 20;
    }
  } else if (lureType == 20) {
    f = 1.17;
  } else if (lureType == 30) {
    if (x < 3) f = 0.3;
    else if (x < 4) f = 0.5;
    else if (x < 5) f = 0.7;
    else if (x < 7) f = 0.8;
  } else {
    if (x < 5) f = 0.05;
    else if (x < 8) f = 0.2;
    else if (x < 10) f = 0.4;
  }
  return f;
}

// --- 00479700 — bite attempt roll --------------------------------------
// recWeight = 00590c10(record) in grams. rec == 0 -> ambient 50 % path.
inline bool BiteRoll(MsvcRand& rng, bool haveRec, int recWeight) {
  int roll = rng.rand(1000);
  if (!haveRec) return roll <= 500;
  if (recWeight >= 1700) return true;
  int thr = (recWeight < 500) ? 125 : (recWeight < 1000) ? 250 : 500;
  return roll <= thr;
}

// --- game-mode edges (004764b0 / 00477310 / 00477220 / 00480820) -------
enum Mode : int {
  MIdle = 5,          // ready to drill
  MDrillStart = 6,
  MHoleCreate = 0x13, // hole creation pass (00480820)
  MLineStage = 0x25,
  MBorePrep = 0x26,
  MBore = 0x18,
  MBoreAlt = 0x29,    // 00477310 fallback target
  MBoreEnd = 0x27,
  MAtHoleA = 0x1e,
  MAtHoleB = 0x1f,
  MSnapHole = 0x20,
};

struct ModeChain {
  int mode = MIdle;
  int bore = 0;          // +0x438
  bool holeOpen = false; // +0x4bc

  // 004764b0 — drill start. Guards: mode==5, !holeOpen, no hole within 300
  // units of the drill point (host supplies the exclusion as noHoleNearby).
  int DrillStart(bool noHoleNearby) {
    if (mode != MIdle) return 2;
    if (holeOpen) { mode = MSnapHole; return 1; }        // 0x20 branch
    if (!noHoleNearby) return 4;
    mode = MDrillStart;
    return 1;
  }
  // 00477310 — bore begin: 0x18 when already in {0x25,0x26,0x18}, else 0x29.
  int BoreBegin() {
    bore = 1;
    mode = (mode == MLineStage || mode == MBorePrep || mode == MBore) ? MBore : MBoreAlt;
    return mode;
  }
  // 00477220 — bore end -> 0x27 (extra guards when bore == 2).
  int BoreEnd(bool busy) {
    if (bore == 2) {
      bore = 0;
      if (mode < 5 || mode == MAtHoleA || mode == MAtHoleB || busy) return mode;
    }
    bore = 0;
    mode = MBoreEnd;
    return mode;
  }
  // 00480820 — hole creation pass at mode 0x13.
  void HolePass() {
    if (mode == MHoleCreate) holeOpen = true;
  }
  // Timeline default next (00467210 next-table, data). 6 -> 0x13 -> 0x18 are
  // (inferred): those links are not written in code (verified edges above).
  int Next() {
    switch (mode) {
      case MDrillStart: mode = MHoleCreate; break;
      case MHoleCreate: mode = MBore; break;
    }
    return mode;
  }
};

// --- the 0xd machine (fish fight / reel-in) ----------------------------
// docs/sim-controller.md §2. All offsets and constants match the original.
struct DrillParams {
  int depth0 = 0;      // session+0x414 at entry
  int maxDepth = 0;    // session+0x420 (hole depth)
  double fishW = 0;    // record+0x10 double at +0x2f8
  int fishType = 0;    // record+8 at +0x2f0
  double pGood = 0;    // record+0x28 at +0x310 (working copy)
  double pBad = 0;     // record+0x20 at +0x308 (working copy)
  int recWeight = 0;   // 00590c10(record)
  bool forced = false; // 0x800 forceRise restart
};

struct DrillRun {
  int ticks = 0;       // 7bcc90  remaining 200 ms ticks
  int power = 0;       // 7bcca0
  int stop = 0;        // 7bcc9c  tick at which the outcome fires
  int stepMs = 0;      // 0079916c
  int stepTicks = 0;   // ticks per depth step
  int count = 0;       // 7bcc98  depth array length
  int stepIdx = 0;     // 7bcca4
  bool dir = false;    // 7bcca8
  bool lastDone = false; // 7bcc96
  bool good = false;   // 7bcc94  breakthrough / catch
  bool bad = false;    // 7bcc95  jam / escape
  bool forced = false; // 7bcca9
  std::vector<int> depth; // session+0x5b8
};

// Init block of state 0xd: array build + outcome rolls. RNG call order is
// exactly the original's: rand(4), then 3 x rand(width) per array entry,
// then rand(10000), rand(ticks), [rand(3)], rand(10000), rand(ticks).
inline DrillRun InitDrill(const DrillParams& p, MsvcRand& rng) {
  DrillRun r;
  r.forced = p.forced;
  r.stepMs = (rng.rand(4) + 13) * 200;
  r.power = p.depth0 < 0x46 ? 0x46 : p.depth0;
  double ms = p.forced
              ? (double)r.power * 0.75
              : (p.fishW * 0.5 + 0.8) * (double)r.power + p.fishW * 70.0;
  r.ticks = (int)(ms * 0.04 + 0.5);
  r.stepTicks = r.stepMs / 200;
  int downStep = (int)((double)r.stepTicks * -25.0);
  int upStep = (int)((double)r.stepTicks * 25.0);
  r.count = r.ticks / r.stepTicks + 1;
  r.depth.assign((size_t)r.count, 0);
  r.depth[0] = p.depth0;
  int writeIdx = 1;
  for (int t = 0; t <= r.ticks && writeIdx < r.count; t++) {
    if (t / r.stepTicks < writeIdx) continue;
    int lo = (int)((double)r.power - (double)t * 25.0);
    if (lo < 0) lo = 0;
    int up = (int)((double)(r.ticks - t) * 25.0);
    if (p.maxDepth <= up) up = p.maxDepth;
    if (up < lo) up = lo;
    int prev = r.depth[(size_t)writeIdx - 1];
    int candHi = prev + upStep;
    if (up <= candHi) candHi = up;
    int lo2 = prev + downStep;
    if (lo < lo2) lo = lo2;
    int target = (t == r.ticks) ? 0
                : (int)((double)(r.ticks - t) / (double)r.ticks * (double)r.power);
    int width = candHi - lo;
    int best = 0;
    for (int k = 0; k < 3; k++) {
      int cand = lo + rng.rand(width);
      auto dist = [](int a, int b) { return a > b ? a - b : b - a; };
      if (k == 0 || dist(target, cand) < dist(target, best)) best = cand;
      if (writeIdx < r.count - 1 && best < 0x3c) best = 0x3c;
    }
    r.depth[(size_t)writeIdx] = best;
    writeIdx++;
    if (writeIdx == r.count) break;
  }
  // outcome rolls (both run; the second overwrites stop)
  if (rng.rand(10000) < (int)(p.pGood * 10000.0)) {
    r.stop = rng.rand(r.ticks);
    if (p.fishType == 3 && p.recWeight > 1000 && rng.rand(3) < 2) r.stop = r.ticks;
    r.good = true;
  }
  if (rng.rand(10000) < (int)(p.pBad * 10000.0)) {
    r.stop = rng.rand(r.ticks);
    r.bad = true;
  }
  return r;
}

// Step-timer fire handler (state 0xd per-frame): advances +0x414 along the
// array one eased segment per stepMs; the last segment eases it to 0 over
// (ticks+1)*200 ms. Returns the easing mode the gen3 timer uses (8..11).
inline int DrillStepAdvance(DrillRun& r, int curDepth, int& from, int& to, int& dur) {
  if (r.stepIdx + 1 < r.count) {
    int lo = r.depth[(size_t)r.stepIdx];
    int hi = r.depth[(size_t)r.stepIdx + 1];
    int turn = 0;
    if (r.stepIdx + 2 < r.count) {
      bool up1 = (hi != lo && hi - lo > -1);       // hi > lo
      bool up2 = (r.depth[(size_t)r.stepIdx + 2] - hi) > 0;
      turn = (up1 != up2) ? 1 : 0;
    }
    int mode = r.dir ? 9 + 2 * turn : 8 + 2 * turn;
    r.dir = turn != 0;
    from = lo;
    to = hi;
    dur = r.stepMs;
    r.stepIdx++;
    return mode;
  }
  from = curDepth;
  to = 0;
  dur = (r.ticks + 1) * 200;
  r.lastDone = true;
  return r.dir ? 9 : 8;
}

// Outcome at ticks == stop (state 0xd end). See docs/sim-controller.md §2.3.
enum class FightEnd { Stall, Normal, Escape, Catch };
inline FightEnd DrillEnd(DrillRun& r, bool stepFired, bool inputSettled) {
  if (!stepFired || !inputSettled) { r.ticks = 1; return FightEnd::Stall; }
  if (r.good) { r.good = false; return FightEnd::Catch; }
  if (r.bad) return FightEnd::Escape;
  return FightEnd::Normal;
}

// --- controller states -------------------------------------------------
enum State : int {
  SInit = -1,
  SIdle = 1,
  SLineOut = 2,
  SWalk1 = 4,
  SWalk2 = 5,
  SDrillHold = 6,
  SDrillPrep = 7,
  SStandUp = 8,
  SRetractShort = 9,
  SRetract = 10,
  SLineEnd = 0xb,
  SFightPrep = 0xc,
  SFight = 0xd,
  SLanded = 0xe,
  SLineLift = 0xf,
  SRetractCatch = 0x10,
  SLineOut2 = 0x11,
  SDepthEase = 0x12,
  SDepthEase2 = 0x13,
  SWaiting = 0x14,
  SBite = 0x15,
  SStrike = 0x16,
  SStrike2 = 0x17,
  SLanded2 = 0x18,
  SAtHoleWait = 0x19,
  SSettle2 = 0x1a,
  SSettle3 = 0x1b,
  SHudDrag = 0x1c,
  SAnalog = 0x1d,
  SAtHole = 0x1e,
  SCatchHold = 0x20,
  SCatchHold2 = 0x21,
  SVerdict = 0x22,
  SAutoFight = 0x23,
};

// Action bits (004876d0 return value).
enum Action : unsigned {
  APrimary = 1,
  ASecondary = 2,       // returned only combined as 3 (space / cmd 5/6)
  ARelease = 4,
  ACmd2 = 8,
  ACmd3 = 0x10,
  ACmd4 = 0x20,
  AHudL = 0x40,
  AHudR = 0x80,
  ACmd9 = 0x100,
  ACmd8 = 0x200,
  AHold = 0x400,
  AHoldLong = 0x800,
  AAutoBite = 0x1000,
  ACmd1 = 0x2000,
  ASerial = 0x4000,
};

// Host-provided inputs + effect sinks. Defaults reproduce observed SP play.
struct Session {
  // state words (session+0x3b8/+0x3bc/+0x3c0)
  int state = SInit, prev = SInit, pending = SInit;

  // line depth fields
  int depth = 0;        // +0x414
  int target = 0;       // +0x418
  int bucket = 0;       // +0x41c
  int holeDepth = 0;    // +0x420

  // per-frame inputs (host sets before Step)
  int ptrCmd = 0;       // 00490060(): 0..4
  int rodCmd = 0;       // 00432920(): 0..4
  int players = 1;      // 004487e0 chain
  int lureType = 30;    // 0040cc90()
  bool canDrill = false;// 0048ace0()
  bool drillOk = true;  // 004872e0/0048ffd0/0048ff70 composite
  bool lureBusy = false;// 0058f3b0()
  bool inputSettled = true; // 0048ff90()
  int verdict = 0;      // +0x33c (0/1 landed, 2 catch, else escape)

  // fight record fields (host fills from the bite record):
  // double +0x2f8, id +0x2f0, p +0x310 / +0x308, 00590c10(+0x2e8) weight
  double fishW = 0;
  int fishType = 0;
  double pGood = 0, pBad = 0;
  int recWeight = 0;

  // effects observed this step (host drains)
  int hudEvent = -1;    // 0049b9c0 id, -1 = none
  int hudMs = 0;
  ModeChain mode;

  // timers (named after the original's gen0..gen4 / line_x / line_y)
  Timer gen[5];
  Timer lineX, lineY, tick200, stepT;

  DrillRun fight;       // 0xd working set (globals 7bcc90.. + +0x5b8)
  int retractTicks = 0; // 7bcc78 (state 10) / 7bcc74 (state 0x10)

  // --- action dispatcher (00489220) ------------------------------------
  // Returns 1 when it consumed the action (and queued a state/timers).
  int Dispatch(unsigned bits) {
    if (bits & AAutoBite) {
      pending = SAutoFight;
      gen[0].Start(0, 0, 800, nullptr, 9);
      gen[1].Start(0, 0, 800, nullptr, 9);
      gen[2].Start(0, 0, 700, nullptr, 9);
      mode.mode = MLineStage;   // 00477880: mode -> 0x25
      return 1;
    }
    if (bits & (APrimary | ARelease)) {
      if (canDrill) {
        pending = SFightPrep;
        gen[0].Start(0, 0, 800, nullptr, 9);
        gen[1].Start(0, 0, 800, nullptr, 9);
        gen[2].Start(0, 0, 700, nullptr, 9);
        gen[3].Start(1000, 0, 800, nullptr, 9);
        mode.mode = MLineStage; // 00477880
        return 1;
      }
      if ((bits & APrimary) == 0) return 0;
      pending = SRetractShort;
      gen[0].Start(0, 0, 800, nullptr, 9);
      gen[1].Start(0, 0, 800, nullptr, 9);
      gen[2].Start(0, 0, 700, nullptr, 9);
      mode.mode = MLineStage;   // 00477880
      return 1;
    }
    return 0;
  }

  // --- main step (0048af00) -------------------------------------------
  void Step(unsigned bits, int dt, MsvcRand& rng) {
    hudEvent = -1;
    for (Timer* t : {&gen[0], &gen[1], &gen[2], &gen[3], &gen[4],
                     &lineX, &lineY, &tick200, &stepT})
      t->Tick(dt);

    if (state == SInit) {
      pending = SIdle;                                  // case -1
    } else switch (state) {
    case SIdle: {                                       // case 1
      if (pending == SLineOut) break;
      if (bits & ASecondary) {
        if (drillOk) {
          gen[0].Start(0, 1, 800, nullptr, 0xb);
          gen[1].Start(0, 1, 800, nullptr, 0xb);
          gen[2].Start(0, 0, 600, nullptr, 8);
          gen[3].Start(150, 0, 600, nullptr, 9);
          gen[4].Start(0, 100, 500, nullptr, 8);
          pending = SDrillPrep;
          mode.BoreBegin();                             // 00477310
        }
      }
      if (bits & ACmd3) pending = SStrike;
      if ((bits & ACmd2) && lureBusy) { hudEvent = 0; hudMs = 7000; pending = SBite; }
      else if (bits & ACmd2) pending = SWaiting;
      if (bits & ACmd4) pending = SAtHoleWait;
      break;
    }
    case SLineOut:                                      // case 2
      if (prev == SIdle || Dispatch(bits) == 0) {
        if (ptrCmd == 1 || ptrCmd == 3) pending = SWalk1;
        if (ptrCmd == 2 || ptrCmd == 4) pending = SLineOut2;
        if (rodCmd == 1) {
          int nb = ReelIn(holeDepth, bucket);
          if (nb != bucket) { target = BucketDepth(holeDepth, nb); bucket = nb;
                              pending = SDepthEase; }
        } else if (rodCmd == 2) {
          int nb = LetOut(holeDepth, bucket);
          if (nb != bucket) { target = BucketDepth(holeDepth, nb); bucket = nb;
                              pending = SDepthEase; }
        } else if (rodCmd == 3 || rodCmd == 4) {
          pending = SDepthEase2;
        }
      }
      break;
    case SWalk1:                                        // case 4 (walk)
      if (Dispatch(bits) == 0 && (ptrCmd == 0 || ptrCmd == 4)) {
        int ms = WalkMs(players, false);
        gen[0].Start(0, 0, ms, nullptr, 0xb);
        gen[1].Start(0, 0, ms, nullptr, 0xb);
        gen[2].Start(0, 0, ms, nullptr, 0xb);
        pending = SWalk2;
      }
      break;
    case SWalk2:                                        // case 5
      if (Dispatch(bits) == 0) {
        if (gen[0].Fired() && gen[1].Fired() && gen[2].Fired() && gen[3].Fired()) {
          pending = SLineOut;
        } else if (ptrCmd == 1) {
          pending = SWalk1;
        }
      }
      break;
    case SDrillHold:                                    // case 6
      if (bits & APrimary) {
        pending = SRetract;
        mode.BoreBegin();
        break;
      }
      if (gen[0].Fired()) {
        pending = SStandUp;
      } else if (rodCmd != 0 && depth > 0x32) {
        target = depth + 0x14;                          // +0x418 = +0x414+20
        bucket = Bucket(holeDepth, target);             // 004ac620
        pending = SStandUp;
      }
      break;
    case SDrillPrep:                                    // case 7
      // ponytail: collapses 7's two-stage sway timers into one gen4 wait;
      // restore the gen2/gen3 (-55/+15, 600/420 ms, mode 0x1d) chain if the
      // visuals need it. Timings kept: gen4 0->100 (500 ms), gen0 ramp below.
      if (gen[4].Fired()) {
        // 004adad0/004c1370 hole lookup/create happened here
        int diff = target - depth >= 0 ? target - depth : depth - target;
        gen[0].Start(0, target - 0x14, DepthEaseMs(diff, players, lureType),
                     nullptr, 0xc);
        pending = SDrillHold;
      }
      break;
    case SStandUp:                                      // case 8
      if (gen[0].Fired()) {
        int ms = WalkMs(players, true);
        gen[0].Start(0, 0, ms, nullptr, 0xb);
        gen[1].Start(0, 0, ms, nullptr, 0xb);
        gen[2].Start(0, 0, ms, nullptr, 0xb);
        gen[3].Start(0, 0, ms, nullptr, 0xb);
        pending = SWalk2;
      }
      break;
    case SRetractShort:                                 // case 9
      if (gen[0].Fired()) pending = SRetract;
      break;
    case SRetract: {                                    // case 10
      if (bits & ASecondary) {
        target = depth + 0x14;
        bucket = Bucket(holeDepth, target);
        mode.BoreBegin();
        pending = SDrillHold;
        break;
      }
      if (retractTicks == 0) {
        retractTicks = (int)((double)depth * 0.04 + 3.0);
        gen[3].Start(depth, 0, (retractTicks - 1) * 200, nullptr, 8);
      }
      if (gen[2].Fired() && retractTicks > 0) {
        retractTicks--;
        if (retractTicks == 0) { depth = 0; pending = SLineEnd; }
      }
      break;
    }
    case SLineEnd:                                      // case 0xb
      if (gen[2].Fired()) {
        pending = lureBusy ? SAtHole : SIdle;           // 00488060: 0x1e / 1
        mode.BoreEnd(false);                            // 00477220
      }
      break;
    case SFightPrep:                                    // case 0xc
      if (gen[0].Fired() && gen[1].Fired() && gen[2].Fired() && gen[3].Fired())
        pending = SFight;
      break;
    case SFight: StepFight(bits, rng); break;           // case 0xd (§2)
    case SLanded:                                       // case 0xe
      if (gen[0].Fired()) {
        pending = SCatchHold;
        mode.BoreEnd(false);
      }
      break;
    case SLineLift:                                     // case 0xf
      if (lineX.Fired()) pending = lureBusy ? SAtHole : SIdle;
      break;
    case SRetractCatch:                                 // case 0x10
      if (retractTicks == 0) {
        retractTicks = (int)((double)depth * 0.028);
        if (retractTicks == 0) retractTicks = 1;
        gen[3].Start(depth, 0, (retractTicks - 1) * 200, nullptr, 8);
      }
      if (gen[2].Fired() && retractTicks > 0) {
        retractTicks--;
        if (retractTicks == 0) { depth = 0; pending = SLineLift; }
      }
      break;
    case SLineOut2:                                     // case 0x11
      if (ptrCmd == 0 || ptrCmd == 3) pending = SLineOut;
      break;
    case SDepthEase:                                    // case 0x12
      if (rodCmd == 1 || rodCmd == 2 || rodCmd == 3 || rodCmd == 4)
        pending = SLineOut;
      break;
    case SDepthEase2:                                   // case 0x13
      if (gen[2].Fired()) pending = SLineOut;
      break;
    case SAutoFight:                                    // case 0x23
      if (gen[0].Fired()) pending = SRetractCatch;
      break;
    case SVerdict:                                      // case 0x22
      if (gen[0].Fired()) {
        if (verdict == 1) pending = SLanded;
        else if (verdict == 2) { hudEvent = 0x5a7; pending = SRetractCatch; }
        else pending = SRetract;
      }
      break;
    default:
      // 0x14..0x1b / 0x1c / 0x1d / 0x1e / 0x19: input/net driven shells whose
      // bodies are HUD and net calls; keep their verified bit edges.
      if (state == SWaiting || state == SStrike || state == SAtHoleWait) {
        if (bits & (ACmd1 | ASecondary)) pending = lureBusy ? SAtHole : SIdle;
        if (bits & ACmd3) pending = SStrike;
        if (bits & ACmd4) pending = SAtHoleWait;
      }
      break;
    }

    // epilogue (0048af00 tail): prev <- state, state <- pending
    if (state != 0) {
      prev = state;
      state = pending;
    }
  }

 private:
  // case 0xd — the fight machine (docs/sim-controller.md §2). Mirrors the
  // original frame: init block falls through into the tick/step/end blocks.
  void StepFight(unsigned bits, MsvcRand& rng) {
    bool restart = (bits & AHoldLong) != 0;   // forceRise fired (0x800)
    if (prev != state || restart) {
      DrillParams p;
      p.depth0 = depth;
      p.maxDepth = holeDepth;
      p.fishW = fishW;
      p.fishType = fishType;
      p.pGood = pGood;
      p.pBad = pBad;
      p.recWeight = recWeight;
      p.forced = restart;
      fight = InitDrill(p, rng);
    }
    // tick timer (gen2 wobble, slot +0x228): missing handle reads as fired
    if (tick200.Fired()) {
      tick200.Start(0, 0, 200, nullptr, 8);
      fight.ticks--;
      if (fight.ticks < 0) fight.ticks = 0;
      if (fight.ticks < fight.stop) fight.ticks = fight.stop;
    }
    // step timer (gen3, slot +0x22c): one eased segment per stepMs
    if (stepT.Fired() && !fight.lastDone) {
      int from, to, dur;
      int m = DrillStepAdvance(fight, depth, from, to, dur);
      stepT.Start(from, to, dur, nullptr, m);
    }
    if (fight.ticks == fight.stop) {
      tick200.Kill();      // 0048af00: 00651410(+0x228) at the top of the end block
      switch (DrillEnd(fight, stepT.Fired(), inputSettled)) {
        case FightEnd::Stall: break;
        case FightEnd::Normal:
          depth = 0;
          pending = (verdict == 0) ? SLanded : SVerdict;
          break;
        case FightEnd::Escape:
          pending = SRetract;
          break;
        case FightEnd::Catch:
          hudEvent = 0x557;
          pending = SRetractCatch;
          break;
      }
      if (pending != SFight) { stepT.Kill(); fight.depth.clear(); }
    }
    // two ticks out: line_x/line_y 500 ms sway (visual only, skipped)
  }
};

}  // namespace ctrl
