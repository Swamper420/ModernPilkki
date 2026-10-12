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

  // Elapsed (0059ae70): plain diff; wrap gives (now-prev)-1 as uint.
  assert(tick::Elapsed(5016, 5000) == 16);
  assert(tick::Elapsed(10, 5) == 5);
  assert(tick::Elapsed(5, 10) == 0xfffffffa);  // wrap: (5-10)-1
  assert(tick::Elapsed(0, 0xffffffff) == 0);

  // Slices (00471810): <=999 single call; >=1000 split; >60000 widens
  // to 5000; zero still fires once with 0.
  std::vector<uint32_t> sl;
  tick::Slices(0, [&](uint32_t s) { sl.push_back(s); });
  assert(sl.size() == 1 && sl[0] == 0);
  sl.clear();
  tick::Slices(999, [&](uint32_t s) { sl.push_back(s); });
  assert(sl.size() == 1 && sl[0] == 999);
  sl.clear();
  tick::Slices(1000, [&](uint32_t s) { sl.push_back(s); });
  assert(sl.size() == 1 && sl[0] == 1000);
  sl.clear();
  tick::Slices(2500, [&](uint32_t s) { sl.push_back(s); });
  assert(sl.size() == 3 && sl[0] == 1000 && sl[1] == 1000 && sl[2] == 500);
  sl.clear();
  tick::Slices(70000, [&](uint32_t s) { sl.push_back(s); });
  assert(sl.size() == 62 && sl[0] == 5000 && sl[1] == 5000 && sl[2] == 1000 &&
         sl[61] == 1000);  // 2x5000 (only while >60000) + 60x1000

  // Governor (00640370): fixed modes, 500ms window, adaptive sleep.
  tick::Governor gov;
  assert(gov.Frame(1000, 1) == 0);      // first sample, no window yet
  assert(gov.Frame(1499, 0) == 0);      // mode 0: never sleeps
  assert(gov.Frame(1500, 2) == 1);      // window closed at 501ms -> mode 2
  assert(gov.Frame(1501, 3) == 5);      // mode 3
  tick::Governor g2;
  int exp = 0;
  for (int i = 1; i <= 12; i++) {       // 61 frames / 500ms = 122 fps
    for (int f = 0; f < 61; f++) g2.Count();
    g2.Frame((uint32_t)(i * 500), 1);
    if (i >= 2) {                       // first call only sets sample base
      if (exp < 10) exp++;
      assert(g2.sleepMs == exp);
    }
  }
  assert(g2.fps >= 121 && g2.sleepMs == 10);  // capped at 10
  tick::Governor g3;
  for (int i = 1; i <= 3; i++) {        // 40 frames / 500ms = 80 fps
    for (int f = 0; f < 40; f++) g3.Count();
    g3.Frame((uint32_t)(i * 500), 1);
  }
  assert(g3.fps >= 80 && g3.fps < 100 && g3.sleepMs == 0);  // floor 0
  tick::Governor g4;
  g4.sleepMs = 4;
  g4.enabled = false;                   // limiter off -> sleep cleared
  for (int f = 0; f < 61; f++) g4.Count();
  g4.Frame(500, 1);
  g4.Frame(1000, 1);                    // window closes on 2nd call
  assert(g4.fps >= 121 && g4.sleepMs == 0);
  printf("tick tests pass\n");
  return 0;
}
