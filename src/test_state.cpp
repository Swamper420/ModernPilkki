// Smallest assert for the 00640370/00632560 phase protocol (state.hpp):
// enter before frame, deferred pending->active copy, exit of old state
// before enter of new, Esc pause return via pauseRet (7c8790).
#include <cassert>
#include <cstdio>
#include <cstring>
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

  // --- Per-screen fade machine (state.hpp, 006304e0 + 00651620) ---

  // 00651860 curve for mode 8 (selector 0) = LINEAR: 1000*elapsed/duration.
  state::FadeTimer ft;
  ft.durationMs = 500;
  ft.active = true;
  ft.Advance(250);
  assert(ft.Value() == 500 && !ft.done);
  ft.Advance(250);
  assert(ft.Value() == 1000 && ft.done);

  // fade-in: enter -> create "Fade_in", tick each step; finish -> frame.
  {
    state::Screen s;
    s.fadeInMs = 100;
    std::vector<const char *> ev;
    auto enter = [&] { ev.push_back("enter"); };
    auto tick = [&](state::Screen &) { ev.push_back("tick"); };
    auto frame = [&] { ev.push_back("frame"); };
    auto exit = [&] { ev.push_back("exit"); };
    assert(!state::ScreenStep(s, enter, tick, frame, exit));
    assert(ev.size() == 2 && !strcmp(ev[0], "enter") && !strcmp(ev[1], "tick"));
    assert(s.phase == 1 && s.in.active);
    state::FadeUpdate(s, 50);
    assert(s.fade == 500);
    state::ScreenStep(s, enter, tick, frame, exit);  // tick with fade 500
    assert(ev.size() == 3 && s.fadeTick == 500);
    state::FadeUpdate(s, 50);  // done, value 1000
    assert(s.fade == 1000 && s.in.done);
    state::ScreenStep(s, enter, tick, frame, exit);  // +0x4c snap tick
    assert(s.phase == 1 && s.fadeTick == 1000 && !strcmp(ev.back(), "tick"));
    state::ScreenStep(s, enter, tick, frame, exit);  // finish -> frame
    assert(s.phase == 2 && !strcmp(ev.back(), "frame"));
  }

  // skip-when-0: no fade-in -> enter, phase 2, frame in the same call.
  {
    state::Screen s;
    s.fadeInMs = 0;
    std::vector<const char *> ev;
    auto enter = [&] { ev.push_back("enter"); };
    auto tick = [&](state::Screen &) { ev.push_back("tick"); };
    auto frame = [&] { ev.push_back("frame"); };
    auto exit = [&] { ev.push_back("exit"); };
    state::ScreenStep(s, enter, tick, frame, exit);
    assert(s.phase == 2 && ev.size() == 2 && !strcmp(ev[1], "frame"));
  }

  // 006306d0 skip: +0x58 set mid-fade snaps to 1000, then finishes next step.
  {
    state::Screen s;
    s.fadeInMs = 200;
    auto noop = [] {};
    auto tick = [](state::Screen &) {};
    state::ScreenStep(s, noop, tick, noop, noop);  // create timer
    s.SetSkipIn();                                 // 006306d0
    state::ScreenStep(s, noop, tick, noop, noop);  // snap
    assert(s.fade == 1000 && s.fadeTick == 1000 && s.phase == 1 && s.skipIn);
    state::ScreenStep(s, noop, tick, noop, noop);  // finish -> phase 2
    assert(s.phase == 2 && !s.skipIn);
  }

  // fade-out skip-when-0 (00630790): exit runs, screen reports next id.
  {
    state::Screen s;
    s.fadeOutMs = 0;
    s.GoToState(20);
    bool exited = false;
    auto noop = [] {};
    auto tick = [](state::Screen &) {};
    auto exit = [&] { exited = true; };
    assert(state::ScreenStep(s, noop, tick, noop, exit));
    assert(exited && s.phase == 0 && s.nextId == 20);
  }

  // fade-out with duration: tick until done, then exit.
  {
    state::Screen s;
    s.fadeOutMs = 100;
    s.GoToState(7);
    bool exited = false;
    auto noop = [] {};
    auto tick = [](state::Screen &) {};
    auto exit = [&] { exited = true; };
    assert(!state::ScreenStep(s, noop, tick, noop, exit));  // create timer
    assert(!exited && s.phase == 3 && s.out.active);
    state::FadeUpdate(s, 100);
    assert(!state::ScreenStep(s, noop, tick, noop, exit));  // +0x4c snap tick
    assert(!exited && s.fadeTick == 1000);
    assert(state::ScreenStep(s, noop, tick, noop, exit));   // finish -> exit
    assert(exited && s.phase == 0);
  }

  // Driver loop: exit(old) + enter(new) inside one ScreenRun call (0058aaf0).
  {
    state::Screen a, b;
    a.fadeOutMs = 0;
    b.fadeInMs = 0;
    a.GoToState(20);
    state::Screen *reg[21] = {};
    reg[10] = &a;
    reg[20] = &b;
    std::vector<const char *> ev;
    auto enter = [&] { ev.push_back("enter"); };
    auto tick = [&](state::Screen &) {};
    auto frame = [&] { ev.push_back("frame"); };
    auto exit = [&] { ev.push_back("exit"); };
    int cur = 10;
    state::ScreenRun(cur, [&](int id) { return reg[id]; }, enter, tick, frame,
                     exit);
    assert(cur == 20 && b.phase == 2);
    assert(ev.size() == 3 && !strcmp(ev[0], "exit") &&
           !strcmp(ev[1], "enter") && !strcmp(ev[2], "frame"));
  }

  // 005c8930 durations (registry-verified; corrects 2 off-by-one ids).
  assert(state::ScreenFadeDur(0).inMs == 700);    // LOAD_ADDON_PACKAGE
  assert(state::ScreenFadeDur(2).inMs == 700);    // MAIN
  assert(state::ScreenFadeDur(3).inMs == 500);    // SINGLE_PLAYER
  assert(state::ScreenFadeDur(0xf).outMs == 1800);  // END_SCREEN
  assert(state::ScreenFadeDur(0x16).outMs == 500);  // RESULTS_START
  assert(state::ScreenFadeDur(0x15).inMs == 0);     // PLAY: no fades
  assert(state::ScreenFadeDur(0x10).inMs == 0);     // PAUSE
  assert(state::ScreenFadeDur(0x4e).inMs == 500);   // RECORDS_CLEARED

  printf("state tests pass\n");
  return 0;
}
