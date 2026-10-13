// test_controller.cpp — asserts for src/controller.hpp
// Covers: mode chain 5->6->0x13->0x18->0x27, drill progress math (0xd depth
// array + +0x414/+0x418 rule), one deterministic rand-roll outcome, walk
// timer duration formula. Must run with asserts enabled.
#include "controller.hpp"

#include <cassert>

using namespace ctrl;

int main() {
  // --- 1. game-mode chain 5 -> 6 -> 0x13 -> 0x18 -> 0x27 ----------------
  ModeChain m;
  assert(m.mode == 5);
  assert(m.DrillStart(true) == 1 && m.mode == 6);   // 004764b0
  assert(m.Next() == 0x13);                          // timeline next (inferred)
  m.HolePass();                                      // 00480820 hole creation
  assert(m.holeOpen);
  assert(m.Next() == 0x18);                          // timeline next (inferred)
  assert(m.BoreEnd(false) == 0x27 && m.mode == 0x27); // 00477220

  // 00477310 verified rule: 0x18 from {0x25,0x26,0x18}, else 0x29
  ModeChain b1; b1.mode = MLineStage;
  assert(b1.BoreBegin() == 0x18);
  ModeChain b2; b2.mode = MDrillStart;
  assert(b2.BoreBegin() == MBoreAlt);
  // 004764b0 guards: wrong mode -> 2, hole nearby -> 4
  ModeChain g1; g1.mode = MDrillStart;
  assert(g1.DrillStart(true) == 2);
  ModeChain g2;
  assert(g2.DrillStart(false) == 4 && g2.mode == 5);

  // --- 2. drill progress math ------------------------------------------
  DrillParams p;
  p.depth0 = 80; p.maxDepth = 300;
  p.fishW = 10.0; p.fishType = 1;
  p.pGood = 1.0; p.pBad = 0.0; p.recWeight = 400;
  MsvcRand rng(1234);
  DrillRun r = InitDrill(p, rng);
  // hand-computed: ms = (10*0.5+0.8)*80 + 10*70 = 1164; ticks = 1164*0.04+0.5
  assert(r.power == 80);                 // max(+0x414, 0x46)
  assert(r.ticks == 47);
  assert(r.stepMs == r.stepTicks * 200);
  assert(r.stepMs >= 2600 && r.stepMs <= 3200);   // (rand(4)+13)*200
  assert(r.count == r.ticks / r.stepTicks + 1);   // 7bcc98 rule
  assert(r.depth[0] == 80);              // array[0] = +0x414 at entry
  for (size_t i = 1; i + 1 < r.depth.size(); i++)
    assert(r.depth[i] >= 0x3c);          // min 0x3c until the last entry

  // +0x414/+0x418 rule (state 6): target = depth + 20, bucket = 004ac620
  Session s;
  s.state = SDrillHold; s.pending = SDrillHold;
  s.depth = 80; s.holeDepth = 200; s.rodCmd = 1;
  s.gen[0].Start(0, 100, 1000, nullptr, 0xc);   // drill ramp still running
  MsvcRand rng2(7);
  s.Step(0, 200, rng2);
  assert(s.target == 100);                        // 80 + 0x14
  assert(s.bucket == Bucket(200, 100));
  assert(s.state == SStandUp);
  // bucket ladder (004ac620 bands: <=130 / <=250 / deep)
  assert(Bucket(130, 130) == 2);
  assert(Bucket(200, 120) == 0);
  assert(Bucket(300, 100) == 0);
  assert(Bucket(300, 250) == 2);

  // --- 3. deterministic rand-roll outcome -------------------------------
  // pBad = 1.0 -> the second roll always fires (jam), pGood = 0 never does.
  DrillParams q = p;
  q.pGood = 0.0; q.pBad = 1.0;
  MsvcRand r3(99);
  DrillRun a = InitDrill(q, r3);
  assert(!a.good && a.bad);
  // pGood = 1.0 -> breakthrough; type-3 fish with weight > 1000 forces
  // stop = ticks when rand(3) < 2 (seed 99 hits it).
  DrillParams q2 = p;
  q2.fishType = 3; q2.recWeight = 2000; q2.pGood = 1.0; q2.pBad = 0.0;
  MsvcRand r4(99);
  DrillRun b = InitDrill(q2, r4);
  assert(b.good && !b.bad);
  assert(b.stop == b.ticks);              // forced full run (not rand(ticks))
  // frozen seeded outcome (MsvcRand(2024), params below)
  DrillParams q3;
  q3.depth0 = 80; q3.maxDepth = 300;
  q3.fishW = 10.0; q3.fishType = 1;
  q3.pGood = 0.5; q3.pBad = 0.25; q3.recWeight = 400;
  MsvcRand r5(2024);
  DrillRun c = InitDrill(q3, r5);
  assert(c.stepMs == 2600 && c.stepTicks == 13);
  assert(c.ticks == 47 && c.count == 4);
  assert(c.good && c.bad);                // both flags: second roll overwrote
  assert(c.stop == 34);                   // ... stop from the second roll
  assert(c.depth[0] == 80 && c.depth[1] == 60 && c.depth[3] == 83);

  // --- 4. walk timer duration formula (004875f0) ------------------------
  assert(WalkMs(1, false) == 550);        // (23-1)*25
  assert(WalkMs(7, false) == 400);        // (23-7)*25
  assert(WalkMs(8, false) == 400);        // players >= 8 -> 400
  assert(WalkMs(3, true) == 1000);        // doubled for stand-up (state 8)
  Session w;
  w.state = SWalk1; w.pending = SWalk1;
  w.players = 4;
  MsvcRand rng3(5);
  w.Step(0, 200, rng3);
  assert(w.state == SWalk2);
  assert(w.gen[0].dur == WalkMs(4, false) && w.gen[0].dur == 475);

  return 0;
}
