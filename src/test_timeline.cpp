// Asserts for src/timeline.hpp (docs/sim-timeline.md). Minimal.
#include <cassert>
#include <cstdio>

#include "timeline.hpp"

int main() {
  using namespace timeline;

  // Duration formula 00464510: (int)(len * speed). human2.3ds stage 9.
  const Script &s = HumanScript();
  Machine m(s);
  assert(s.count == 57);
  assert(m.Duration(9) == 1000);
  assert(m.Duration(24) == 200);   // JUST_SITTING
  assert(m.Duration(51) == 2000);  // POHJAAN_MENI_JATKA
  Script half;
  half.count = 1;
  half.m[0].len = 700;
  half.m[0].speed = 0.5f;
  half.m[0].next = 0;
  assert(Machine(half).Duration(0) == 350);

  // Next-table transition + hop gate: KAVELY1 (500 ms) -> KAVELY2.
  Machine walk(s);
  walk.cur = 0;
  walk.Update(499);
  assert(walk.Mode() == 0);
  walk.Update(1);
  assert(walk.Mode() == 1);

  // Pending override (00465d50): queued move wins over the next table,
  // and takes effect at the move-duration boundary.
  Machine p(s);
  p.cur = 0;
  p.Queue(5);
  assert(p.Pending() == 5);
  p.Update(499);
  assert(p.Mode() == 0);
  p.Update(1);
  assert(p.Mode() == 5);
  assert(p.Pending() == -1);
  assert(p.Queue(99) == -1);  // bounds check against moveCount

  // Jump (00464410): restart, applies on the next tick at elapsed 0.
  Machine j(s);
  j.cur = 0;
  j.Update(300);
  assert(j.Jump(24) == 0);
  assert(j.Mode() == -1);
  j.Update(50);
  assert(j.Mode() == 24);
  assert(j.Elapsed() == 0);

  // Elapsed clamp 004644d0: never returns past duration-1. PAIKALLAAN
  // (5) self-loops with chain flag set, so Update hops at most 5x.
  Machine c(s);
  c.cur = 5;
  c.Update(1000);
  assert(c.Mode() == 5);
  assert(c.Elapsed() < m.Duration(5));

  // Drill chain (00480820): 9 -> 10 -> 0xc -> 0xe -> 0x10 -> outcome,
  // one hop per hold window. holdOff == 2 is the plain drill-start path
  // (004764b0), where the hold gates every exit including the outcome.
  ChainInput in;
  in.holdOff = 2;
  Chain ch;
  assert(ch.Step(9, 0, 1200, in) == -1);  // stamps stage 1
  assert(ch.Step(9, 1199, 1200, in) == -1);
  assert(ch.Step(9, 1200, 1200, in) == 10);
  assert(ch.Step(10, 1200, 1200, in) == -1);
  assert(ch.Step(10, 2400, 1200, in) == 12);
  assert(ch.Step(12, 3600, 1200, in) == -1);
  assert(ch.Step(12, 4800, 1200, in) == 14);
  assert(ch.Step(14, 6000, 1200, in) == -1);
  assert(ch.Step(14, 7200, 1200, in) == 16);
  assert(ch.Step(16, 8400, 1200, in) == -1);  // stamps stage 9
  assert(ch.Step(16, 9600, 1200, in) == 0x12);  // LAPI_MENI
  ChainInput b;  // holdOff == 0: exit need not wait out the hold
  b.bottom = true;
  Chain ch2;
  assert(ch2.Step(16, 1200, 1200, b) == 0x33);  // POHJAAN_MENI_JATKA
  ChainInput a;
  a.alt = true;
  Chain ch3;
  assert(ch3.Step(16, 1200, 1200, a) == 0x31);  // LAPI_MENI_JATKA
  ChainInput early;  // holdOff != 2 exits without waiting out the hold
  Chain ch4;
  assert(ch4.Step(16, 0, 100000, early) == 0x12);

  // Hold formula 0047a1c0.
  assert(HoldMs(0) == 0);
  assert(HoldMs(1) == 1200);
  assert(HoldMs(100) == 100200);
  assert(HoldMs(101) == 100200);

  // Round-end mode sets (00476180 / 00476160 / 004761d0).
  assert(IsRoundEndMode(0x18) && IsRoundEndMode(0x24));
  assert(IsRoundEndMode(0x27) && IsRoundEndMode(0x28));
  assert(!IsRoundEndMode(5));
  assert(!IsRoundEndMode(0x25));
  assert(IsLiftMode(0x25) && IsLiftMode(0x26) && !IsLiftMode(0x27));
  assert(IsSitTransition(0x15) && IsSitTransition(0x16));
  assert(IsSitTransition(0x17) && !IsSitTransition(0x18));
  assert(kEndTimerMs == 2300 && kEndRampMs == 2000);

  std::puts("test_timeline ok");
  return 0;
}
