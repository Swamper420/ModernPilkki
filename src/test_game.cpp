// Integration asserts for src/game.hpp. Re-pointed to the verified module
// semantics: RNG = bite::Rng stream (11), slices (1), walk math (2), grid
// bounds + walkgrid mask (3), drill chain 6->0x12->0x13->0x18 + hole
// objects (4/5), line buckets (6), bites via bite::BuildRecord (7/12/13),
// the 0xd fight run from controller.hpp (8), weight/cap via bite::Weight
// (9), species tables via bite::SpeciesTable indexed by weight (10), bait
// grid (14), round end 2300 ms (15), tallies (16), offline AI timers (18).
// Never weakened: RNG sequence, weight/cap math, bite thresholds, species
// tables, walk math stay byte-exact.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <type_traits>
#include <vector>

#include "bite.hpp"
#include "controller.hpp"
#include "game.hpp"
#include "input.hpp"
#include "offline.hpp"
#include "tick.hpp"
#include "timeline.hpp"

static ves::Grid MkGrid(uint16_t w, uint16_t h, uint8_t cell) {
  ves::Grid g;
  g.w = w;
  g.h = h;
  g.cells.assign((size_t)w * h, cell);
  g.pal.assign(768, 128);
  return g;
}

// Drill until the line drops (chain: 1 s stance + 5 s stages + teardown).
static void DrillToHole(game::Game &g) {
  input::Keys k;
  input::Set(k, 6, true);
  g.Update(k, 1.0f);
  for (int i = 0; i < 40 && !g.LineOut(); i++) g.Update(k, 1.0f);
}

// One full bite->fight->land pass with a fixed seed; returns the run
// InitDrill built (game::Game::Fight snapshot).
struct FightRun {
  ctrl::DrillRun run;
  double w = 0;
  int catches = 0;
};
static FightRun RunFight(uint32_t seed) {
  FightRun out;
  game::Game g;
  ves::Grid d = MkGrid(8, 8, 200);
  g.Seed(seed);
  g.SetLake(&d, nullptr);
  DrillToHole(g);
  assert(g.LineOut() && g.AtHole());
  g.ForceBite();
  assert(g.Biting());
  out.w = g.ActiveBite()->W;
  input::Keys k;
  input::Set(k, 8, true);
  g.Update(k, 0.016f);
  input::Set(k, 8, false);  // release: no 3 s forceRise abort
  assert(g.Fighting());
  // the run resolves after its step chain (segments + final ease), which
  // scales with the fish: minutes for heavy records — give it room.
  for (int i = 0; i < 1500 && g.Fighting(); i++) g.Update(k, 1.0f);
  assert(!g.Fighting());
  out.catches = g.catches;
  out.run = g.Fight();
  return out;
}

int main() {
  // 11: MSVC rand exact sequence for srand(0x1234) — the one shared RNG
  // (game::MsvcRand IS bite::Rng).
  static_assert(std::is_same<game::MsvcRand, bite::Rng>::value, "one RNG");
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
  assert(bite::Weight(1200, 10) == 20.0);
  assert(bite::Weight(5000, 20) == 30.0);  // capped
  assert(bite::Weight(1000, 30) == 6.25);
  assert(bite::Weight(1000, 40) == 1000.0 / 230.0);
  assert(bite::Weight(999999, 10) == 300.0);  // 10 m cap
  assert(bite::Weight(600, 10) == 10.0);
  // 8: catch/fail bands by v vs the line rule's u32 (record +0x30/+0x38).
  {
    double cp, fp;
    bite::Bands(bite::LineRuleFor(10), 149, cp, fp);
    assert(cp == 0.7 && fp == 0.1);
    bite::Bands(bite::LineRuleFor(10), 150, cp, fp);
    assert(cp == 0.9 && fp == 0.4);
    bite::Bands(bite::LineRuleFor(10), 300, cp, fp);
    assert(cp == 0.95 && fp == 0.5);
  }
  // 7: bite attempt thresholds on the RECORD WEIGHT grams (00479700,
  // ctrl::BiteRoll): rec==0 -> 50%, else 125/250/500/always.
  {
    auto oracle = [](int recW, int roll) {
      if (recW < 0) return roll <= 500;
      if (recW >= 1700) return true;
      int thr = (recW < 500) ? 125 : (recW < 1000) ? 250 : 500;
      return roll <= thr;
    };
    const int weights[] = {-1, 499, 500, 999, 1000, 1699, 1700, 5000};
    for (int wi = 0; wi < 8; wi++) {
      int w = weights[wi];
      ctrl::MsvcRand a(777), b(777);
      for (int i = 0; i < 200; i++) {
        int roll = b.rand(1000);
        bool got = ctrl::BiteRoll(a, w >= 0, w >= 0 ? w : 0);
        assert(got == oracle(w, roll));
      }
    }
  }
  // 10: species tables (004acc80), indexed by weight v — the old
  // depth-indexed tables were wrong (docs/sim-bite.md 4.6).
  {
    const int *t = bite::SpeciesTable(2, 10);  // v < 30
    assert(t[0] == 1 && t[3] == 2 && t[4] == 2 && t[7] == 8);
    t = bite::SpeciesTable(2, 100);  // v < 200
    assert(t[2] == 2 && t[4] == 3 && t[7] == 8);
    t = bite::SpeciesTable(2, 500);  // else
    assert(t[0] == 6 && t[1] == 7 && t[7] == 2);
    t = bite::SpeciesTable(3, 0);  // type 3 (type 5 shares)
    assert(t[0] == 6 && t[4] == 7 && t[6] == 4 && t[7] == 4);
    t = bite::SpeciesTable(5, 999);
    assert(t[0] == 6 && t[7] == 4);
    t = bite::SpeciesTable(0xf, 0);
    assert(t[0] == 3 && t[1] == 4 && t[5] == 9 && t[7] == 8);
    t = bite::SpeciesTable(0x16, 0);
    assert(t[1] == 9 && t[4] == 9 && t[7] == 8);
  }
  // Record +0x20 timing ramp: 300 < v <= 1000 ramps, top at v >= 1001.
  assert(bite::Timing32(1001) == 0.16666);
  assert(bite::Timing32(1000) == 0.05 + 700 * 0.00016665714285714285);
  assert(bite::Timing32(500) == 0.05 + 200 * 0.00016665714285714285);
  assert(bite::Timing32(300) == 0.05);
  assert(bite::Timing32(9) == 0.05);
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
    // Target (x0+2000 = 4400) is past the walkable bound 4200: the
    // original accepts it (0047f530 rejects only negative coords) and
    // the walk is stopped by the walk grid, never rejected (sim-walk 1).
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
    g2.Click(g2.x + 4000, g2.y);  // OOB target accepted (0047f530)
    input::Keys ke;
    tick::Slices(2500, [&](uint32_t ms) { g2.Update(ke, ms / 1000.f); });
    assert(std::fabs(g2.x - (xa + 0.34 * 2500.0)) < 1e-6);
    // 3: grid bounds stop the walk at (w-1)*600.
    while (g2.Walking()) g2.Update(ke, 1.f);
    // wall at the walkable bound (w-1)*600 = 4200; last legal step kept
    assert(g2.x <= 4200.0 && g2.x > 3700.0);
  }
  // 3: walkgrid corner-mask path (004af230) when a grid is loaded.
  {
    game::Game g;
    ves::Grid d = MkGrid(4, 2, 100);
    g.Seed(1);
    g.SetLake(&d, nullptr);
    assert(g.Walkable(100, 100));   // bounds fallback: mask 15 stand-in
    assert(!g.Walkable(-1, 100));
    static const uint8_t pass[6] = {0, 0, 0, 0, 0, 0};  // 0x10 clear
    g.SetWalkGrid(walkgrid::GridView{pass, 3, 2});
    assert(g.Walkable(100, 100));   // mask 15 -> passable
    static const uint8_t block[6] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10};
    g.SetWalkGrid(walkgrid::GridView{block, 3, 2});
    assert(!g.Walkable(100, 100));  // mask 0 -> never walkable
    static const uint8_t half[6] = {0, 0x10, 0x10, 0, 0, 0};
    g.SetWalkGrid(walkgrid::GridView{half, 3, 2});
    assert(walkgrid::MaskWalkable(9, 100, 100));  // u < 300 half-cell rule
    assert(!walkgrid::MaskWalkable(9, 500, 100));
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
  // 4/5: drill chain machine 5 -> 6 -> (9..0x10 stages) -> 0x12 -> 0x13
  // hole creation -> 0x18 sitting (line out); hole objects with id and
  // landCount; 300-unit hole exclusion; walk-away reel-in back to mode 5.
  {
    game::Game g;
    g.Seed(3);
    input::Keys k;
    g.DrillStart();  // no lake: original guard (+0x528) refuses
    assert(g.holes.empty() && g.Mode() == 5);
    ves::Grid d = MkGrid(8, 8, 200);
    g.SetLake(&d, nullptr);
    input::Set(k, 6, true);
    g.Update(k, 0.016f);  // rising edge -> mode 6 (drill stance)
    assert(g.Mode() == 6 && g.holes.empty() && !g.justDrilled);
    g.Update(k, 0.016f);  // held: no repeat
    assert(g.Mode() == 6);
    for (int i = 0; i < 40 && !g.LineOut(); i++) g.Update(k, 1.0f);
    assert(g.holes.size() == 1);
    assert(g.holes[0].open && g.holes[0].landCount == 0);
    assert((g.holes[0].id >> 16) == 36);  // cellIdx at drill point (4,4), w=8
    assert((g.holes[0].id & 0xffff) == 0);
    // 00480820 created the hole at mode 0x13; the line drops at 0x18
    // (JUST_SITTING = fishing at hole), not the old 0x25 guess.
    assert(g.Mode() == game::kModeSit);
    assert(g.LineOut() && g.AtHole());
    // round-end mode sets: sitting is in 00476180 (fishing-idle).
    assert(g.RoundEndMode());
    assert(g.EndModeEdge());   // rising edge fired when entering 0x18
    g.Update(k, 1.0f);
    assert(g.RoundEndMode() && !g.EndModeEdge());  // edge is one-shot
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
    for (auto &f : g.fishes) assert(f.x >= 0 && f.y >= 0);  // no fish logic
  }
  // 7: bite record fields through the documented synthetic fallback stock
  // (no lake file -> bite::SyntheticStock), ring state + formulas intact.
  {
    game::Game g;
    ves::Grid d = MkGrid(8, 8, 200);
    g.Seed(0x1234);
    g.SetLake(&d, nullptr);
    DrillToHole(g);
    assert(g.LineOut() && g.BiteCount() == 0);
    for (int i = 0; i < 50 && !g.Biting(); i++) g.BiteAttempt(true);
    assert(g.Biting());
    const bite::Record *r = g.ActiveBite();
    assert(r && r->species != 0);
    assert(r->seq == (uint32_t)g.BiteCount());
    assert(r->W == bite::Weight(r->V(), 10));       // line 10 m row
    assert(r->timing == bite::Timing32(r->V()));
    assert(r->V() > 0);
    assert(r->minW >= 5 && r->minW <= 84);          // rand(80)+5 (line 10)
    assert(r->rollOff < 400);
    assert(r->rollDiv >= 1 && r->rollDiv <= 5);
    assert(r->f64 >= 0 && r->f64 <= 100);
    assert(r->outcome <= 3 && r->outcome2 == r->outcome);
    assert(r->x == (int)g.x && r->y == (int)g.y);
    g.Reel();
    assert(!g.LineOut() && g.Mode() == 5);
  }
  // 8/14/16: the 0xd fight run (InitDrill/DrillStepAdvance/DrillEnd)
  // resolves the bite: verified run timings, deterministic per seed, then
  // land: tally, bait consume, per-hole counter.
  {
    game::Game g;
    ves::Grid d = MkGrid(8, 8, 200);
    g.Seed(0x1234);
    g.SetLake(&d, nullptr);
    DrillToHole(g);
    assert(g.LineOut() && g.AtHole());
    int baitBefore = g.bait[0][0];
    g.ForceBite();
    assert(g.Biting());
    const bite::Record *r = g.ActiveBite();
    assert(r->catchP == 1.0 && r->failP == 0.0);  // forced catch record
    double w = r->W;
    input::Keys k;
    input::Set(k, 8, true);
    g.Update(k, 0.016f);  // strike -> 0xd fight
    input::Set(k, 8, false);
    assert(g.Fighting() && g.Biting());
    assert(g.Mode() == game::kModeLift);  // 00477880 -> NOSTO lift
    g.Update(k, 0.2f);  // the run's init frame
    const ctrl::DrillRun run = g.Fight();
    double runMs = (w * 0.5 + 0.8) * (double)run.power + w * 70.0;
    int expectTicks = (int)(runMs * 0.04 + 0.5);  // 7bcc90 formula
    assert(run.stepMs == run.stepTicks * 200);
    assert(run.stepTicks >= 13 && run.stepTicks <= 16);  // (rand(4)+13)
    assert(run.power >= 0x46);                 // max(depth, 70)
    assert(run.count == expectTicks / run.stepTicks + 1);
    // snapshot is taken on the init frame, after its 200 ms tick
    assert(run.ticks == expectTicks || run.ticks == expectTicks - 1);
    assert(run.stop >= 0 && run.stop <= expectTicks);
    if (run.ticks != run.stop) {  // run still alive on the snapshot frame
      assert(!run.depth.empty());
      assert(run.depth[0] == run.power);  // array[0] = +0x414 at entry
    }
    for (int i = 0; i < 1500 && g.Fighting(); i++) g.Update(k, 1.0f);
    assert(!g.Fighting() && g.catches == 1 && !g.Biting());
    assert(g.LastFightEnd() == ctrl::FightEnd::Catch ||   // 00473280
           g.LastFightEnd() == ctrl::FightEnd::Normal);   // 00473320
    assert(g.lastKg > 0 && g.lastKg <= 300.0);  // cap bound
    assert(g.totalKg == g.lastKg && g.biggest == g.lastKg);
    assert(g.bait[0][0] == baitBefore - 1);  // 0058f7c0 consume
    assert(g.holes[0].landCount == 1);       // 00473320 +0x18
    assert(g.Mode() == game::kModeSit);      // BoreBegin: back to fishing
    // 14: dry lure gate: no active lure -> line retracts.
    for (int t = 0; t < 4; t++)
      for (int j = 0; j < 16; j++) g.bait[t][j] = 0;
    g.Update(k, 0.016f);
    assert(!g.ActiveLure() && !g.LineOut());
  }
  // 8: drill-run determinism (same seed, same run) — the RNG sequence and
  // the array build are byte-exact across instances.
  {
    FightRun a = RunFight(0x1234);
    FightRun b = RunFight(0x1234);
    assert(a.w == b.w && a.catches == b.catches);
    assert(a.run.stepMs == b.run.stepMs && a.run.stepTicks == b.run.stepTicks);
    assert(a.run.ticks == b.run.ticks && a.run.stop == b.run.stop);
    assert(a.run.count == b.run.count && a.run.power == b.run.power);
    assert(a.run.good == b.run.good && a.run.bad == b.run.bad);
    assert(a.run.depth == b.run.depth);
    FightRun c = RunFight(7);  // other seed -> other stream
    assert(c.run.stepMs >= 2600 && c.run.stepMs <= 3200);
    assert(c.catches == 1);
  }
  // 7: Ambient timer (rand(75000)+20000, 50% rec==0 roll) fires a bite
  // without any player action (deterministic seed).
  {
    game::Game g;
    ves::Grid d = MkGrid(8, 8, 200);
    g.Seed(9);
    g.SetLake(&d, nullptr);
    DrillToHole(g);
    assert(g.LineOut());
    bool bit = false;
    for (int i = 0; i < 700 && !bit; i++) {  // 700 s >= 7x max period
      g.Update(input::Keys{}, 1.0f);
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
    for (int i = 0; i < 40 && !g.LineOut(); i++) g.Update(k, 1.0f);
    double hx, hy;
    assert(g.ActiveHole(hx, hy));
    g.Click(g.x - 2000, g.y);  // start a long walk
    g.Update(k, 0.016f);
    double xm = g.x;
    // Small click clear of the 300-unit hole snap ring (004aeee0) so it
    // hits the 1500-unit minimum-click gate (00799154) and is ignored.
    g.Click(xm - 100, g.y - 300);
    g.Update(k, 0.016f);
    assert(g.x < xm - 5.0);  // kept walking to the old target
    g.Click(hx, hy);          // click on the hole: snap + walk
    for (int i = 0; i < 600 && g.Walking(); i++) g.Update(k, 0.016f);
    assert(g.Arrived());
    double dx = g.x - hx, dy = g.y - hy;
    double dist = std::sqrt(dx * dx + dy * dy);
    assert(dist >= 80.0 && dist <= 700.0);  // 90 standoff, arrival 600
  }
  // 3: 004a5700 depth over 600-unit cells + walk bounds + frame bridge.
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
    assert(std::fabs(g.DepthCm(600, 0) - 1000.f) < 1e-6);  // cell 200 row
  }
  // 3b: 004a5700 decompile parity (docs/sim.md). Saddle cell (0,0) with
  // V(x,y) row-major: V00=0, V10=100, V01=100, V11=0 (cell (2,2)=77).
  // Expected values from the decompiled expression, ops in its exact order
  // (values -> double, truncate at the end):
  //   tx<ty : (V3-V2)*(1-ty) + ((V2-V1)*(1-tx) + V1)   V1=(x1,y1),V2=(x0,y1),V3=(x0,y0)
  //   tx>=ty: (V3-V2)*ty      + ((V2-V1)*tx      + V1)   V1=(x0,y0),V2=(x1,y0),V3=(x1,y1)
  {
    game::Game g;
    ves::Grid d = MkGrid(3, 3, 0);
    d.cells[1] = 100;  // V(1,0)
    d.cells[3] = 100;  // V(0,1)
    d.cells[8] = 77;   // V(2,2)
    g.Seed(1);
    g.SetLake(&d, nullptr);
    // (450,150): tx = 450/600 = 0.75 >= ty = 0.25 -> (V11-V10)*ty +
    // ((V10-V00)*tx + V00) = (0-100)*0.25 + (100*0.75 + 0) = 50. Bil
    // inear would give 62 (V00*0.5625+V10*0.1875+V01*0.1875+V11*0.0625).
    assert(g.DepthAt(450, 150) == 50);
    // flag 0: raw cell (0,0) value, no blend (004beb70 path).
    assert(g.DepthAt(450, 150, false) == 0);
    // (150,450): tx = 0.25 < ty = 0.75 -> V1=V(1,1)=0, V2=V(0,1)=100,
    // V3=V(0,0)=0: (V3-V2)*(1-ty) + ((V2-V1)*(1-tx) + V1) =
    // -100*0.25 + (100*0.75 + 0) = 50.
    assert(g.DepthAt(150, 450) == 50);
    // (100,200): tx = 1/6 < ty = 1/3 -> -100*(1-1/3) + (100*(1-1/6) + 0)
    // = 16.667 -> truncated 16 (cvttsd2si).
    assert(g.DepthAt(100, 200) == 16);
    // Exact cell corner (flag 1 too): raw cell (2,2) value (idx clamp).
    assert(g.DepthAt(1200, 1200) == 77);
    // Not walkable past (w-1)*600: 0 (004af230 gate).
    assert(g.DepthAt(1300, 1200) == 0);
    assert(g.DepthAt(-600, 0) == 0);
  }
  // Real stock wiring: the .ves DATA1 populations when the file exists,
  // the documented stand-in when it does not — bites work either way and
  // missing files never crash.
  {
    game::Game g;
    ves::Grid d = MkGrid(8, 8, 200);
    g.Seed(3);
    g.SetLake(&d, nullptr, nullptr, "extracted/lakes/haapolahti.ves");
    DrillToHole(g);
    assert(g.LineOut());
    for (int i = 0; i < 500 && !g.Biting(); i++) g.BiteAttempt(true);
    assert(g.Biting());
    assert(g.ActiveBite()->W > 0);
  }
  {
    game::Game g;
    ves::Grid d = MkGrid(8, 8, 200);
    g.Seed(3);
    g.SetLake(&d, nullptr, nullptr, "/nonexistent/lake.ves",
              "/nonexistent/data0.dat");
    DrillToHole(g);
    assert(g.LineOut());
    for (int i = 0; i < 500 && !g.Biting(); i++) g.BiteAttempt(true);
    assert(g.Biting());  // degrade to the stand-in stock, never crash
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
  // 15: round-end constant (00650fe0 end timer 0x8fc = 2300 ms; 2000 is
  // the fade ramp target) + the mode sets (00476180|00476160).
  assert(game::kEndHoldMs == 2300);
  assert(game::kEndHoldMs == timeline::kEndTimerMs);
  assert(timeline::IsRoundEndMode(0x18) && timeline::IsRoundEndMode(0x24));
  assert(timeline::IsRoundEndMode(0x27) && timeline::IsRoundEndMode(0x28));
  assert(timeline::IsLiftMode(0x25) && timeline::IsLiftMode(0x26));
  assert(!timeline::IsRoundEndMode(5) && !timeline::IsLiftMode(0x1e));
  // 004875f0 walk/stand timer formula exposed for hooks.
  assert(game::Game::WalkMs(1, false) == 550);  // (23-1)*25
  assert(game::Game::WalkMs(7, false) == 400);
  assert(game::Game::WalkMs(8, false) == 400);
  assert(game::Game::WalkMs(3, true) == 1000);  // doubled stand-up
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
