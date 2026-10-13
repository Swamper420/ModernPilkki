// Smallest asserts per docs/sim.md delta: RNG sequence (11), slice
// boundaries (1), click-walk math (2), 600-grid bilinear + bounds (3),
// drill mode machine + hole objects (4/5), line buckets (6), ambient +
// probabilistic catch (7/8), weight (9), species tables (10), no fish
// logic in dots (12/13), bait grid (14), tally (16), AI timers (18).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "game.hpp"
#include "input.hpp"
#include "offline.hpp"
#include "tick.hpp"

static ves::Grid MkGrid(uint16_t w, uint16_t h, uint8_t cell) {
  ves::Grid g;
  g.w = w;
  g.h = h;
  g.cells.assign((size_t)w * h, cell);
  g.pal.assign(768, 128);
  return g;
}

int main() {
  // 11: MSVC rand exact sequence for srand(0x1234).
  {
    game::MsvcRand r;
    r.Seed(0x1234);
    const uint32_t expect[10] = {15256, 25789, 4789,  5383,  4116,
                                 25814, 28931, 22672, 15197, 19848};
    for (int i = 0; i < 10; i++) assert(r.Rand() == expect[i]);
    r.Seed(0x1234);
    const int em[5] = {787, 164, 787, 691, 605};  // rand(1000) values
    for (int i = 0; i < 5; i++) assert(r.RandMax(1000) == em[i]);
    assert(r.RandMax(0) == 0);
    assert(r.RandMax(1) == 0);
    assert(r.RandMax(7) >= 0 && r.RandMax(7) < 7);
  }
  // 9: W = min(v * k_line, cap_line), k = 1/60, 1/115, 1/160, 1/230.
  assert(game::Game::WeightG(1200, 0) == 20.0);
  assert(game::Game::WeightG(5000, 1) == 30.0);  // capped
  assert(game::Game::WeightG(1000, 2) == 6.25);
  assert(game::Game::WeightG(1000, 3) == 1000.0 / 230.0);
  assert(game::Game::WeightG(999999, 0) == 300.0);  // 10 m cap
  // 8: depth-band catch/fail probabilities + strike resolution.
  double cp, fp;
  game::Game::CatchBand(149.9, cp, fp);
  assert(cp == 0.7 && fp == 0.1);
  game::Game::CatchBand(150, cp, fp);
  assert(cp == 0.9 && fp == 0.4);
  game::Game::CatchBand(300, cp, fp);
  assert(cp == 0.95 && fp == 0.5);
  assert(game::Game::ResolveStrike(999, 0.7, 0.1) == game::kResEscape);
  assert(game::Game::ResolveStrike(1000, 0.7, 0.1) == game::kResLand);
  assert(game::Game::ResolveStrike(7000, 0.7, 0.1) == game::kResNormal);
  // 7: attempt-B thresholds by activity (125/250/500/always).
  assert(game::Game::BiteThreshold(0) == 125);
  assert(game::Game::BiteThreshold(499) == 125);
  assert(game::Game::BiteThreshold(500) == 250);
  assert(game::Game::BiteThreshold(999) == 250);
  assert(game::Game::BiteThreshold(1000) == 500);
  assert(game::Game::BiteThreshold(1699) == 500);
  assert(game::Game::BiteThreshold(1700) == -1);  // always
  // 10: species band tables.
  const int *t = game::Game::SpeciesTableFor(2, 10);
  assert(t[0] == 1 && t[3] == 2 && t[4] == 9);
  t = game::Game::SpeciesTableFor(2, 100);
  assert(t[2] == 2 && t[4] == 0 && t[7] == 3);
  t = game::Game::SpeciesTableFor(2, 500);
  assert(t[0] == 6 && t[1] == 7 && t[7] == 8);
  t = game::Game::SpeciesTableFor(3, 0);  // type 3 (type 5 shares)
  assert(t[0] == 6 && t[6] == 7 && t[7] == 7);
  t = game::Game::SpeciesTableFor(0xf, 0);
  assert(t[0] == 3 && t[1] == 4 && t[5] == 9);
  t = game::Game::SpeciesTableFor(0x16, 0);
  assert(t[1] == 9 && t[4] == 1 && t[7] == 6);
  // bite record +32 second timing value.
  assert(game::Game::Timing32(1000) == 0.16666);
  assert(game::Game::Timing32(500) == 0.05 + 200 * 0.0001666571);
  assert(game::Game::Timing32(300) == 0.05);
  // 1: slice splitting boundaries (tick::Slices feeds game::Update).
  {
    std::vector<uint32_t> sl;
    tick::Slices(0, [&](uint32_t ms) { sl.push_back(ms); });
    assert(sl.size() == 1 && sl[0] == 0);
    sl.clear();
    tick::Slices(2500, [&](uint32_t ms) { sl.push_back(ms); });
    assert(sl.size() == 3 && sl[0] == 1000 && sl[1] == 1000 && sl[2] == 500);
    sl.clear();
    tick::Slices(65000, [&](uint32_t ms) { sl.push_back(ms); });
    assert(sl.size() == 61 && sl[0] == 5000);  // catch-up slice 5000 ms
    for (auto s : sl) assert(s == 5000 || s <= 1000);
  }
  // 2: walk step math (dir * 0.34 * ms), sliced == unsliced.
  {
    game::Game g;
    ves::Grid d = MkGrid(8, 8, 200);
    g.Seed(1);
    g.SetLake(&d, nullptr);
    double x0 = g.x, y0 = g.y;
    g.Click(x0 + 2000, y0);  // due east, past min click distance 1500
    input::Keys k;
    g.Update(k, 0.5f);  // 500 ms * 0.34 = 170 units
    assert(std::fabs(g.x - (x0 + 170.0)) < 1e-9);
    assert(std::fabs(g.y - y0) < 1e-9);
    // Slices of the same 2500 ms walk as one 2500 ms step.
    game::Game g2;
    g2.Seed(1);
    g2.SetLake(&d, nullptr);
    double xa = g2.x;
    g2.Click(g2.x + 4000, g2.y);
    input::Keys ke;
    tick::Slices(2500, [&](uint32_t ms) { g2.Update(ke, ms / 1000.f); });
    assert(std::fabs(g2.x - (xa + 0.34 * 2500.0)) < 1e-6);
    // 3: grid bounds stop the walk at (w-1)*600.
    while (g2.Walking()) g2.Update(ke, 1.f);
    // wall at the walkable bound (w-1)*600 = 4200; last legal step kept
    assert(g2.x <= 4200.0 && g2.x > 3700.0);
  }
  // 6: line depth quantisation {10,20,30,40} via slots + PgUp/PgDn edges.
  {
    game::Game g;
    input::Keys k;
    g.SetSlot(0);
    assert(g.lineDepth == 10);
    g.SetSlot(2);
    assert(g.lineDepth == 30);
    input::Set(k, 5, true);  // PgDn edge -> deeper
    g.Update(k, 0.016f);
    g.Update(k, 0.016f);  // held: no repeat
    assert(g.lineDepth == 40);
    input::Set(k, 5, false);
    g.Update(k, 0.016f);
    input::Set(k, 5, true);
    g.Update(k, 0.016f);  // clamped at 40
    assert(g.lineDepth == 40);
    input::Set(k, 5, false);
    g.Update(k, 0.016f);
    for (int i = 0; i < 10; i++) {  // PgUp steps down to 10, clamped
      input::Set(k, 4, true);
      g.Update(k, 0.016f);
      input::Set(k, 4, false);
      g.Update(k, 0.016f);
    }
    assert(g.lineDepth == 10);
  }
  // 4/5: drill mode machine 5 -> 6 -> hole(0x13) -> 0x18 -> 0x27 -> 0x25
  // -> 0x1e, hole objects with id/landCount, 300-unit hole exclusion,
  // walk-away reel-in back to mode 5.
  {
    game::Game g;
    g.Seed(3);
    input::Keys k;
    g.DrillStart();  // no lake: original guard (+0x528) refuses
    assert(g.holes.empty() && g.Mode() == 5);
    ves::Grid d = MkGrid(8, 8, 200);
    g.SetLake(&d, nullptr);
    input::Set(k, 6, true);
    g.Update(k, 0.016f);  // rising edge -> mode 6, hole not created yet
    assert(g.Mode() == 6 && g.holes.empty() && !g.justDrilled);
    g.Update(k, 0.016f);  // held: no repeat
    assert(g.Mode() == 6);
    for (int i = 0; i < 200 && !g.LineOut(); i++) g.Update(k, 0.016f);
    assert(g.holes.size() == 1);
    assert(g.holes[0].open && g.holes[0].landCount == 0);
    assert((g.holes[0].id >> 16) == 36);  // cellIdx at spawn (4,4), w=8
    assert((g.holes[0].id & 0xffff) == 0);
    assert(g.Mode() == game::kModeAtHoleA);  // 0x1e at-hole pose
    assert(g.LineOut() && g.AtHole());
    // Reel-in: walk away -> line in, mode 5 again.
    input::Keys m;
    input::Set(m, 3, true);
    for (int i = 0; i < 400 && g.LineOut(); i++) g.Update(m, 0.016f);
    assert(!g.LineOut() && g.Mode() == 5);
    // 300-unit hole exclusion at the drill point pos + dir*75.
    input::Set(m, 3, false);
    input::Keys e;  // face east
    input::Set(e, 3, true);
    g.Update(e, 0.016f);
    input::Set(e, 3, false);
    g.x = g.holes[0].x - 100;
    g.y = g.holes[0].y;
    g.DrillStart();  // drill point ~hole - 25: within 300, refused
    assert(g.Mode() == 5 && g.holes.size() == 1);
    g.x = g.holes[0].x - 500;  // drill point ~hole - 425: allowed
    g.DrillStart();
    assert(g.Mode() == 6);
  }
  // 12/13: no wandering-fish logic by default (dots gated off).
  {
    game::Game g;
    ves::Grid d = MkGrid(8, 8, 200);
    g.Seed(1);
    g.SetLake(&d, nullptr);
    assert(g.fishes.empty());  // original-exact default
    g.newMechanics = true;
    g.SetLake(&d, nullptr);
    assert(g.fishes.size() == 10);  // presentation extras behind the flag
  }
  // 7/8/14/16: ForceBite -> strike -> catch with bait consume, per-hole
  // counter and tally (biggest/total).
  {
    game::Game g;
    ves::Grid d = MkGrid(8, 8, 200);
    g.Seed(0x1234);
    g.SetLake(&d, nullptr);
    input::Keys k;
    input::Set(k, 6, true);
    g.Update(k, 0.016f);
    for (int i = 0; i < 200 && !g.LineOut(); i++) g.Update(k, 0.016f);
    assert(g.LineOut() && g.AtHole());
    int baitBefore = g.bait[0][0];
    g.ForceBite();
    assert(g.Biting());
    input::Set(k, 8, true);  // strike resolves the record
    g.Update(k, 0.016f);
    assert(g.catches == 1 && !g.Biting());
    assert(g.lastKg > 0 && g.lastKg <= 300.0);  // cap bound
    assert(g.totalKg == g.lastKg && g.biggest == g.lastKg);
    assert(g.bait[0][0] == baitBefore - 1);  // 0058f7c0 consume
    assert(g.holes[0].landCount == 1);       // 00473320 +0x18
    // 14: dry lure gate: no active lure -> line retracts.
    for (int t = 0; t < 4; t++)
      for (int j = 0; j < 16; j++) g.bait[t][j] = 0;
    g.Update(k, 0.016f);
    assert(!g.ActiveLure() && !g.LineOut());
  }
  // 7: Ambient timer (rand(75000)+20000, 50% roll) fires a bite without
  // any player action (deterministic seed).
  {
    game::Game g;
    ves::Grid d = MkGrid(8, 8, 200);
    g.Seed(9);
    g.SetLake(&d, nullptr);
    input::Keys k;
    input::Set(k, 6, true);
    g.Update(k, 0.016f);
    for (int i = 0; i < 200 && !g.LineOut(); i++) g.Update(k, 0.016f);
    bool bit = false;
    for (int i = 0; i < 40000 && !bit; i++) {  // 640 s >= 6x max period
      g.Update(k, 0.016f);
      bit = g.Biting();
    }
    assert(bit);
  }
  // 2: click thresholds: small click ignored mid-walk, hole snap takes
  // the 90-unit stand-off point.
  {
    game::Game g;
    ves::Grid d = MkGrid(8, 8, 200);
    g.Seed(5);
    g.SetLake(&d, nullptr);
    input::Keys k;
    input::Set(k, 6, true);
    g.Update(k, 0.016f);
    for (int i = 0; i < 200 && !g.LineOut(); i++) g.Update(k, 0.016f);
    double hx, hy;
    assert(g.ActiveHole(hx, hy));
    g.Click(g.x - 2000, g.y);  // start a long walk
    g.Update(k, 0.016f);
    double xm = g.x;
    g.Click(xm - 100, g.y);  // 100 units away, not arrived: ignored
    g.Update(k, 0.016f);
    assert(g.x < xm - 5.0);  // kept walking to the old target
    g.Click(hx, hy);          // click on the hole: snap + walk
    for (int i = 0; i < 600 && g.Walking(); i++) g.Update(k, 0.016f);
    assert(g.Arrived());
    double dx = g.x - hx, dy = g.y - hy;
    double dist = std::sqrt(dx * dx + dy * dy);
    assert(dist >= 80.0 && dist <= 700.0);  // 90 standoff, arrival 600
  }
  // 3: bilinear depth over 600-unit cells + walk bounds + frame bridge.
  {
    game::Game g;
    ves::Grid d = MkGrid(4, 2, 100);
    d.cells[0] = 0;
    d.cells[1] = 200;
    g.Seed(1);
    g.SetLake(&d, nullptr);
    assert(g.DepthAt(0, 0) == 0);
    assert(std::fabs(g.DepthAt(600, 0) - 200.0) < 1e-6);
    assert(std::fabs(g.DepthAt(300, 0) - 100.0) < 1e-6);
    assert(!g.Walkable(-1, 100));       // clamp >= 0 side
    assert(!g.Walkable(100, 1801));     // beyond (h-1)*600
    assert(g.Walkable(100, 100));
    float px = g.PortX(300);            // frame bridge roundtrip
    assert(std::fabs(g.FromPortX(px) - 300.0) < 1e-3);
    float py = g.PortY(300);
    assert(std::fabs(g.FromPortY(py) - 300.0) < 1e-3);
    assert(g.PortX(0) == -128.f && g.PortX(2400) == 128.f);
  }
  // no lake: movement + ForceBite safe, never bites, drill guarded off.
  {
    game::Game g;
    g.Seed(7);
    g.SetLake(nullptr, nullptr, nullptr);
    input::Keys k;
    g.Update(k, 0.016f);
    assert(g.catches == 0 && !g.Biting() && g.holes.empty());
    g.ForceBite();
    assert(!g.Biting());
    input::Set(k, 6, true);
    g.Update(k, 0.016f);
    assert(g.holes.empty());  // original drill guard needs a lake
  }
  // 15: verified countdown chain + round-end hold constant.
  assert(game::Game::CountdownNext(9) == 10);
  assert(game::Game::CountdownNext(10) == 0xc);
  assert(game::Game::CountdownNext(0xc) == 0xe);
  assert(game::Game::CountdownNext(0xe) == 0x10);
  assert(game::Game::CountdownNext(0x10) == 0x12);
  assert(game::Game::CountdownNext(5) == -1);
  assert(game::kEndHoldMs == 2000);
  // 18: AI_* timer formulas (offline.hpp).
  assert(offline::AIEiSyoMs(3, 5) == 18000);
  assert(offline::AIEiSyoMs(0, 0) == 20000);
  assert(offline::AIEiSyoMs(9, 10) == 10000);  // (9%1) - 10 + 20
  assert(offline::AINoFishShortMs(2, 7) == 15000);
  assert(offline::AINoFishLongMs(4999, false) == 39999);
  assert(offline::AINoFishLongMs(9999, true) == 69999);
  assert(offline::AIBoreMs(99) == 11900);
  assert(offline::AIBoreMs(0) == 2000);
  assert(offline::AIKorjausMs(4) == 10000);
  assert(offline::AISyokoMs() == 1000);
  assert(offline::AITempMs() == 500);
  assert(offline::AISinkingMs() == 3000);
  assert(offline::BotTicks(1000) == 60);
  printf("game tests pass\n");
  return 0;
}
