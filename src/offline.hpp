// Offline competition: bot roster + agents + leaderboard + catch log.
// Roster/chat parsed from botinfo.dat decoded text (NAME:/FI:/EN: lines,
// cf. tools/botinfo.py, FUN_0059b070). No sockets here: netplay stubbed
// disabled, offline is default (main.cpp never opens a socket).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "botinfo.hpp"

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

// Comp::Tick runs per frame (~60 fps), so AI durations convert to ticks.
inline int BotTicks(int ms) { return (int)((double)ms / (1000.0 / 60.0) + 0.5); }

struct Bot {
  std::string name;
  float skill = 0.5f;  // 0..1 catch size/rate variance
  float x = 0, y = 0;  // wander pos, -1..1 world
  int state = 0;       // 0 wander, 1 drill, 2 catch flash
  int t = 0;           // ticks to next state change
  int score = 0;       // total grams
  int count = 0;
  std::string bubble;  // current chat bubble
  int bubbleTtl = 0;
  int Skill10() const {  // AI skill 0..10 from the 0..1 variance
    int s = (int)(skill * 10 + 0.5f);
    return s < 0 ? 0 : (s > 10 ? 10 : s);
  }
};

// One bot tick. Returns catch grams (0 = none).
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
    const ChatLine *pick = nullptr;
    for (auto &c : chat)
      if (c.name == b.name) {
        pick = &c;
        break;
      }
    if (!pick && !chat.empty()) {
      std::uniform_int_distribution<size_t> ci(0, chat.size() - 1);
      pick = &chat[ci(rng)];
    }
    b.bubble = pick ? b.name + ": " + pick->Text() : b.name + ": kala!";
    b.bubbleTtl = 180;
    b.state = 2;  // catch flash, falls through to wander next tick
    return grams;
  }
  return 0;
}

struct Comp {
  std::vector<Bot> bots;
  std::vector<ChatLine> chat;
  int playerScore = 0, playerCount = 0;
  int countdown = 0;  // ticks remaining; 0 = no limit / finished
  std::mt19937 rng{12345};
  std::vector<Catch> log;

  void Init(const std::vector<std::string> &names,
            const std::vector<ChatLine> &cl, int nBots, int countTicks,
            uint32_t seed) {
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
      bots.push_back(b);
    }
    countdown = countTicks;
    log.clear();
  }

  void Tick() {
    if (countdown > 0) countdown--;
    for (size_t i = 0; i < bots.size(); i++) {
      int g = TickBot(bots[i], chat, rng);
      if (g > 0) log.push_back(Catch{(int)i, g});
    }
  }

  bool Done() const { return countdown == 0; }

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
