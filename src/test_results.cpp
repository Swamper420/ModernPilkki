// Asserts for src/results.hpp (docs/sim-results.md). Minimal.
#include <cassert>
#include <cstdio>

#include "results.hpp"

int main() {
  using namespace results;

  // Row formulas: total weight and biggest fish from the tallies.
  Tally t;
  t.count = 3;
  t.totalG = 1234.5;
  t.biggestG = 700;
  t.bites = 2;
  auto rows = Build(t);
  assert(rows.size() == 2);
  assert(rows[0].id == kRowTotalWeight);
  assert(rows[0].valueG == 1234.5);
  assert(rows[1].id == kRowBiggest);
  assert(rows[1].valueG == 700.0);

  // "No bites." row appears exactly when bites == 0.
  Tally z;
  z.totalG = 0;
  auto zrows = Build(z);
  assert(zrows.size() == 3);
  assert(zrows[2].id == kRowNoBites);

  // 5-catch review (0x47845a): give up when biggest < 50 or avg < 30.
  Tally r;
  r.count = 5;
  r.biggestG = 60;
  r.totalG = 250;  // avg 50
  assert(!GiveUpAfterFive(r));
  r.biggestG = 49;
  assert(GiveUpAfterFive(r));
  r.biggestG = 60;
  r.totalG = 149;  // avg 29.8
  assert(GiveUpAfterFive(r));
  r.count = 4;
  assert(!GiveUpAfterFive(r));

  std::puts("test_results ok");
  return 0;
}
