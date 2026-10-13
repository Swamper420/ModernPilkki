// Move/mode timeline machine + round chain. Clean-room port of the
// Propilkki 2 move sequencer (00467210 clock, 00464410/00464470/004644d0/
// 00464510/00464600/00465d50 query API) and the drill chain (00480820),
// per docs/sim-timeline.md. Header-only, no SDL. Keyframe evaluation is
// presentation and stays out (sim-timeline.md section 3.5).
#pragma once
#include <cstring>

namespace timeline {

// One move (= game mode). dur = (int)((float)len * speed) (00464510).
// chain = script+0x53a44 flag: 0 breaks the hop chain after one
// transition (00467210); shipped moves default 1 (00463630 reset).
struct Move {
  int len = 0;            // script+0x118, ms
  float speed = 1.0f;     // script+0x52aec (1.0: <STARTSCALING> unported)
  int next = -1;          // script+move*4 next-move table
  bool chain = true;      // script+0x53a44
  const char *name = "";  // script+0x522b0+move*30, <= 29 chars
};

// Move script (script+0x52f50 = count; AddMove 00463b50 caps 70 moves).
struct Script {
  static constexpr int kMaxMoves = 70;
  int count = 0;
  Move m[kMaxMoves];
};

// Verified table: gfx/3ds/human2.3ds <STARTMOVES> (57 moves, ids 0..56).
// len=<LENGTH> ms, next=<NEXT>, names verbatim (KAIRAAMINEN = drilling,
// KAVELY = walking, JUOKSU = running, NOSTO = fish lift, KORJAILU =
// tackle mending, KYYKYSSA = squatting). See docs/sim-timeline.md section 2.
inline const Script &HumanScript() {
  static const Script s = [] {
    Script t;
    struct Row { int len, next; const char *name; };
    static const Row rows[] = {
        {500, 1, "KAVELY1"},              // 0
        {500, 0, "KAVELY2"},              // 1
        {250, 0, "KAVELY_ALOITA"},        // 2
        {700, 5, "PYSAHTYMINEN_VASEN"},   // 3
        {700, 5, "PYSAHTYMINEN_OIKEA"},   // 4
        {100, 5, "PAIKALLAAN"},           // 5: standing, drill start OK
        {250, 7, "KAIRAUSASENTOON1"},     // 6: drill stance (004764b0)
        {550, 8, "KAIRAUSASENTOON2"},     // 7
        {200, 9, "KAIRAUSASENTOON3"},     // 8
        {1000, 9, "KAIRAAMINEN1"},        // 9: chain stage 1
        {1000, 11, "KAIRAAMINEN1TO2"},    // 10: chain stage 2
        {1000, 11, "KAIRAAMINEN2"},       // 11
        {1000, 13, "KAIRAAMINEN2TO3"},    // 12: chain stage 3 (0xc)
        {1000, 13, "KAIRAAMINEN3"},       // 13
        {1000, 15, "KAIRAAMINEN3TO4"},    // 14: chain stage 4 (0xe)
        {1000, 15, "KAIRAAMINEN4"},       // 15
        {1000, 17, "KAIRAAMINEN4TO5"},    // 16: chain stage 5 (0x10)
        {1000, 17, "KAIRAAMINEN5"},       // 17
        {350, 19, "LAPI_MENI"},           // 18: outcome "through" (0x12)
        {450, 20, "KAANNY_JATTAAKSEEN"},  // 19: hole creation (0x13)
        {2500, 21, "JATA_JA_KAANNY_TAKAISIN"},  // 20: handle teardown
        {500, 22, "REPPU_POIS"},          // 21: +0x640 = 1
        {1000, 23, "REPPU_MAAHAN"},       // 22
        {1000, 24, "ISTU"},               // 23
        {200, 24, "JUST_SITTING"},        // 24: fishing at hole (0x18)
        {1000, 26, "NOUSE_YLOS_1"},       // 25
        {500, 27, "NOUSE_YLOS_2"},        // 26
        {500, 28, "REPPU_SELKAAN"},       // 27
        {500, 29, "KAIRA_KATEEN"},        // 28
        {500, 5, "KAIRA_OLALLE"},         // 29
        {400, 31, "JUOKSU1"},             // 30: run, K = 0.725 (0x1e)
        {400, 30, "JUOKSU2"},             // 31: run (0x1f)
        {250, 33, "SUORAAN_JATTOASENTOON1"},  // 32
        {550, 34, "SUORAAN_JATTOASENTOON2"},  // 33
        {400, 20, "SUORAAN_JATTOASENTOON3"},  // 34
        {3000, 5, "PUUHA_STAND_KATSELU"},     // 35
        {500, 24, "KOKEILU"},             // 36: round-end mode (0x24)
        {500, 38, "NOSTO1"},              // 37: fish lift (0x25)
        {500, 37, "NOSTO2"},              // 38: fish lift (0x26)
        {500, 40, "KORJAILU_ASENTOON"},   // 39: round-end mode (0x27)
        {500, 40, "KORJAILEE"},           // 40: round-end mode (0x28)
        {500, 24, "PILKKIASENTOON"},      // 41
        {3000, 5, "PUUHA_STAND_LAKKI"},   // 42
        {3000, 5, "PUUHA_JALAT"},         // 43
        {3000, 5, "PUUHA_STAND_KATSELU2"},// 44
        {1000, 26, "NOUSE_YLOS_KORJAILUSTA"},  // 45
        {200, 47, "SUORAAN_SELKAAN1"},    // 46
        {550, 48, "SUORAAN_SELKAAN2"},    // 47
        {250, 5, "SUORAAN_SELKAAN3"},     // 48
        {350, 50, "LAPI_MENI_JATKA"},     // 49: outcome variant (0x31)
        {450, 46, "LAPI_MENI_JATKA_2"},   // 50
        {2000, 50, "POHJAAN_MENI_JATKA"}, // 51: outcome "bottom" (0x33)
        {1000, 53, "KYYKKYYN"},           // 52
        {100, 53, "KYYKYSSA"},            // 53: squatting (0x35, HUD flag)
        {1000, 5, "KYYKYSTA_YLOS"},       // 54
        {10000, 53, "KYYKYSSA_PUUHA1"},   // 55
        {1000, 56, "DUMMY_DISABLE"},      // 56
    };
    t.count = (int)(sizeof(rows) / sizeof(rows[0]));
    for (int i = 0; i < t.count; ++i) {
      t.m[i].len = rows[i].len;
      t.m[i].next = rows[i].next;
      t.m[i].name = rows[i].name;
    }
    return t;
  }();
  return s;
}

// The move/mode machine: this+0x1a4 elapsed, +0x1ac current, +0x1b0
// pending (00467210 clock half, hops <= 5 per call).
struct Machine {
  const Script *script = nullptr;
  int cur = -1;       // +0x1ac, -1 = not started
  int pending = -1;   // +0x1b0
  int elapsed = 0;    // +0x1a4

  explicit Machine(const Script &s) : script(&s) {}

  int Duration(int mode) const {  // 00464510
    return (int)((float)script->m[mode].len * script->m[mode].speed);
  }

  int Mode() const { return cur; }        // 00464470
  int Pending() const { return pending; } // 00464600

  int Elapsed() const {  // 004644d0: clamp to duration-1
    if (cur < 0) return 0;
    int d = Duration(cur);
    return elapsed < d ? elapsed : d - 1;
  }

  // 00465d50: queue mode after the current move ends.
  int Queue(int mode) {
    if (!script || mode >= script->count || mode < 0) return -1;
    pending = mode;
    return 0;
  }

  // 00464410: queue and restart current (cur = -1 -> next Update jumps).
  int Jump(int mode) {
    if (Queue(mode) != 0) return -1;
    cur = -1;
    return 0;
  }

  // 00467210 clock: advance dt ms (dt > 0), run the hop loop.
  void Update(int dt) {
    if (!script) return;
    if (dt > 0) elapsed += dt;
    if (cur == -1) {
      elapsed = 0;
      if (pending != -1) cur = pending;  // + 00467f10/004638f0 hooks (visual)
      pending = -1;
    }
    if (cur < 0) return;
    for (int hops = 0; hops < 5;) {
      int d = Duration(cur);
      if (elapsed < d) break;
      elapsed -= d;
      ++hops;
      if (pending == -1) {
        int next = script->m[cur].next;
        if (next != cur && next != -1) {
          cur = next;
          if (!script->m[cur].chain) break;  // one hop per call
        }
      } else {
        cur = pending;  // pending hops chain freely (no break in 00467210)
        pending = -1;
      }
    }
  }
};

// ---- drill chain / round structure (00480820, session-side) ----

// Per-stage hold, session+0x46c (0047a1c0): v = drill skill 0042bd00().
inline int HoldMs(int v) {
  if (v < 0) v = 0;
  int h = (v > 100) ? 100000 : v * 1000;
  return h > 0 ? h + 200 : h;
}

// Chain outcome inputs (session bytes +0x4e8 / +0x6c0 / +0x4e4).
struct ChainInput {
  int holdOff = 0;   // +0x4e4: 2 = plain drill start, hold gates the exit
  bool bottom = false;  // +0x4e8: outcome POHJAAN_MENI_JATKA
  bool alt = false;     // +0x6c0: outcome LAPI_MENI_JATKA
};

// Stage machine at session+0x450 (+0x470 stamp). Step() mirrors the
// 00480820 chain: 9 -> 10 -> 0xc -> 0xe -> 0x10 -> {0x12|0x31|0x33},
// one hop per stage once now - stamp >= hold. Returns the mode to queue
// via Machine::Queue (00465d50 semantics), or -1 to stay.
struct Chain {
  int stage = 0;  // +0x450
  int stamp = -1; // +0x470 (game clock ms)

  int Step(int mode, int now, int hold, const ChainInput &in) {
    int next = -1;
    switch (mode) {
      case 9:
        if (stage < 1) { stage = 1; stamp = now; }
        else if (stage != 1) return -1;
        if (now - stamp >= hold) { stage = 2; next = 10; }
        return next;
      case 10:
      case 11:
        if (stage < 3) { stage = 3; stamp = now; }
        else if (stage != 3) return -1;
        if (now - stamp >= hold) { stage = 4; next = 12; }
        return next;
      case 12:
      case 13:
        if (stage < 5) { stage = 5; stamp = now; }
        else if (stage != 5) return -1;
        if (now - stamp >= hold) { stage = 6; next = 14; }
        return next;
      case 14:
      case 15:
        if (stage < 7) { stage = 7; stamp = now; }
        else if (stage != 7) return -1;
        if (now - stamp >= hold) { stage = 8; next = 16; }
        return next;
      case 16:
      case 17:
        if (stage < 9) { stage = 9; stamp = now; }
        else if (stage != 9) return -1;
        if (now - stamp >= hold || in.holdOff != 2) {
          stage = 10;
          next = in.bottom ? 0x33 : (in.alt ? 0x31 : 0x12);
        }
        return next;
      default:
        return -1;
    }
  }
};

// Round-end mode sets (all just 00464470(0) mode tests).
inline bool IsRoundEndMode(int m) {  // 00476180
  return m == 0x18 || m == 0x24 || m == 0x27 || m == 0x28;
}
inline bool IsLiftMode(int m) {  // 00476160
  return m == 0x25 || m == 0x26;
}
inline bool IsSitTransition(int m) {  // 004761d0
  return m == 0x15 || m == 0x16 || m == 0x17;
}

// End timer (0063ca80): 00650fe0(&0074c150, 0, 2000 or 0, 0x8fc,
// &007c895c, 0xb) -> 2300 ms hold ramping 0 -> 2000 before Results.
inline constexpr int kEndTimerMs = 2300;  // 0x8fc
inline constexpr int kEndRampMs = 2000;   // ramp target (0 when net busy)

}  // namespace timeline
