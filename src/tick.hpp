// Frame tick (WinMain loop body, FUN_00605eb0, SDL timer).
// Return protocol: 0 continue, 1 skip frame, 2 quit.
// Order per decomp (stage bodies decompiled: docs/decomp-notes.md;
// timing semantics ported below as Elapsed/Slices/Governor):
//  timer delta -> quit flag -> UPDATE {62e230,471810,467e80,651620,
//  606bd0,658570} -> RENDER {6499e0,6440b0,640370,652940,
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

// FUN_0059ae70 (used by ~100 callers): elapsed = now - prev; on wrap
// (now < prev) result is (now - prev) - 1, i.e. 2^32-d-1 as uint (huge,
// callers clamp). Matches original bit-for-bit.
inline uint32_t Elapsed(uint32_t now, uint32_t prev) {
  uint32_t e = now - prev;
  return now < prev ? e - 1 : e;
}

// FUN_00471810 update slicer: delta runs the sim in <=999ms slices; when
// the remainder is >60000ms (focus loss etc) a slice widens to 5000ms.
// Zero delta still calls step(0) once (original does too).
template <typename F>
inline void Slices(uint32_t delta, F step) {
  uint32_t left = delta;
  do {
    uint32_t s = left;
    if (left > 999) s = left > 60000 ? 5000u : 1000u;
    left -= s;
    step(s);
  } while (left > 0);
}

// FUN_00640370 frame limiter. Mode from vsync probe FUN_0061ec30:
// 0 no sleep, 1 adaptive, 2 Sleep(1), 3 Sleep(5). fps sampled every
// >=500ms as round(frames*1000/ms); adaptive: fps>=121 sleepMs++ (cap 10),
// 80<=fps<100 sleepMs-- (floor 0), 100..120 hold, fps<80 sleepMs=0.
// Call Count() once per frame, then Frame(now, mode) -> ms to sleep.
struct Governor {
  uint32_t sampleAt = 0;  // 7c83b8
  uint32_t frames = 0;    // 7c8b0c
  int sleepMs = 0;        // 7c83b4
  int fps = 0;            // 7c6958
  bool enabled = true;    // 7c83bc
  void Count() { frames++; }
  int Frame(uint32_t now, int mode) {
    if (sampleAt == 0) sampleAt = now;
    uint32_t ms = Elapsed(now, sampleAt);
    if (ms > 499) {
      fps = (int)((1000.0 / (double)ms) * (double)frames + 0.5);
      frames = 0;
      sampleAt = now;
      if (!enabled) {
        sleepMs = 0;
      } else if (mode == 1) {
        if (fps >= 121) {
          if (sleepMs < 10) sleepMs++;
        } else if (fps >= 80) {
          if (fps < 100 && sleepMs > 0) sleepMs--;
        } else {
          sleepMs = 0;
        }
      }
    }
    if (mode == 0) return 0;
    if (mode == 2) return 1;
    if (mode == 3) return 5;
    return sleepMs;
  }
};

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
