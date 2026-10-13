// Smallest asserts for src/bite.hpp: 9-cell walk, weight formula + caps,
// timing ramp, difficulty ramp, outcome byte rule, species tables, ring
// wrap, rate limiter, and a deterministic record build. All RNG runs are
// seeded so the checks are reproducible.
#include <cassert>
#include <cstdio>

#include "bite.hpp"

using namespace bite;

namespace {

// 004a9490 strict cell bounds: cx*600 < x <= (cx+1)*600.
void TestCellOf() {
  SyntheticStock s(4, 4);
  assert(s.CellOf(0, 0) == -1);       // x must be > 0
  assert(s.CellOf(600, 600) == 0);    // right edge belongs to cell 0
  assert(s.CellOf(601, 601) == 5);    // 1 + 4*1
  assert(s.CellOf(1250, 650) == 6);   // 2 + 4*1
  assert(s.CellOf(2400, 1600) == 11);  // 3 + 4*2, right edge inclusive
  assert(s.CellOf(2401, 1601) == -1);  // cx == gridW
}

void TestWalk() {
  // Centre cell of a 5x5 grid: the full 3x3 in (dx,dy) table order.
  auto w = WalkCells(12, 5, 5);
  const int want[9][2] = {{1, 1}, {2, 1}, {1, 2}, {2, 2}, {3, 1},
                          {3, 2}, {1, 3}, {2, 3}, {3, 3}};
  assert(w.size() == 9);
  for (int i = 0; i < 9; i++) {
    assert(w[i].first == want[i][0]);
    assert(w[i].second == want[i][1]);
  }
  // Corner cell: only 4 in-bounds neighbours, seed is not advanced.
  auto c = WalkCells(0, 5, 5);
  assert(c.size() == 4);
  assert(c[0].first == 0 && c[0].second == 0);
  assert(c[1].first == 1 && c[1].second == 0);
  assert(c[2].first == 0 && c[2].second == 1);
  assert(c[3].first == 1 && c[3].second == 1);
  assert(WalkCells(-1, 5, 5).empty());
}

void TestWeightTiming() {
  assert(Weight(1800, 10) == 30.0);
  assert(Weight(100000, 10) == 300.0);              // cap 006da020
  assert(Weight(10000, 20) == 30.0);                // cap 007778d8
  assert(Weight(1000, 30) == 1000.0 * 0.00625);
  assert(Weight(1000, 40) == 1000.0 * 0.004347826086956522);
  assert(Timing32(300) == 0.05);
  assert(Timing32(301) == 0.05 + 0.00016665714285714285);
  assert(Timing32(1000) == 0.05 + 700 * 0.00016665714285714285);
  assert(Timing32(1001) == 0.16666);
}

void TestRamp() {
  assert(Ramp(0, 1, 799) == 100);
  assert(Ramp(0, 1, 800) == 100);
  assert(Ramp(0, 1, 1125) == 50);   // frac 0.5 -> 100 - (int)50.5
  assert(Ramp(0, 1, 1450) == 0);
  assert(Ramp(0, 1, 1451) == 0);
  assert(Ramp(1, 1, 2000) == 100);
  assert(Ramp(1, 1, 4250) == 50);
  assert(Ramp(1, 1, 6501) == 0);
  assert(Ramp(0, 0, 1) == 0);       // type 0 row is empty
  assert(OutcomeThreshold(10, 1) == 95);
  assert(OutcomeThreshold(20, 1) == 60);
  assert(OutcomeThreshold(30, 1) == 30);
  assert(OutcomeThreshold(40, 1) == 0);
  assert(OutcomeThreshold(40, 3) == 10);
}

void TestOutcome() {
  Rng rng;
  rng.Seed(7);
  assert(OutcomeByte(0, 1, 500, 10, rng) == 0);    // ramp 100 -> 0
  assert(OutcomeByte(1, 1, 4300, 10, rng) == 1);   // flag 1, v < 16500
  // ramp 0, line 40 threshold 0: r2 >= 0 always -> 3.
  assert(OutcomeByte(0, 1, 1449, 40, rng) == 3);
}

void TestSpecies() {
  const int *t = SpeciesTable(2, 25);
  const int a[8] = {1, 1, 1, 2, 2, 2, 3, 8};
  for (int i = 0; i < 8; i++) assert(t[i] == a[i]);
  const int b[8] = {1, 1, 2, 2, 3, 4, 7, 8};
  t = SpeciesTable(2, 100);
  for (int i = 0; i < 8; i++) assert(t[i] == b[i]);
  const int c[8] = {6, 7, 7, 7, 9, 1, 1, 2};
  t = SpeciesTable(2, 500);
  for (int i = 0; i < 8; i++) assert(t[i] == c[i]);
  const int d[8] = {6, 6, 6, 6, 7, 7, 4, 4};
  t = SpeciesTable(3, 5000);
  for (int i = 0; i < 8; i++) assert(t[i] == d[i]);
  t = SpeciesTable(5, 5000);  // type 5 shares type 3's table
  for (int i = 0; i < 8; i++) assert(t[i] == d[i]);
  const int e[8] = {3, 4, 5, 5, 9, 9, 3, 8};
  t = SpeciesTable(0xf, 0);
  for (int i = 0; i < 8; i++) assert(t[i] == e[i]);
  const int f[8] = {3, 9, 5, 5, 9, 9, 3, 8};
  t = SpeciesTable(0x16, 0);
  for (int i = 0; i < 8; i++) assert(t[i] == f[i]);
}

void TestRing() {
  Ring ring;
  Rng rng;
  rng.Seed(99);
  for (int i = 0; i < 70; i++) {
    Record &r = ring.Push(rng, 10);
    assert(r.seq == (uint32_t)(i + 1));
    assert(r.minW >= 5 && r.minW <= 84);       // rand(80) + 5
    assert(r.rollDiv >= 1 && r.rollDiv <= 5);
    assert(r.f64 >= 0 && r.f64 <= 100);
  }
  assert(ring.Find(70) != nullptr);
  assert(ring.Find(7) != nullptr);   // slot survived the 64 wrap
  assert(ring.Find(6) == nullptr);   // overwritten
  assert(ring.Find(0) == nullptr);
  Record &r30 = ring.Push(rng, 30);
  assert(r30.minW >= -20 && r30.minW <= 59);   // rand(80) - 20
}

void TestRateWindow() {
  RateWindow w(4);
  assert(w.Allow(1000));
  assert(w.Allow(1000));
  assert(w.Allow(1000));
  assert(w.Allow(1000));
  assert(!w.Allow(100));   // avg 3100/4 = 775 < 900
  assert(!w.Allow(100));
  assert(!w.Allow(2000));  // avg 3200/4 = 800 < 900
  assert(w.Allow(2000));   // avg 4200/4 = 1050, recovered
}

void TestAttempt() {
  assert(AttemptThreshold(0) == 125);
  assert(AttemptThreshold(500) == 250);
  assert(AttemptThreshold(1000) == 500);
  assert(AttemptThreshold(1700) == -1);
  // Deterministic: the ambient roll is exactly rand(1000) >= 500.
  Rng a, b;
  a.Seed(5);
  b.Seed(5);
  bool expect = a.RandMax(1000) >= 500;
  assert(Attempt(false, 0, b) == expect);
  Rng c, d;
  c.Seed(5);
  d.Seed(5);
  (void)c.RandMax(1000);
  assert(Attempt(true, 2000, d));  // activity >= 1700 = always
}

void TestAutoBite() {
  int t = -1;
  assert(AutoBitePercent(10, 21, &t) == 1.0 && t == 80);
  assert(AutoBitePercent(10, 10, &t) == 1.0 && t == 20);
  assert(AutoBitePercent(10, 6, &t) == 1.3 && t == 0);
  assert(AutoBitePercent(10, 3, &t) == 1.6 && t == 0);
  assert(AutoBitePercent(20, 2, &t) == 1.17 && t == 0);
  assert(AutoBitePercent(30, 2, &t) == 0.3 && t == 0);
  assert(AutoBitePercent(30, 6, &t) == 0.8 && t == 0);
  assert(AutoBitePercent(30, 8, &t) == 1.0 && t == 0);
  assert(AutoBitePercent(40, 5, &t) == 0.2 && t == 0);
  assert(AutoBitePercent(40, 3, nullptr) == 0.05);
}

bool Same(const Record &a, const Record &b) {
  return a.species == b.species && a.idx == b.idx && a.type == b.type &&
         a.V() == b.V() && a.W == b.W && a.tag == b.tag &&
         a.timing == b.timing && a.bonus == b.bonus &&
         a.catchP == b.catchP && a.failP == b.failP &&
         a.outcome == b.outcome && a.outcome2 == b.outcome2 &&
         a.x == b.x && a.y == b.y && a.z == b.z && a.f50 == b.f50;
}

Context Ctx() {
  Context ctx;
  ctx.x = 1250;
  ctx.y = 650;
  ctx.z = 40;
  ctx.line = 10;
  ctx.f60 = 1;
  return ctx;
}

void TestBuildDeterministic() {
  Record r1, r2;
  Rng g1, g2;
  g1.Seed(4242);
  g2.Seed(4242);
  SyntheticStock s1(4, 4), s2(4, 4);
  bool ok1 = BuildRecord(Ctx(), s1, g1, r1);
  bool ok2 = BuildRecord(Ctx(), s2, g2, r2);
  assert(ok1 && ok2);
  assert(Same(r1, r2));
  assert(r1.species != 0);
  assert(r1.W <= WeightCap(10));
  assert(r1.outcome2 == r1.outcome);
  // The band selection is by v against the line rule (u32 = 300 at 10 m).
  const LineRule &rule = LineRuleFor(10);
  double cp, fp;
  Bands(rule, r1.V(), cp, fp);
  assert(r1.catchP == cp && r1.failP == fp);
}

// Empty stock + lucky seed exercises the fallback synth exactly.
struct EmptyStock : SyntheticStock {
  int Count(int, int) const override { return 0; }
  int SpeciesId(int idx) const override { return idx + 40; }
};

void TestFallback() {
  uint32_t seed = 0;
  for (; seed < 100000; seed++) {
    Rng probe;
    probe.Seed(seed);
    if (probe.RandMax(110) == 0) break;
  }
  assert(seed < 100000);
  Rng rng;
  rng.Seed(seed);
  EmptyStock stock;
  Record rec;
  Context ctx = Ctx();
  assert(BuildRecord(ctx, stock, rng, rec));
  assert(rec.type == 2);
  assert(rec.V() >= 9 && rec.V() <= 22);
  assert(rec.timing == 0.05 && rec.bonus == 0.0 && rec.outcome == 0);
  assert(rec.species == (uint32_t)stock.SpeciesId(rec.idx));
  assert(rec.W == Weight(rec.V(), 10));
  // Gate: state8 == -1 with f60 < 1 rejects before any RNG is drawn.
  Rng rng2;
  rng2.Seed(seed);
  Context bad = Ctx();
  bad.f60 = 0;
  Record rec2;
  assert(!BuildRecord(bad, stock, rng2, rec2));
  assert(rec2.species == 0);
}

}  // namespace

int main() {
  TestCellOf();
  TestWalk();
  TestWeightTiming();
  TestRamp();
  TestOutcome();
  TestSpecies();
  TestRing();
  TestRateWindow();
  TestAttempt();
  TestAutoBite();
  TestBuildDeterministic();
  TestFallback();
  std::printf("test_bite ok\n");
  return 0;
}
