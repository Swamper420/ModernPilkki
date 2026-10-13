// botai.hpp — Propilkki 2 opponent AI: shared action queue + AI_STATE_*
// machines, clean-room port. Behavior documented in docs/sim-ai.md; each
// block cites the image VA it comes from. Deterministic given an RNG seed.
#pragma once
#include <cstdint>
#include <deque>

namespace botai {

// MSVC rand — sim.md §4.7. 00676d2c: s = s*214013 + 2531011, 15-bit.
// 0059db20(n): two draws, u = (r2 << 15) | r1 (30-bit), value
// (int)(u * 2^-30 * n), clamp n-1 (00777a20 = 0.0 addend, verified).
struct MsvcRand {
  uint32_t s = 1;
  int Raw() {
    s = s * 214013u + 2531011u;
    return (int)((s >> 16) & 0x7fff);
  }
  int Below(int n) {
    if (n <= 0) return 0;
    int r1 = Raw();
    int r2 = Raw();
    uint32_t u = (uint32_t)(((r2 & 0x7fff) << 15) | (r1 & 0x7fff));
    int v = (int)((double)u * 9.313225746154785e-10 * (double)n);
    return v == n ? n - 1 : v;
  }
};

// Action record: 0x14 bytes = id + 4 payload dwords (0062ffe0/00630040).
struct Action {
  int id = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0;
};
static_assert(sizeof(Action) == 20, "original action record is 0x14 bytes");

// Event ids (queue records). Producers/consumers: docs/sim-ai.md §1.2.
enum : int {
  kEvRouteRelease = 0,   // p4 = route object to release
  kEvRouteAck = 1,       // route rejected/short (net reply)
  kEvWalkOrder = 2,      // p1=x p2=y; also "mode 0x18 entered" from the pump
  kEvStopped = 3,        // stopped at hole / mode 5 entered
  kEvAct = 4,            // main verb: arrival / take-up timer step
  kEvRoute = 5,          // p1=x p2=y p3=bearing p4=route; also AI_bore_timer2
  kEvEiSyo = 6,          // AI_ei_syo_short fired
  kEvNoFishLong = 7,     // AI_no_fish_long fired
  kEvStrikeMiss = 8,     // bite happened, no strike
  kEvStrike = 9,         // strike / take up
  kEvRouteOk = 10,       // route accepted
  kEvRouteShort = 0xb,   // route retry hint
  kEvCancel = 0xc,       // movement cancelled
  kEvAbort = 0xd,        // can't make it home before round end
  kEvClickOrder = 0xe,   // click-to-move order / special flag in take-up
  kEvMode35 = 0xf,       // mode 0x35 entered
};

// "Go to state:" ids (00630790). 2/4/5/8 = the four machines (verified by
// edge semantics, docs/sim-ai.md §1.3); others park in the base consumer.
enum : int {
  kStWalking = 2,
  kStDrilling = 4,
  kStFishing = 5,
  kStTakeUp = 8,
  kStGiveUp = 10,
};

// AI timer slots (human +0x624..+0x634 in the original).
enum {
  kTmrNoFishLong = 0,  // +0x624
  kTmrEiSyo = 1,       // +0x628 (AI_ei_syo_short / AI_no_fish_short)
  kTmrBore = 2,        // +0x62c (AI_bore_timer2 / AI_bore / AI nosto)
  kTmrKorjaus = 3,     // +0x630 (AI_korjaus / AI nosto / AI sinking / AI_Temp)
  kTmrSyoko = 4,       // +0x634 (AI_syoko_timer)
  kNumTimers = 5
};

// Timer formulas (docs/sim-ai.md §3). Duplicates of offline.hpp A*Ms on
// purpose — orchestrator dedupes at integration.
inline int AIEiSyoMs(int r, int skill) {  // 00477e20/004728b0
  if (skill < 0) skill = 0;
  if (skill > 10) skill = 10;
  return ((r % (11 - skill)) - skill + 20) * 1000;
}
inline int AINoFishShortMs(int r, int skill) { return AIEiSyoMs(r, skill); }
inline int AINoFishLongMs(int r, bool lake) {  // 00477e20/004728b0
  return lake ? (r % 10000) + 60000 : (r % 5000) + 35000;
}
inline int AIBoreMs(int r) { return (r % 100 + 20) * 100; }  // 00477e20
inline int AIKorjausMs(int skill) { return (14 - skill) * 1000; }  // 00478210
inline int AISyokoMs() { return 1000; }    // 004710e0
inline int AITempMs() { return 500; }      // 00478b10 case 3
inline int AISinkingMs() { return 3000; }  // 00478210 phase 1
inline int AINostoMs() { return 5000; }    // 00472a40 (take-up entry)

// Bite record (human +0x6c8 region). p_catch +0x6f0, p_fail +0x6e8
// (depth bands 0.7/0.1, 0.9/0.4, 0.95/0.5 — sim.md §4.4.7), type +0x6d0,
// outcome +0x71c (2 = pre-resolved success, 3 = pre-resolved escape),
// weight from the obfuscated field 00590c10 (XOR 0x0E5FAF5F, grams).
struct Bite {
  int type = 0;
  int outcome = 0;
  double pCatch = 0.0;
  double pFail = 0.0;
  int weight = 0;
};

struct Bot;

// Gameplay primitives + world queries the AI invokes. Same seam the player
// controller port (0048af00) should use; defaults are inert so tests wire
// only what they need.
struct Hooks {
  virtual ~Hooks() = default;
  // primitives (VAs = originals)
  virtual int DrillStart() { return 4; }                 // 004764b0: 1 ok, 4 hole here
  virtual void BoreBegin() {}                            // 00477310
  virtual void BoreEnd() {}                              // 00477220
  virtual void LineStage() {}                            // 00477880
  virtual void Jig() {}                                  // 004771d0 (mode 0x24)
  virtual int LandFish(Bot &, const Bite &) { return 0; }  // 00473320 → >0 = landed
  virtual void FishSuccess(Bot &, const Bite &) {}         // 00473280
  virtual void FishEscaped(Bot &, const Bite &) {}         // 004732d0
  virtual bool BiteRoll(Bot &, Bite &) { return false; }   // 004ba5e0 + validators
  // world / round queries
  virtual bool ShallowAtDrillPoint() { return false; }  // 00472480: depth < 30
  virtual bool DrillAreaOk() { return true; }           // +0x52c/00439900/00437a40/00439a80
  virtual int TravelCost() { return 0; }                // 00472cf0: cost home
  virtual int ClockSeconds() { return 0; }              // 004383c0: round clock
  virtual bool ModeIs(int) { return false; }            // 00464470/00476270
  virtual bool NextWaypoint() { return false; }         // 00476040
  virtual bool LakeModel() { return false; }            // +0x528 && 004aeeb0
  virtual bool LakeEdge() { return false; }             // 004b15c0
  virtual void HudEvent(int) {}                         // 004993c0 (bot-chat gate inside)
  virtual int ActivitySpan() { return 100; }            // 0059fad0
  virtual int ActivityQuality(int) { return 0; }        // 0059fbe0
};

// Human/AI context (the fields the four machines touch; offsets = original
// session object). One Bot per opponent.
struct Bot {
  int skill = 5;                 // +0x6a0, 0..10
  int role = 0;                  // +0x170 (1/2 checked in WALKING ev 1)
  int team = 0;                  // +0x168 (== 3 branches)
  int miss = 0;                  // +0x350 consecutive-miss counter
  int count = 0, total = 0, biggest = 0;  // +0x354/+0x358/+0x35c
  int boredom = 0;               // +0x5f0 (00472780)
  int abortLatch = 0;            // +0x620
  int footFlag = 0;              // +0x6c0
  int clock = 0;                 // ms since start (DAT_007c695c stand-in)
  int latch18 = 0, latch5 = 0, latch35 = 0;  // +0x5fc/+0x600/+0x604 one-shots
  int edgeAt = 0, edgeDelay = 0;             // +0x60c/+0x610 lake-edge delay
  Bite bite;                     // +0x6c8 region
  MsvcRand rng;
  std::deque<Action> queue;      // state+0x50 action queue
  int state = kStWalking;        // current state id (00630790 table)
  int pending = 0;               // +0x44 requested next state
  bool hasPending = false;       // +0x30 == 3
  // per-state scratch (state object +8/+0xc/+0x10); reset on transition.
  int s8 = 0, sC = 0, s10 = 0;
  struct Tmr {
    int left = 0;  // 0 = off, -1 = fired (00651220), > 0 counting down
    int span = 0;  // last armed ms (for 006510d0 elapsed)
  };
  Tmr tmr[kNumTimers];
};

// --- queue + timers -----------------------------------------------------

inline void Post(Bot &b, const Action &a) { b.queue.push_back(a); }  // 006303c0
inline void Post(Bot &b, int id) { Post(b, Action{id, 0, 0, 0, 0}); }  // 0062ffe0

inline void Arm(Bot &b, int slot, int ms) {  // 004715e0: kill + arm
  b.tmr[slot].span = ms;
  b.tmr[slot].left = ms > 0 ? ms : -1;
}
inline void KillTimer(Bot &b, int slot) { b.tmr[slot] = Bot::Tmr{}; }  // 00471620
inline bool Fired(Bot &b, int slot) { return b.tmr[slot].left < 0; }   // 00471510
inline void Consume(Bot &b, int slot) { KillTimer(b, slot); }          // 00651410
inline int Elapsed(const Bot &b, int slot) {  // 006510d0
  return b.tmr[slot].span - (b.tmr[slot].left < 0 ? 0 : b.tmr[slot].left);
}
inline void Advance(Bot &b, int dtMs) {
  for (int i = 0; i < kNumTimers; i++) {
    int &t = b.tmr[i].left;
    if (t > 0) {
      t -= dtMs;
      if (t <= 0) t = -1;
    }
  }
}

inline void DecBoredom(Bot &b) {  // 00472780
  if (--b.boredom < 0) b.boredom = 0;
}

// Deferred state swap — 00630790 only records the request (+0x44, +0x30=3);
// the owner swaps after the handler body finished.
inline void GoTo(Bot &b, int id) {
  b.pending = id;
  b.hasPending = true;
}

// 00472a40: take-up entry (wiring *(inferred)*, body verified).
inline void EnterTakeUp(Bot &b, Hooks &h) {
  Arm(b, kTmrKorjaus, AINostoMs());
  h.LineStage();  // 00477880
  b.s8 = b.sC = b.s10 = 0;
}

inline void ApplyPending(Bot &b, Hooks &h) {
  if (!b.hasPending) return;
  b.hasPending = false;
  b.state = b.pending;
  b.s8 = b.sC = b.s10 = 0;  // fresh state object fields
  if (b.state == kStTakeUp) EnterTakeUp(b, h);
}

// --- state machines -----------------------------------------------------

// 00478b10 AI_STATE_WALKING. Scratch: s8 = blocked.
inline void HandleWalking(Bot &b, Hooks &h, const Action &a) {
  switch (a.id) {
    case kEvRouteAck:  // 1
      if ((b.role != 1 && b.role != 2) || !h.ShallowAtDrillPoint()) {
        b.s8 = 1;
        return;
      }
      GoTo(b, 0xe);  // unported state id
      return;
    case kEvStopped:  // 3
      if (b.s8 == 1) Arm(b, kTmrKorjaus, AITempMs());  // "AI_Temp" at +0x630
      return;
    case kEvAct: {  // 4 — arrival / take-up timer step
      if (h.ClockSeconds() - 25 < h.TravelCost()) {  // 00472cf0 vs 004383c0
        GoTo(b, kStGiveUp);
        return;
      }
      bool shallow = h.ShallowAtDrillPoint();  // 00472480
      bool areaOk = h.DrillAreaOk();           // +0x52c/00439900/00437a40/00439a80
      if (!shallow && areaOk && h.DrillStart() == 1) {  // 004764b0
        GoTo(b, kStDrilling);
        return;
      }
      if (b.team != 0 && h.NextWaypoint()) {  // 00476040
        GoTo(b, 0);
        return;
      }
      DecBoredom(b);  // 00472780
      GoTo(b, kStWalking);
      return;
    }
    case kEvRoute:  // 5
      if (!h.ModeIs(5)) return;  // 00476270
      // fall through
    case kEvClickOrder:  // 0xe
      GoTo(b, kStWalking);
      return;
    case kEvCancel:  // 0xc
      GoTo(b, 9);
      return;
    case kEvAbort:  // 0xd
      KillTimer(b, kTmrKorjaus);  // 00471620(+0x630)
      GoTo(b, kStGiveUp);
      return;
    default:
      return;  // base consumer: just free (006307c0)
  }
}

// 00477b90 AI_STATE_DRILLING. Scratch: s8 = abort, sC = bore seen.
inline void HandleDrilling(Bot &b, Hooks &h, const Action &a) {
  switch (a.id) {
    case kEvWalkOrder:  // 2 — mode 0x18 entered (bore began)
      h.BoreBegin();    // 00477310
      if (b.s8) {
        GoTo(b, kStGiveUp);
        return;
      }
      if (!b.sC) {
        h.HudEvent(10);
        GoTo(b, kStFishing);
        return;
      }
      b.footFlag = 0;
      GoTo(b, kStWalking);
      return;
    case kEvStopped:  // 3 — mode 5 entered (standing ready)
      if (b.sC) {
        b.footFlag = 0;
        GoTo(b, b.s8 ? kStGiveUp : kStWalking);
      }
      return;
    case kEvAbort:  // 0xd
      b.s8 = 1;
      return;
    case kEvClickOrder:  // 0xe
      b.footFlag = 1;
      b.sC = 1;
      return;
    default:
      return;
  }
}

// 00477e20 AI_STATE_FISHING. Timers: kill/re-arm sets per the table in
// docs/sim-ai.md §4.3.
inline void HandleFishing(Bot &b, Hooks &h, const Action &a) {
  switch (a.id) {
    case kEvRoute:   // 5 — AI_bore_timer2 fired
    case kEvEiSyo:   // 6 — AI_ei_syo_short fired
      h.Jig();       // 004771d0
      Arm(b, kTmrEiSyo, AIEiSyoMs(b.rng.Raw(), b.skill));
      return;
    case kEvNoFishLong: {  // 7
      for (int i = 0; i < kNumTimers; i++)
        if (i != kTmrKorjaus) KillTimer(b, i);
      if (b.team != 3) {
        h.HudEvent(6);
        GoTo(b, kStWalking);
        return;
      }
      h.Jig();
      Arm(b, kTmrEiSyo, AIEiSyoMs(b.rng.Raw(), b.skill));
      Arm(b, kTmrNoFishLong,
          AINoFishLongMs(b.rng.Below(10000), h.LakeModel()));
      Arm(b, kTmrSyoko, AISyokoMs());
      Arm(b, kTmrBore, AIBoreMs(b.rng.Raw()));
      h.HudEvent(5);
      return;
    }
    case kEvStrikeMiss:  // 8 — bite happened, no strike
      if (Elapsed(b, kTmrNoFishLong) > 29999) h.HudEvent(4);
      Arm(b, kTmrEiSyo, AIEiSyoMs(b.rng.Raw(), b.skill));
      Arm(b, kTmrNoFishLong, AINoFishLongMs(b.rng.Below(10000), h.LakeModel()));
      Arm(b, kTmrSyoko, AISyokoMs());
      Arm(b, kTmrBore, AIBoreMs(b.rng.Raw()));
      h.Jig();
      return;
    case kEvStrike:  // 9 — strike!
      for (int i = 0; i < kNumTimers; i++)
        if (i != kTmrKorjaus) KillTimer(b, i);
      GoTo(b, kStTakeUp);
      return;
    case kEvRouteOk:      // 10
    case kEvRouteShort:   // 0xb
    case kEvCancel:       // 0xc
      return;
    case kEvAbort:  // 0xd
      for (int i = 0; i < kNumTimers; i++)
        if (i != kTmrKorjaus) KillTimer(b, i);
      GoTo(b, kStGiveUp);
      return;
    case kEvClickOrder:  // 0xe
      for (int i = 0; i < kNumTimers; i++)
        if (i != kTmrKorjaus) KillTimer(b, i);
      GoTo(b, kStWalking);
      // falls into the ev-8 tail in the original
      Arm(b, kTmrEiSyo, AIEiSyoMs(b.rng.Raw(), b.skill));
      Arm(b, kTmrNoFishLong, AINoFishLongMs(b.rng.Below(10000), h.LakeModel()));
      Arm(b, kTmrSyoko, AISyokoMs());
      Arm(b, kTmrBore, AIBoreMs(b.rng.Raw()));
      h.Jig();
      return;
    default:
      return;
  }
}

// 00478210 stats block (w > 0): count/total/max + the 5-catch review
// (max < 50 or avg < 30 → give up). Extracted so tests can drive it
// directly; the handler calls it on every land.
inline void ApplyLand(Bot &b, Hooks &h, int w) {
  if (w <= 0) return;
  b.count++;
  b.total += b.bite.weight;  // 00590c10
  if (b.biggest <= b.bite.weight) b.biggest = b.bite.weight;
  if (b.count == 5) {
    if (b.biggest < 50 || b.total / 5 < 30) {  // give up
      b.miss = 0;
      if (b.team == 3) {
        h.HudEvent(0xf);
      } else {
        h.HudEvent(0x10);
        GoTo(b, kStWalking);
      }
      b.count = 0;
      b.total = 0;
    } else {  // good enough — reset counters
      b.count = 0;
      b.total = 0;
    }
    b.biggest = 0;
  }
  DecBoredom(b);  // 00472780
  if (b.bite.weight > 100) b.boredom = 0;
}

// 00478210 AI_STATE_FISH_TAKE_UP. Scratch: s8 = phase 0/1/2, sC = abort
// (ev 0xd), s10 = special (ev 0xe).
inline void HandleTakeUp(Bot &b, Hooks &h, const Action &a) {
  if (a.id != kEvAct) {
    if (a.id == kEvAbort) {  // 0xd
      b.sC = 1;
      return;
    }
    if (a.id == kEvClickOrder) {  // 0xe
      if (b.s8 == 0) h.HudEvent(0x14);
      b.s10 = 1;
      return;
    }
    return;  // consume
  }
  if (b.s8 == 0) {
    // Phase 0 — fight resolution (docs/sim-ai.md §4.4).
    int w = 0;
    if (b.bite.outcome == 2) {
      h.FishSuccess(b, b.bite);  // 00473280
      b.miss = 0;
    } else if (b.bite.outcome == 3) {
      h.FishEscaped(b, b.bite);  // 004732d0
      b.miss = 0;
    } else {
      bool caught = false, escaped = false;
      if (b.bite.type == 3) {
        // Both draws always happen (RNG order matters).
        caught = b.rng.Below(10000) < (int)(b.bite.pCatch * 10000.0);
        escaped = b.rng.Below(10000) < (int)(b.bite.pFail * 10000.0);
      }
      if (caught) {
        h.FishSuccess(b, b.bite);
        b.miss = 0;
      } else if (escaped) {
        h.FishEscaped(b, b.bite);
        b.miss = 0;
      } else {
        w = h.LandFish(b, b.bite);  // 00473320
      }
    }
    if (!b.sC) {
      if (!b.s10) {
        if (w != 0) {
          b.miss = 0;
          ApplyLand(b, h, w);
        } else if (b.miss < 1) {
          b.miss += 1;
        } else {
          b.miss = 0;
          if (b.team == 3) {
            h.HudEvent(0xd);
          } else {
            h.HudEvent(0xe);
            GoTo(b, kStWalking);
          }
        }
      } else {
        b.miss = 0;
        GoTo(b, kStWalking);
        ApplyLand(b, h, w);
      }
      b.s8 = 1;  // phase 1
      Arm(b, kTmrKorjaus, AIKorjausMs(b.skill));
      h.BoreEnd();  // 00477220
      return;
    }
    GoTo(b, kStGiveUp);
    return;
  }
  if (b.s8 == 1) {
    // Phase 1 — korjaus elapsed.
    if (!b.s10) {
      if (!b.sC) {
        Arm(b, kTmrKorjaus, AISinkingMs());  // "AI sinking" at +0x630
        b.s8 = 2;
        h.BoreBegin();  // 00477310
        return;
      }
      GoTo(b, kStGiveUp);
      return;
    }
    b.miss = 0;
    GoTo(b, kStWalking);
    return;
  }
  // Phase 2 — sinking elapsed.
  if (b.s10) {
    b.miss = 0;
    GoTo(b, kStWalking);
    return;
  }
  if (b.sC) {
    GoTo(b, kStGiveUp);
    return;
  }
  b.s8 = 0;
  GoTo(b, kStFishing);
}

// --- pump + driver ------------------------------------------------------

// 004710e0: AI timer/edge pump. Posts at most one timer event (priority
// order verified), then the mode-edge block. The bite/syoko block rolls
// the strike decision (skill + 1 quality draws vs rand % 100).
inline void Pump(Bot &b, Hooks &h) {
  if (Fired(b, kTmrNoFishLong)) {
    Consume(b, kTmrNoFishLong);
    Post(b, kEvNoFishLong);
  } else if (Fired(b, kTmrEiSyo)) {
    Consume(b, kTmrEiSyo);
    Post(b, kEvEiSyo);
  } else if (Fired(b, kTmrBore)) {
    Consume(b, kTmrBore);
    Post(b, kEvRoute);
  } else if (Fired(b, kTmrKorjaus)) {
    Consume(b, kTmrKorjaus);
    Post(b, kEvAct);
  } else if (!b.abortLatch && h.ClockSeconds() < h.TravelCost()) {
    b.abortLatch = 1;  // +0x620
    Post(b, kEvAbort);
  } else if (Fired(b, kTmrSyoko)) {
    Consume(b, kTmrSyoko);
    b.bite = Bite{};
    if (!h.BiteRoll(b, b.bite)) {
      Arm(b, kTmrSyoko, AISyokoMs());  // "AI syoko(human.cpp)"
    } else {
      int span = h.ActivitySpan();  // 0059fad0
      int best = 0;
      for (int i = 0; i < b.skill + 1; i++) {
        int q = h.ActivityQuality(span > 0 ? b.rng.Raw() % span : 0);  // 0059fbe0
        if (best < q) best = q;
      }
      Post(b, (b.rng.Raw() % 100 < best) ? kEvStrike : kEvStrikeMiss);
    }
  }
  // Mode-edge block (always evaluated; one event per edge, latch per entry
  // — the original tests the zero flags 0047f740 refreshes each frame).
  if (h.ModeIs(0x18)) {
    if (!b.latch18) {
      b.latch18 = 1;
      Post(b, kEvWalkOrder);  // "bore mode entered" (no payload)
    }
  } else {
    b.latch18 = 0;
  }
  if (h.ModeIs(5)) {
    if (!b.latch5) {
      b.latch5 = 1;
      Post(b, kEvStopped);
    }
  } else {
    b.latch5 = 0;
  }
  if (h.ModeIs(0x35)) {
    if (!b.latch35) {
      b.latch35 = 1;
      Post(b, kEvMode35);
    }
  } else {
    b.latch35 = 0;
  }
  if (b.edgeAt == 0 && h.LakeEdge()) {
    b.edgeAt = b.clock;
    b.edgeDelay = b.rng.Below(100);  // +0x610 = rand(100)
  }
  if (b.edgeAt != 0 && b.clock - b.edgeAt > b.edgeDelay) {
    b.edgeAt = 0;
    Post(b, kEvRouteRelease);
  }
}

// One slice (vft[+8]): advance timers, post events, drain ONE action
// (006307c0), run the current machine, then apply the deferred swap
// (00630790 semantics). dtMs is one frame or one slicer slice (≤ 1000).
inline void Tick(Bot &b, Hooks &h, int dtMs) {
  b.clock += dtMs;
  Advance(b, dtMs);
  Pump(b, h);
  if (!b.queue.empty()) {
    Action a = b.queue.front();  // 00630740
    b.queue.pop_front();
    switch (b.state) {
      case kStWalking:
        HandleWalking(b, h, a);
        break;
      case kStDrilling:
        HandleDrilling(b, h, a);
        break;
      case kStFishing:
        HandleFishing(b, h, a);
        break;
      case kStTakeUp:
        HandleTakeUp(b, h, a);
        break;
      default:
        break;  // unported states: base handler consumes (006307c0)
    }
  }
  ApplyPending(b, h);
}

}  // namespace botai
