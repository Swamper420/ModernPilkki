// Frame tick (WinMain loop body, FUN_00605eb0, SDL timer).
// Return protocol: 0 continue, 1 skip frame, 2 quit.
// Order per decomp (callees unported; null stages skipped until then):
//  timer delta -> quit flag -> UPDATE {62e230,471810,467e80,651620,
//  651620,606bd0,658570} -> RENDER {6499e0,6440b0,640370,652940,
//  [638280],64c920 + DX-restore check}.
// Flags: updateEnabled = 7c6a09, renderEnabled = 7c6a08,
// quitRequested = 7c689c / 7c6908==0x32. Async Sleep gate (200/50/20ms)
// and per-frame time base (7c6a0c/7c695c) unported (open).
#pragma once
#include <cstdint>
#include <vector>

namespace tick {

enum class Action { Continue = 0, Skip = 1, Quit = 2 };

// Millisecond clock (7c6a68/6c accumulators). First update: delta 0.
struct Clock {
  uint32_t last = 0, now = 0, delta = 0;
  bool started = false;
  void Update(uint32_t t) {
    now = t;
    delta = started ? now - last : 0;
    last = now;
    started = true;
  }
};

// Nullable stage hooks, called in 00605eb0 order when set.
struct Stages {
  void (*update[7])() = {};
  void (*render[6])() = {};
};

inline void RunStages(void (*const *fn)(void), size_t n,
                      std::vector<int> *log, int base) {
  for (size_t i = 0; i < n; i++)
    if (fn[i]) {
      fn[i]();
      if (log) log->push_back(base + (int)i);
    }
}

struct Driver {
  bool updateEnabled = true;  // 7c6a09
  bool renderEnabled = true;  // 7c6a08
  bool quitRequested = false;  // 7c689c
  Stages stages;
  std::vector<int> *log = nullptr;  // test hook, else null

  Action Step() {
    if (quitRequested) return Action::Quit;
    if (!updateEnabled) return Action::Skip;
    RunStages(stages.update, 7, log, 0);
    if (!renderEnabled) return Action::Skip;
    RunStages(stages.render, 6, log, 7);
    return Action::Continue;
  }
};

}  // namespace tick
