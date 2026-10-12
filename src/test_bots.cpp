// Smallest assert: 5 bots tick 600 steps no crash, leaderboard sorts,
// chat line non-empty. Synthetic only (real-file check = tools/botinfo.py).
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "offline.hpp"

int main() {
  std::string script =
      "NAME: Pekka\nFI: kylma kala!\nEN: \n\n"
      "NAME: Matti\nFI: iso ahven\nEN: big perch\nNEXT:1\n\n";
  auto chat = offline::ParseScript(script);
  assert(chat.size() == 2);
  assert(!chat[0].Text().empty() && !chat[1].Text().empty());
  assert(chat[0].Text() == "kylma kala!");  // empty EN falls back to FI
  assert(chat[1].Text() == "big perch");    // EN wins when present

  auto names = offline::Roster(chat);
  assert(names.size() >= 2);
  // Synthetic fallback when script empty.
  assert(!offline::Roster({}).empty());

  offline::Comp comp;
  std::vector<std::string> five(names.begin(), names.end());
  while (five.size() < 5) five.push_back("Bot" + std::to_string(five.size()));
  comp.Init(five, chat, 5, 600, 7);
  assert(comp.bots.size() == 5);
  for (int i = 0; i < 600; i++) comp.Tick();  // no crash, bots catch
  int total = 0;
  for (auto &b : comp.bots) {
    total += b.count;
    assert(b.x >= -1 && b.x <= 1 && b.y >= -1 && b.y <= 1);
  }
  assert(total > 0);
  assert(comp.Done());

  comp.AddPlayerCatch(100000);
  auto board = comp.Board();
  assert(board.size() == 6);
  assert(board[0].first == "You" && board[0].second == 100000);
  for (size_t i = 1; i < board.size(); i++)
    assert(board[i - 1].second >= board[i].second);  // sorted desc

  bool chatSeen = false;
  for (auto &b : comp.bots)
    if (!b.bubble.empty()) chatSeen = true;
  assert(chatSeen || !comp.log.empty());

  assert(comp.Save("/tmp/modernpilkki-test-catchlog.txt"));
  printf("bots tests pass\n");
  return 0;
}
