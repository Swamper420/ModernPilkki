// 7c6904 game-mode state protocol, 1:1 from ProPilkki2 00640370 +
// 00632560 (deferred apply). Three globals become fields: pending
// DAT_007c6904, active DAT_007c6908, phase DAT_007c690c (0 enter,
// 1 per-frame, 2 exit). Requested states land in pending; Deferred()
// (00632560, called after EVERY state-block pass inside the 00640370
// do-while, not once at function end) advances the phase and only
// copies pending -> active after the exit phase ran. Esc pause return
// uses DAT_007c8790 semantics (pauseRet). See docs/decomp-notes.md.
#pragma once

namespace state {

inline constexpr int kEnter = 0;
inline constexpr int kFrame = 1;
inline constexpr int kExit = 2;

struct Driver {
  int pending = 0;   // DAT_007c6904
  int active = 0;    // DAT_007c6908
  int phase = 0;     // DAT_007c690c
  int pauseRet = 0;  // DAT_007c8790: state to resume from pause
};

// 00632560: same active/pending -> phase 0 becomes 1 (enter done).
// Pending change -> force phase 2 (exit); once exit ran, copy pending
// to active and drop back to enter.
inline void Deferred(Driver &d) {
  if (d.pending == d.active) {
    if (d.phase == kEnter) d.phase = kFrame;
    return;
  }
  if (d.phase != kEnter) {
    if (d.phase == kExit) {
      d.phase = kEnter;
      d.active = d.pending;
      return;
    }
    if (d.phase != kFrame) return;
  }
  d.phase = kExit;
}

// One 00640370 pass: run block(active, phase) -- enter, then frame,
// then exit of the old state, then enter of the new -- until the frame
// phase is current (the per-frame tail 0049e2b0 runs once per frame).
template <typename Block>
void Pass(Driver &d, Block block) {
  for (;;) {
    block(d.active, d.phase);
    Deferred(d);
    if (d.phase == kFrame) return;
  }
}

// Esc in play (00640370 states 0x14/0x15): remember the state to
// return to, request the pause hub (id 4).
inline void Pause(Driver &d, int ret, int pauseId) {
  d.pauseRet = ret;
  d.pending = pauseId;
}

inline int ResumeId(const Driver &d) { return d.pauseRet; }

}  // namespace state
