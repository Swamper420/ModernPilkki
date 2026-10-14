// Smallest assert: roster/chat/leaderboard/catch log keep working, and the
// botai-driven bots (docs/sim-ai.md §6) make deterministic progress in
// their own game sessions: drill a hole, then get a bite or reach the
// fishing state within N slices. Synthetic 8x8 lake grid (no file loads;
// the synthetic stock stands in). Legacy toy TickBot fallback still works
// behind Comp::useToy.
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "botai.hpp"
#include "offline.hpp"

static ves::Grid MkGrid() {
  ves::Grid g;
  g.w = 8;
  g.h = 8;
  g.cells.assign(64, 200);
  g.pal.assign(768, 128);
  return g;
}

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

  ves::Grid d = MkGrid();
  offline::Comp comp;
  std::vector<std::string> five(names.begin(), names.end());
  while (five.size() < 5) five.push_back("Bot" + std::to_string(five.size()));
  comp.Init(five, chat, 5, 600 * 1000, 7, &d, "");  // 600 s round
  assert(comp.bots.size() == 5);
  for (auto &b : comp.bots) assert(b.g && b.hooks && b.ai.skill >= 0);
  // 600 x 1 s slices: walk -> drill (~25 s) -> fish; the Ambient timer
  // (20..95 s) plus the AI jig cadence produce bites well within the round.
  for (int i = 0; i < 600; i++) comp.Tick(1000);
  int holes = 0, bites = 0, fished = 0, caught = 0, total = 0;
  for (auto &b : comp.bots) {
    holes += (int)b.g->holes.size();
    bites += b.g->BiteCount();
    if (b.ai.state == botai::kStFishing || b.aiPrevState == botai::kStFishing)
      fished++;
    caught += b.count;
    total += b.score;
    assert(b.score >= 0 && b.count >= 0);
  }
  printf("botai progress: holes %d bites %d fishing %d caught %d %dg\n",
         holes, bites, fished, caught, total);
  assert(holes > 0);               // seeded bots drill
  assert(bites > 0 || fished > 0);  // bite or fishing state within N slices
  assert(comp.Done());              // round clock ran out
  // Real weights: any landed catch weighs more than a toy gram sprinkle.
  for (auto &c : comp.log)
    if (c.bot >= 0) assert(c.grams > 0);

  comp.AddPlayerCatch(100000000);  // 100 t: unmistakably top of the board
  auto board = comp.Board();
  assert(board.size() == 6);
  assert(board[0].first == "You" && board[0].second == 100000000);
  for (size_t i = 1; i < board.size(); i++)
    assert(board[i - 1].second >= board[i].second);  // sorted desc

  bool chatSeen = false;
  for (auto &b : comp.bots)
    if (!b.bubble.empty()) chatSeen = true;
  assert(chatSeen || !comp.log.empty());

  // Per-bot determinism: same comp seed -> same runs.
  offline::Comp again;
  again.Init(five, chat, 5, 600 * 1000, 7, &d, "");
  for (int i = 0; i < 600; i++) again.Tick(1000);
  assert(again.log.size() == comp.log.size() - 1);  // minus the player row
  for (size_t i = 0; i < again.bots.size(); i++)
    assert(again.bots[i].score == comp.bots[i].score);

  // Legacy toy fallback (flag) still catches.
  offline::Comp toy;
  toy.useToy = true;
  toy.Init(five, chat, 5, 6000, 7);
  for (int i = 0; i < 6000; i++) toy.Tick(16);
  int toyTotal = 0;
  for (auto &b : toy.bots) toyTotal += b.count;
  assert(toyTotal > 0);

  assert(comp.Save("/tmp/modernpilkki-test-catchlog.txt"));
  printf("bots tests pass\n");
  return 0;
}
