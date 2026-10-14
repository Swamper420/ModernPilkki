// Smallest assert: TITLE->LAKE_SELECT->PLAY->RESULTS, R restart,
// lake scan safe on missing/empty dir, GAMETYPES default time, round time
// clamp min(minutes,60), the docs/sim-timeline.md §5 end sequence
// (time-up -> end-mode edge -> 2300 ms hold -> freeze -> Results), and the
// docs/sim-results.md rows (ui::Rows).
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
  // Countdown only (Update no longer jumps to Results).
  ui::Update(m, 3600);
  assert(m.timeLeft == 0 && m.state == ui::State::Play);
  ui::Press(m, 6);  // Enter while Play = nothing (lakes only from Results)
  assert(m.state == ui::State::Play);
  // §5 sequence: time up + end-mode edge -> 2300 ms hold -> freeze+Results.
  assert(!ui::EndHold(m, 1.f, false, false, false));  // no end mode yet
  assert(m.endHold < 0);
  assert(!ui::EndHold(m, 1.f, true, false, false));   // edge creates timer
  assert(m.endHold >= 0);
  assert(!ui::EndHold(m, 2.299f - 1.f, true, false, false));  // still holding
  assert(ui::EndHold(m, 0.002f, true, false, false));          // 2300 ms fire
  ui::To(m, ui::State::Results);  // host: 00483890(2,0) freeze + Results
  assert(m.state == ui::State::Results);
  ui::Press(m, 6);  // Enter from results -> lake list
  assert(m.state == ui::State::LakeSelect);
  ui::Press(m, 6);
  ui::BeginPlay(m, "B", ui::Rules{});
  assert(m.endHold < 0 && m.timeLeft == 600);  // fresh run resets the timer
  // Round time limit = min(minutes, 60) * 60000 ms.
  ui::Rules longr;
  longr.timeSec = 4000;
  ui::BeginPlay(m, "B", longr);
  assert(m.timeLeft == 3600);
  ui::BeginPlay(m, "B", ui::Rules{});
  m.timeLeft = 0.05f;
  ui::Update(m, 0.1f);
  assert(m.timeLeft == 0 && m.state == ui::State::Play);  // edge still needed
  assert(!ui::EndHold(m, 1.f, false, false, true));       // nothing yet
  // Edge-driven path: the edge alone creates the timer at time-up.
  assert(!ui::EndHold(m, 0.1f, false, true, false));
  assert(m.endHold >= 0);
  // 004749b0 pending (live fight) blocks the fire, not the timer.
  assert(!ui::EndHold(m, 2.29f, true, false, true));
  assert(ui::EndHold(m, 0.02f, true, false, false));
  ui::To(m, ui::State::Results);
  ui::Press(m, ui::kRestart);  // R flags restart, main resets sim
  assert(m.needRestart && m.state == ui::State::Results);
  m.needRestart = false;  // main acted: BeginPlay same lake
  ui::BeginPlay(m, m.lakeName, m.rules);
  assert(m.state == ui::State::Play && m.timeLeft == 600);
  ui::Press(m, 7);  // Play -> Pause -> resume
  assert(m.state == ui::State::Pause);
  ui::Update(m, 3600);  // paused: timer frozen
  assert(m.state == ui::State::Pause && m.timeLeft == 600);
  assert(!ui::EndHold(m, 1.f, true, true, false));  // pause holds the end too
  ui::Press(m, 7);
  assert(m.state == ui::State::Play);
  m.timeLeft = 0;
  ui::Update(m, 0.01f);
  assert(ui::EndHold(m, 2.31f, true, false, false));
  ui::To(m, ui::State::Results);
  ui::Press(m, 7);  // Results Esc quits
  assert(m.quit);

  // docs/sim-results.md rows: tallies -> verified row ids, Finnish labels.
  {
    ui::Hud h;
    h.catches = 3;
    h.kg = 2.5;
    h.biggest = 1.25;
    h.holes = 2;
    h.bites = 0;
    auto rows = ui::Rows(h);
    assert(rows.size() == 4);  // total, biggest, "No bites.", Initial drill
    assert(rows[0].id == results::kRowTotalWeight &&
           rows[0].valueG == 2500.0);
    assert(rows[1].id == results::kRowBiggest && rows[1].valueG == 1250.0);
    assert(rows[2].id == results::kRowNoBites && rows[2].valueG < 0);
    assert(rows[3].id == results::kRowInitialDrill);
    assert(ui::RowText(rows[0]) == "Kalojen yhteispaino: 2.5 kg");
    assert(ui::RowText(rows[2]) == "No bites.");
    h.bites = 4;
    rows = ui::Rows(h);
    assert(rows.size() == 4);  // + bite flag 0xad, no "No bites."
    assert(rows[2].id == results::kRowBiteFlag);
    assert(rows[3].id == results::kRowInitialDrill && rows[3].valueG < 0);
    h.holes = 0;
    rows = ui::Rows(h);
    assert(rows.size() == 3);  // Initial drill "as available" only
    h.catches = 0;
    h.kg = h.biggest = 0;
    rows = ui::Rows(h);
    assert(rows[0].valueG == 0 && rows[1].valueG == 0);
  }
  printf("ui tests pass\n");
  return 0;
}
