// Offline competition: bot roster + agents + leaderboard + catch log.
// Roster/chat parsed from botinfo.dat decoded text (NAME:/FI:/EN: lines,
// cf. tools/botinfo.py, FUN_0059b070). No sockets here: netplay stubbed
// disabled, offline is default (main.cpp never opens a socket).
// Agents: each bot drives its own game::Game session through the botai
// machines (docs/sim-ai.md §6) via botai::Hooks; the toy wander/drill
// TickBot survives behind Comp::useToy as a fallback.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "botai.hpp"
#include "botinfo.hpp"
#include "game.hpp"

namespace offline {

// netplay explicitly out of scope: offline build never touches sockets.
constexpr bool kNetDisabled = true;

struct ChatLine {
  std::string name, fi, en;
  // EN column empty in shipped file; FI carries chat. Never empty.
  std::string Text() const {
    if (!en.empty()) return en;
    if (!fi.empty()) return fi;
    return "...";
  }
};

// Parse decoded botinfo text into chat lines. One entry per NAME: block
// carrying its FI:/EN: lines. NEXT:/ID:/NEW/END only delimit blocks.
inline std::vector<ChatLine> ParseScript(const std::string &text) {
  std::vector<ChatLine> out;
  ChatLine cur;
  bool inBlock = false;
  auto flush = [&] {
    if (inBlock && !cur.name.empty() && (!cur.fi.empty() || !cur.en.empty()))
      out.push_back(cur);
    cur = ChatLine{};
    inBlock = false;
  };
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t e = text.find('\n', pos);
    if (e == std::string::npos) e = text.size();
    std::string ln = text.substr(pos, e - pos);
    if (!ln.empty() && ln.back() == '\r') ln.pop_back();
    if (ln.compare(0, 5, "NAME:") == 0) {
      flush();
      cur.name = ln.substr(5);
      inBlock = true;
    } else if (ln.compare(0, 3, "FI:") == 0 && inBlock) {
      if (!cur.fi.empty()) cur.fi += " ";
      cur.fi += ln.substr(3);
    } else if (ln.compare(0, 3, "EN:") == 0 && inBlock) {
      if (!cur.en.empty()) cur.en += " ";
      cur.en += ln.substr(3);
    } else if (ln.compare(0, 4, "NEW ") == 0 || ln.compare(0, 3, "END") == 0 ||
               ln.compare(0, 3, "ID:") == 0) {
      flush();
    }
    if (e == text.size()) break;
    pos = e + 1;
  }
  flush();
  // Trim leading spaces left by "NAME: X".
  for (auto &c : out) {
    for (auto *s : {&c.name, &c.fi, &c.en}) {
      size_t a = s->find_first_not_of(" \t");
      *s = a == std::string::npos ? "" : s->substr(a);
    }
  }
  return out;
}

// Decode raw botinfo.dat bytes (still ciphered lines) into script text.
// Returns "" when file missing/corrupt; caller falls back to synthetic.
inline std::string DecodeFile(const std::vector<uint8_t> &raw) {
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos <= raw.size()) {
    size_t e = pos;
    while (e < raw.size() && raw[e] != '\n') e++;
    size_t n = e - pos;
    if (n && raw[pos + n - 1] == '\r') n--;
    std::string enc(reinterpret_cast<const char *>(raw.data() + pos), n);
    // strtok " \r\n\t" strips blank/whitespace-only lines in loader.
    bool blank = true;
    for (char c : enc)
      if (c != ' ' && c != '\t') blank = false;
    if (!blank) {
      std::string dec;
      if (!botinfo::DecodeLine(enc, dec)) return "";
      lines.push_back(dec);
    }
    if (e == raw.size()) break;
    pos = e + 1;
  }
  std::string text;
  for (auto &l : lines) text += l;
  // Strip UTF-8 BOM the encoder stored in line 0.
  if (text.compare(0, 3, "\xef\xbb\xbf") == 0) text.erase(0, 3);
  return text;
}

inline std::vector<uint8_t> ReadBytes(const char *p) {
  FILE *f = fopen(p, "rb");
  if (!f) return {};
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> d(n > 0 ? (size_t)n : 0);
  if (n > 0 && fread(d.data(), 1, (size_t)n, f) != (size_t)n) d.clear();
  fclose(f);
  return d;
}

// Runtime load: extracted/ first (dev), data/ (install), else "".
inline std::string LoadScriptText() {
  for (const char *p :
       {"extracted/data/botinfo.dat", "data/botinfo.dat"}) {
    auto raw = ReadBytes(p);
    if (raw.empty()) continue;
    std::string t = DecodeFile(raw);
    if (!t.empty()) return t;
  }
  return "";
}

inline std::vector<std::string> Roster(const std::vector<ChatLine> &chat) {
  std::vector<std::string> names;
  for (auto &c : chat) {
    bool seen = false;
    for (auto &n : names)
      if (n == c.name) seen = true;
    if (!seen) names.push_back(c.name);
  }
  // ponytail: synthetic fallback, real file supplies 12 names.
  if (names.empty())
    names = {"Pekka Pilkki", "Matti Meikalainen", "Jussi Jarvinen",
             "Kari Kala", "Lasse Laitinen", "Olli Onkija"};
  return names;
}

struct Catch {
  int bot = -1;  // -2 = player
  int grams = 0;
};

// AI_* timer formulas (docs/sim.md section 5), skill 0..10. These are the
// original durations; the full AI state machines (AI_STATE_* handlers +
// shared action queue) are not wired yet (open, delta 18/20).
inline int AIEiSyoMs(int r, int skill) {
  if (skill < 0) skill = 0;
  if (skill > 10) skill = 10;
  return ((r % (11 - skill)) - skill + 20) * 1000;
}
inline int AINoFishShortMs(int r, int skill) { return AIEiSyoMs(r, skill); }
inline int AINoFishLongMs(int r, bool lake) {
  return lake ? (r % 10000) + 60000 : (r % 5000) + 35000;
}
inline int AIBoreMs(int r) { return (r % 100 + 20) * 100; }
inline int AIKorjausMs(int skill) { return (14 - skill) * 1000; }
inline int AISyokoMs() { return 1000; }
inline int AITempMs() { return 500; }
inline int AISinkingMs() { return 3000; }

// Toy AI durations in 60 fps ticks (legacy TickBot fallback only; the
// botai machines take ms directly).
inline int BotTicks(int ms) { return (int)((double)ms / (1000.0 / 60.0) + 0.5); }

// Chat bubble pick: own name's line first, else any (FI fallback in Text).
inline const ChatLine *PickLine(const std::vector<ChatLine> &chat,
                                const std::string &name, std::mt19937 &rng) {
  for (auto &c : chat)
    if (c.name == name) return &c;
  if (!chat.empty()) {
    std::uniform_int_distribution<size_t> ci(0, chat.size() - 1);
    return &chat[ci(rng)];
  }
  return nullptr;
}

// botai::Hooks wired to one per-bot game::Game session (docs/sim-ai.md §6
// seam: the machines call the same drill/walk/strike/reel/bite primitives
// as the player). The 0xd fight is the PLAYER's mini-game — the AI
// resolves through the 00478210 two-roll instead, so Jig never starts a
// fight (004771d0 is the jig/bite-request action).
struct GameHooks : botai::Hooks {
  game::Game *g = nullptr;
  int roundLeftSec = 0;  // 004383c0 round clock readback source

  int DrillStart() override {  // 004764b0: 1 ok, 4 hole here
    g->DrillStart();
    return g->Mode() == 6 ? 1 : 4;
  }
  void BoreBegin() override { g->BoreBegin(); }  // 00477310
  void BoreEnd() override { g->BoreEnd(); }      // 00477220
  void LineStage() override { g->LineStage(); }  // 00477880 -> 0x25
  void Jig() override {                          // 004771d0
    if (!g->Biting()) g->Strike();  // rate-limited bite request, no fight
  }
  int LandFish(botai::Bot &, const botai::Bite &) override {
    return g->LandFish() ? (int)(g->lastKg * 1000.0) : 0;  // 00473320
  }
  void FishSuccess(botai::Bot &, const botai::Bite &) override {
    g->LandFish();  // 00473280 lands too (docs/sim-controller.md §2.3)
  }
  void FishEscaped(botai::Bot &, const botai::Bite &) override {
    g->FishEscaped();  // 004732d0
  }
  // 004ba5e0 poll: the session's own Ambient/strike paths make the bite;
  // the AI syoko pump reads it here and rolls the strike decision.
  bool BiteRoll(botai::Bot &, botai::Bite &bt) override {
    const bite::Record *r = g->ActiveBite();
    if (!r) return false;
    bt.type = r->type;
    bt.outcome = r->outcome;
    bt.pCatch = r->catchP;
    bt.pFail = r->failP;
    bt.weight = r->V();  // 00590c10 record weight (grams)
    return true;
  }
  bool ShallowAtDrillPoint() override {  // 00472480: depth < 30 raw cells
    return g->DepthAt(g->x, g->y) < 30.0f;
  }
  bool ModeIs(int m) override { return g->Mode() == m; }
  bool LakeModel() override { return g->GridW() > 2; }  // +0x528 has lake
  int ClockSeconds() override { return roundLeftSec; }  // 004383c0
  // 0059fbe0 quality curve is unported (docs/sim-ai.md §5 open);
  // ponytail: identity stand-in so the verified strike shape works —
  // max of skill+1 draws vs rand%100 (more draws at higher skill).
  int ActivityQuality(int r) override { return r; }
};

struct Bot {
  std::string name;
  float skill = 0.5f;  // 0..1 catch size/rate variance
  float x = 0, y = 0;  // legacy toy wander pos (TickBot fallback)
  int state = 0;       // legacy toy state (TickBot fallback)
  int t = 0;           // legacy toy ticks to next change (TickBot fallback)
  int score = 0;       // total grams
  int count = 0;
  std::string bubble;  // current chat bubble
  int bubbleTtl = 0;   // ms remaining
  int Skill10() const {  // AI skill 0..10 from the 0..1 variance
    int s = (int)(skill * 10 + 0.5f);
    return s < 0 ? 0 : (s > 10 ? 10 : s);
  }
  // botai-driven session (docs/sim-ai.md §6). ponytail: each bot owns a
  // full game::Game + its own DATA1 stock load (N lakes for N bots); share
  // one stock if load time ever matters.
  botai::Bot ai;
  std::unique_ptr<game::Game> g;
  std::unique_ptr<GameHooks> hooks;
  bool walkPending = false;  // a click-to-move order is in flight
  int aiPrevState = 0;       // entry-edge glue (FISHING timer arming)
};

// Legacy toy AI (fallback behind Comp::useToy; the botai machines are the
// default). One toy tick. Returns catch grams (0 = none).
inline int TickBot(Bot &b, const std::vector<ChatLine> &chat,
                   std::mt19937 &rng) {
  if (b.bubbleTtl > 0 && --b.bubbleTtl == 0) b.bubble.clear();
  std::uniform_real_distribution<float> mv(-0.05f, 0.05f);
  if (b.state == 0) {  // wander, then drill (AI_bore_timer2)
    b.x += mv(rng);
    b.y += mv(rng);
    if (b.x > 1) b.x = 1;
    if (b.x < -1) b.x = -1;
    if (b.y > 1) b.y = 1;
    if (b.y < -1) b.y = -1;
    if (--b.t <= 0) {
      b.state = 1;
      b.t = BotTicks(AIBoreMs((int)(rng() & 0x7fffffff)));
    }
    return 0;
  }
  if (--b.t <= 0) {  // drill done -> catch, size scales with skill
    std::uniform_int_distribution<int> g(
        20, (int)(120 + 900 * b.skill));
    int grams = g(rng);
    b.score += grams;
    b.count++;
    b.state = 0;
    // AI_ei_syo_short wait before the next bite attempt.
    b.t = BotTicks(AIEiSyoMs((int)(rng() & 0x7fffffff), b.Skill10()));
    // Chat bubble: prefer own name's line, else any (FI fallback in Text).
    const ChatLine *pick = PickLine(chat, b.name, rng);
    b.bubble = pick ? b.name + ": " + pick->Text() : b.name + ": kala!";
    b.bubbleTtl = 180 * 16;  // ~180 toy frames at 16 ms
    b.state = 2;  // catch flash, falls through to wander next tick
    return grams;
  }
  return 0;
}

struct Comp {
  std::vector<Bot> bots;
  std::vector<ChatLine> chat;
  int playerScore = 0, playerCount = 0;
  int countdown = 0;  // ms remaining; 0 = no limit / finished
  std::mt19937 rng{12345};
  std::vector<Catch> log;
  bool useToy = false;  // legacy TickBot fallback (default = botai machines)

  // Each bot gets its own game::Game session over the same lake (ponytail:
  // own DATA1 stock load per bot) and its own deterministic streams: the
  // session RNG seeded from comp seed + index and botai::MsvcRand likewise
  // — same seed in, same comp out.
  void Init(const std::vector<std::string> &names,
            const std::vector<ChatLine> &cl, int nBots, int countMs,
            uint32_t seed, const ves::Grid *depth = nullptr,
            const std::string &lakePath = std::string()) {
    chat = cl;
    rng.seed(seed);
    bots.clear();
    std::uniform_real_distribution<float> sk(0.3f, 1.0f);
    for (int i = 0; i < nBots && i < (int)names.size(); i++) {
      Bot b;
      b.name = names[i];
      b.skill = sk(rng);
      b.x = (float)i / (nBots > 1 ? nBots - 1 : 1) * 2 - 1;
      // first wander stretch uses the AI_ei_syo wait formula too
      b.t = BotTicks(AIEiSyoMs((int)(rng() & 0x7fffffff), b.Skill10()));
      b.ai.skill = b.Skill10();
      b.ai.rng.s = seed * 2654435761u + (uint32_t)i * 40503u + 1;
      b.g = std::make_unique<game::Game>();
      b.g->Seed(seed + (uint32_t)i);
      b.g->SetLake(depth, nullptr, nullptr, lakePath);
      b.hooks = std::make_unique<GameHooks>();
      b.hooks->g = b.g.get();
      bots.push_back(std::move(b));
    }
    countdown = countMs;
    log.clear();
  }

  // One frame slice per bot (dtMs <= 1000, docs/sim-ai.md §6).
  void Tick(uint32_t dtMs) {
    if (countdown > 0) countdown = std::max(0, countdown - (int)dtMs);
    for (size_t i = 0; i < bots.size(); i++) {
      if (useToy) {
        int g = TickBot(bots[i], chat, rng);
        if (g > 0) log.push_back(Catch{(int)i, g});
        continue;
      }
      TickOne(bots[i], (int)i, dtMs);
    }
  }

  bool Done() const { return countdown <= 0; }

  // Deterministic wander target (00473070-style random probe): a
  // walkable grid point >= 2000 units out (past the 1500 click minimum).
  bool WanderTarget(Bot &b, double &wx, double &wy) {
    game::Game &g = *b.g;
    double mx = (g.GridW() - 1) * game::kCell, my = (g.GridH() - 1) * game::kCell;
    for (int tries = 0; tries < 8; tries++) {
      wx = (double)b.ai.rng.Below((int)mx + 1);
      wy = (double)b.ai.rng.Below((int)my + 1);
      double dx = wx - g.x, dy = wy - g.y;
      if (dx * dx + dy * dy >= 2000.0 * 2000.0 && g.Walkable(wx, wy))
        return true;
    }
    return false;
  }

  // One botai slice: the sim session advances first (its Ambient/strike
  // paths make the bites), then the machine polls it through the hooks.
  // The driver owns only the feet + event glue — walk completion posts
  // kEvAct (arrival, 0047f740) into the shared action queue.
  void TickOne(Bot &b, int idx, uint32_t dtMs) {
    game::Game &g = *b.g;
    b.hooks->roundLeftSec = countdown / 1000;
    g.Update(input::Keys{}, dtMs / 1000.f);
    int st0 = b.ai.state;  // pre-tick state for the entry edge
    if (b.walkPending && !g.Walking()) {  // walk done -> arrival event
      b.walkPending = false;
      botai::Post(b.ai, botai::kEvAct);
    }
    if (b.ai.state == botai::kStWalking && !b.walkPending && !g.Walking() &&
        b.ai.queue.empty() && !b.ai.hasPending) {
      double wx = 0, wy = 0;
      if (WanderTarget(b, wx, wy)) {
        g.Click(wx, wy);
        b.walkPending = g.Walking();
      }
      if (!b.walkPending) botai::Post(b.ai, botai::kEvAct);  // stuck stand-in
    }
    botai::Tick(b.ai, *b.hooks, (int)dtMs);
    // FISHING entry arms the AI timer set (docs/sim-ai.md §4.3 re-arm tail
    // formulas; botai.hpp has no entry block — test_botai arms the same way).
    if (b.ai.state == botai::kStFishing && st0 != botai::kStFishing) {
      botai::Arm(b.ai, botai::kTmrEiSyo,
                 botai::AIEiSyoMs(b.ai.rng.Raw(), b.ai.skill));
      botai::Arm(b.ai, botai::kTmrNoFishLong,
                 botai::AINoFishLongMs(b.ai.rng.Below(10000), g.GridW() > 2));
      botai::Arm(b.ai, botai::kTmrSyoko, botai::AISyokoMs());
      botai::Arm(b.ai, botai::kTmrBore, botai::AIBoreMs(b.ai.rng.Raw()));
    }
    b.aiPrevState = b.ai.state;
    if (b.bubbleTtl > 0 && (b.bubbleTtl -= (int)dtMs) <= 0) b.bubble.clear();
    if (g.justCaught) {  // 00473320 landed: leaderboard gets the real weight
      int grams = (int)(g.lastKg * 1000.0);
      b.score += grams;
      b.count++;
      log.push_back(Catch{idx, grams});
      const ChatLine *pick = PickLine(chat, b.name, rng);
      b.bubble = pick ? b.name + ": " + pick->Text() : b.name + ": kala!";
      b.bubbleTtl = 3000;
    }
  }

  void AddPlayerCatch(int grams) {
    playerScore += grams;
    playerCount++;
    log.push_back(Catch{-2, grams});
  }

  // Ranked entries: player ("You") + bots, desc by score.
  std::vector<std::pair<std::string, int>> Board() const {
    std::vector<std::pair<std::string, int>> b{{"You", playerScore}};
    for (auto &bot : bots) b.push_back({bot.name, bot.score});
    for (size_t i = 1; i < b.size(); i++)
      for (size_t j = i; j > 0 && b[j].second > b[j - 1].second; j--)
        b[j].swap(b[j - 1]);
    return b;
  }

  static std::string SavePath() {
    const char *home = getenv("HOME");
    if (home && *home) return std::string(home) + "/.modernpilkki/catchlog.txt";
    return "./save/catchlog.txt";
  }

  // Append catch log. Creates parent dir, never throws.
  bool Save(const std::string &path = "") const {
    std::string p = path.empty() ? SavePath() : path;
    size_t slash = p.rfind('/');
    if (slash != std::string::npos && slash > 0) {
      std::string dir = p.substr(0, slash);
      mkdir(dir.c_str(), 0755);  // one level suffices for both paths
    }
    FILE *f = fopen(p.c_str(), "a");
    if (!f) return false;
    for (auto &c : log) {
      const char *who =
          c.bot == -2 ? "You" : (c.bot >= 0 && c.bot < (int)bots.size()
                                     ? bots[c.bot].name.c_str()
                                     : "?");
      fprintf(f, "%s %d\n", who, c.grams);
    }
    fclose(f);
    return true;
  }
};

}  // namespace offline
