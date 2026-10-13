// Smallest assert for the 00640370/00632560 phase protocol (state.hpp):
// enter before frame, deferred pending->active copy, exit of old state
// before enter of new, Esc pause return via pauseRet (7c8790).
#include <cassert>
#include <cstdio>
#include <vector>

#include "state.hpp"

int main() {
  std::vector<std::pair<int, int>> log;  // (state id, phase) per block call
  auto rec = [&](int id, int phase) { log.push_back({id, phase}); };

  // Steady state: pending==active, phase frame -> one frame block only.
  state::Driver d{10, 10, state::kFrame, 20};
  state::Pass(d, rec);
  assert(log.size() == 1 && log[0].first == 10 &&
         log[0].second == state::kFrame);
  assert(d.active == 10 && d.phase == state::kFrame);

  // Enter before frame: enter block runs, pass returns at frame phase;
  // the frame block runs on the next pass (00640370 exits once phase==1).
  state::Driver e{10, 10, state::kEnter, 20};
  log.clear();
  state::Pass(e, rec);
  assert(log.size() == 1 && log[0].second == state::kEnter);
  assert(e.phase == state::kFrame);
  state::Pass(e, rec);
  assert(log.size() == 2 && log[1].second == state::kFrame);

  // Request from frame phase: frame(old) -> exit(old) -> enter(new).
  // Active only changes at the deferred copy (00632560), after exit.
  log.clear();
  d.pending = 20;
  state::Pass(d, rec);
  assert(log.size() == 3);
  assert(log[0].first == 10 && log[0].second == state::kFrame);
  assert(log[1].first == 10 && log[1].second == state::kExit);
  assert(log[2].first == 20 && log[2].second == state::kEnter);
  assert(d.active == 20 && d.pending == 20 && d.phase == state::kFrame);

  // Request from enter phase: exit still precedes the new enter.
  log.clear();
  e.pending = 4;
  state::Pass(e, rec);  // e at frame phase; request lands, old exits first
  assert(log.size() == 3);
  assert(log[0].first == 10 && log[0].second == state::kFrame);
  assert(log[1].first == 10 && log[1].second == state::kExit);
  assert(log[2].first == 4 && log[2].second == state::kEnter);
  assert(e.active == 4 && e.phase == state::kFrame);

  // Esc pause return (7c8790): Pause stores ret, hub goes active,
  // resume returns to the stashed state.
  state::Pause(d, 20, 4);  // d active 20 (play)
  assert(state::ResumeId(d) == 20 && d.pending == 4);
  state::Pass(d, rec);  // exit(20), enter(4)
  assert(d.active == 4 && d.phase == state::kFrame);
  d.pending = state::ResumeId(d);  // hub frame chooses resume (screen 0x15)
  state::Pass(d, rec);             // exit(4), enter(20)
  assert(d.active == 20 && d.phase == state::kFrame);

  printf("state tests pass\n");
  return 0;
}
