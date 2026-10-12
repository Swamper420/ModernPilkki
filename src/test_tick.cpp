// Smallest assert: clock deltas, stage order, skip/quit protocol.
#include <cassert>
#include <cstdio>
#include <vector>
#include "tick.hpp"

static std::vector<int> *g;
static void S0() { g->push_back(100); }
static void S1() { g->push_back(101); }
static void R0() { g->push_back(200); }

int main() {
  tick::Clock c;
  c.Update(5000);
  assert(c.delta == 0 && c.now == 5000);
  c.Update(5016);
  assert(c.delta == 16);

  std::vector<int> seen;
  g = &seen;
  tick::Driver d;
  d.stages.update[0] = S0;
  d.stages.update[6] = S1;
  d.stages.render[0] = R0;
  d.log = &seen;
  assert(d.Step() == tick::Action::Continue);
  // hooks fire in order (100,101,200), log ids interleave (0,6,7)
  assert(seen.size() == 6 && seen[0] == 100 && seen[1] == 0 &&
         seen[2] == 101 && seen[3] == 6 && seen[4] == 200 && seen[5] == 7);

  d.renderEnabled = false;
  assert(d.Step() == tick::Action::Skip);
  d.renderEnabled = true;
  d.updateEnabled = false;
  assert(d.Step() == tick::Action::Skip);
  d.updateEnabled = true;
  d.quitRequested = true;
  assert(d.Step() == tick::Action::Quit);
  printf("tick tests pass\n");
  return 0;
}
