// Smallest assert: TITLE->LAKE_SELECT->PLAY->RESULTS, R restart,
// lake scan safe on missing/empty dir, GAMETYPES default time.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "ui.hpp"

int main() {
  assert(ui::ScanLakes("/nonexistent-dir-xyz").empty());
  auto tmp = std::filesystem::temp_directory_path() / "mp_uitest_empty";
  std::filesystem::remove_all(tmp);
  std::filesystem::create_directories(tmp);
  assert(ui::ScanLakes(tmp.string()).empty());
  std::filesystem::remove_all(tmp);

  uint8_t gt[100] = {};
  gt[0] = gt[1] = 1;
  memset(gt + 7, 'c', sizeof(gt) - 7);
  ui::Rules r = ui::ParseGametypes(gt, sizeof gt);
  assert(r.hasGametypes && r.flags[0] == 1 && r.flags[1] == 1 &&
         r.flags[7] == 0 && r.timeSec == 600);
  assert(!ui::ParseGametypes(nullptr, 0).hasGametypes);

  ui::Game g;
  ui::Init(g, {});
  assert(g.state == ui::State::Title);
  ui::Press(g, 6);  // no lakes: stay
  assert(g.state == ui::State::Title && !g.quit);
  ui::Press(g, 7);
  assert(g.quit);

  ui::Game m;
  ui::Init(m, {{"a.ves", "A"}, {"b.ves", "B"}});
  ui::Press(m, 6);
  assert(m.state == ui::State::LakeSelect);
  ui::Press(m, 0);  // wrap to last
  assert(m.sel == 1);
  ui::Press(m, 1);  // wrap to first
  assert(m.sel == 0);
  ui::Press(m, 6);
  assert(m.needLoad && m.pendIdx == 0);
  ui::BeginPlay(m, "A", ui::Rules{});
  assert(m.state == ui::State::Play && m.timeLeft == 600 && !m.needLoad);
  assert(m.lakes.size() == 2);  // list survives round restart
  ui::Update(m, 3600);          // timer expiry -> Results
  assert(m.state == ui::State::Results && m.timeLeft == 0);
  ui::Press(m, 6);  // Enter from results -> lake list
  assert(m.state == ui::State::LakeSelect);
  ui::Press(m, 6);
  ui::BeginPlay(m, "B", ui::Rules{});
  m.timeLeft = 0.05f;
  ui::Update(m, 0.1f);
  assert(m.state == ui::State::Results);
  ui::Press(m, ui::kRestart);  // R flags restart, main resets sim
  assert(m.needRestart && m.state == ui::State::Results);
  m.needRestart = false;  // main acted: BeginPlay same lake
  ui::BeginPlay(m, m.lakeName, m.rules);
  assert(m.state == ui::State::Play && m.timeLeft == 600);
  ui::Press(m, 7);  // Play -> Pause -> resume
  assert(m.state == ui::State::Pause);
  ui::Update(m, 3600);  // paused: timer frozen
  assert(m.state == ui::State::Pause);
  ui::Press(m, 7);
  assert(m.state == ui::State::Play);
  m.timeLeft = 0;
  ui::Update(m, 0.01f);
  ui::Press(m, 7);  // Results Esc quits
  assert(m.quit);
  printf("ui tests pass\n");
  return 0;
}
