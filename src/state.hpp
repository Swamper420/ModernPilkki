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

// ---------------------------------------------------------------------------
// Per-screen fade machine. Separate from the 7c6904 mode driver above: the
// original keeps one Screen object per screen id (registry 005c8930) and
// walks it through the 006304e0 phase machine. Each screen object carries its
// own fade-in/out durations; the fade value is a permille ramp 0..1000.
// Fields/offsets from docs/decomp-notes.md "Screen driver 006304e0".
// ---------------------------------------------------------------------------

// 00651620 fade timer entry (created by 00650fe0 -> 00650ee0 with mode 8).
// rate = 1.0 / duration; the driver's fade tick reads 00651860 with curve
// selector (mode & 7) == 0 == LINEAR: value = 1000 * clamp(t, 0, 1) where
// t = elapsed * rate. Done when t >= 1.0 (DAT_00777888 = 1.0).
struct FadeTimer {
  int durationMs = 0;   // screen +0x34 in / +0x38 out
  int elapsedMs = 0;    // accumulated wall-clock ms
  bool active = false;  // handle (+0x3c/+0x40) != 0
  bool done = false;    // 00651220: entry state == 2
  void Advance(int deltaMs) {  // 00651620 update pass
    if (!active || done) return;
    elapsedMs += deltaMs;
    if (elapsedMs >= durationMs) {
      elapsedMs = durationMs;
      done = true;
    }
  }
  int Value() const {  // 00651860 mode 8 -> linear 0..1000
    if (durationMs <= 0) return 1000;
    int v = (int)((long)elapsedMs * 1000 / durationMs);
    return v > 1000 ? 1000 : v;
  }
};

// One screen object (0x60 B, 88-slot registry 005c8930).
struct Screen {
  int phase = 0;         // +0x30: 0 enter, 1 fade-in, 2 frame, 3 fade-out
  int nextId = -1;       // +0x44; enter resets to -1, exit reads it
  int fadeInMs = 0;      // +0x34 (0 = skip fade-in)
  int fadeOutMs = 0;     // +0x38 (0 = skip fade-out)
  int fade = 0;          // +0x48 permille 0..1000 (timer target)
  int fadeTick = 0;      // +0x4c copy handed to the tick method
  bool skipIn = false;   // +0x58 (006306d0 -> fade-in snaps to 1000)
  bool skipOut = false;  // +0x5c (006306e0)
  FadeTimer in, out;     // +0x3c / +0x40 timer handles

  void SetSkipIn() { skipIn = true; }    // 006306d0
  void SetSkipOut() { skipOut = true; }  // 006306e0
  // 00630790: a frame requests exit + the next screen id.
  void GoToState(int id) { nextId = id; phase = 3; }
};

// 00651620: advance the active fade timer into screen +0x48. Runs in the
// UPDATE stage, before 006304e0 (so the driver sees the updated value).
inline void FadeUpdate(Screen &s, int deltaMs) {
  if (s.phase == 1 && s.in.active) {
    s.in.Advance(deltaMs);
    s.fade = s.in.Value();
  } else if (s.phase == 3 && s.out.active) {
    s.out.Advance(deltaMs);
    s.fade = s.out.Value();
  }
}

// 006304e0 for one screen. Callbacks: enter() = vtable +0, fadeTick(s) =
// +4/+0xc, frame() = +8, exit() = +0x10. Returns true when the screen
// exited (s.nextId holds the next id; caller runs the new screen's enter).
template <class Enter, class FadeTick, class Frame, class Exit>
bool ScreenStep(Screen &s, Enter enter, FadeTick fadeTick, Frame frame,
                Exit exit) {
  if (s.phase == 0 || s.phase == 1) {
    if (s.phase == 0) {
      enter();
      s.nextId = -1;
      s.phase = 1;
    }
    if (s.fadeInMs == 0) {  // skip-when-0: straight to frame
      s.phase = 2;
      frame();
      return false;
    }
    if (!s.in.active) {  // 00650fe0("Fade_in",0,1000,dur,&fade,8)
      s.in = FadeTimer{};
      s.in.durationMs = s.fadeInMs;
      s.in.active = true;
      s.fade = 0;
      s.fadeTick = 0;
      fadeTick(s);
      return false;
    }
    if (!s.in.done && !s.skipIn) {  // 00651220 not done and +0x58 clear
      s.fadeTick = s.fade;
      fadeTick(s);
      return false;
    }
    if (s.fadeTick == 1000) {  // done: kill timer, phase 2, frame
      s.skipIn = false;
      s.in = FadeTimer{};
      s.phase = 2;
      frame();
      return false;
    }
    s.fade = 1000;  // skip flag: snap the value (+0x48/+0x4c)
    s.fadeTick = 1000;
    fadeTick(s);
    return false;
  }
  if (s.phase == 2) {
    frame();
    return false;
  }
  if (s.phase == 3) {
    if (s.fadeOutMs != 0) {
      if (!s.out.active) {  // 00650fe0("Fade_out",0,1000,dur,&fade,8)
        s.out = FadeTimer{};
        s.out.durationMs = s.fadeOutMs;
        s.out.active = true;
        s.fade = 0;
        s.fadeTick = 0;
        fadeTick(s);
        return false;
      }
      if (!s.out.done && !s.skipOut) {
        s.fadeTick = s.fade;
        fadeTick(s);
        return false;
      }
      if (s.fadeTick != 1000) {
        s.fade = 1000;
        s.fadeTick = 1000;
        fadeTick(s);
        return false;
      }
      s.skipOut = false;
      s.out = FadeTimer{};
    }
    exit();
    s.phase = 0;  // next: caller does 0058aaf0 lookup + enter(new)
    return true;
  }
  return false;
}

// 006304e0 driver loop over a registry: exit(old) + enter(new) in the SAME
// call (do-while, not return). lookup(id) -> Screen*, null stops.
template <class Lookup, class Enter, class FadeTick, class Frame, class Exit>
void ScreenRun(int &currentId, Lookup lookup, Enter enter, FadeTick fadeTick,
               Frame frame, Exit exit) {
  if (currentId == -1) return;
  for (;;) {
    Screen *s = lookup(currentId);
    if (!s) {
      currentId = -1;
      return;
    }
    if (!ScreenStep(*s, enter, fadeTick, frame, exit)) return;
    currentId = s->nextId;
    if (currentId == -1) return;
  }
}

// 005c8930 registry fade durations (ms). 700 for the three splash/load
// screens, 500 for the menu screens, END_SCREEN 0/1800, RESULTS_START 0/500;
// everything else none. (Corrects two off-by-one ids in decomp-notes.)
struct FadeDur {
  int inMs, outMs;
};
inline FadeDur ScreenFadeDur(int id) {
  if (id == 0 || id == 1 || id == 2) return {700, 0};  // LOAD_ADDON/START/MAIN
  if (id == 0xf) return {0, 1800};                     // END_SCREEN
  if (id == 0x16) return {0, 500};                     // RESULTS_START
  switch (id) {
    case 3: case 6: case 7: case 8: case 9: case 0xa: case 0xb:
    case 0xc: case 0xe: case 0x18: case 0x1b: case 0x1d: case 0x1e:
    case 0x1f: case 0x28: case 0x2a: case 0x2f: case 0x32: case 0x34:
    case 0x35: case 0x36: case 0x37: case 0x38: case 0x39: case 0x3a:
    case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x40:
    case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49:
    case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55:
    case 0x56: case 0x57:
      return {500, 0};
  }
  return {0, 0};  // PLAY (0x15), PAUSE (0x10), RESULTS_TOTAL (0x17), ...
}

}  // namespace state
