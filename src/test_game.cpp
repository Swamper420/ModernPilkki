// Smallest assert: clamps, drill edge, bite->catch, empty-lake safe.
#include <cassert>
#include <cstdio>

#include "game.hpp"
#include "input.hpp"

static ves::Grid MkGrid(uint8_t cell) {
  ves::Grid g;
  g.w = 8;
  g.h = 8;
  g.cells.assign(64, cell);
  g.pal.assign(768, 128);
  return g;
}

int main() {
  // No lake: safe, never bites.
  {
    game::Game g;
    g.Seed(7);
    g.SetLake(nullptr, nullptr, nullptr);
    input::Keys k;
    g.Update(k, 0.016f);
    assert(g.catches == 0 && !g.Biting() && g.fishes.empty());
    input::Set(k, 6, true);
    g.Update(k, 0.016f);
    assert(g.holes.size() == 1);  // drill works without lake
    g.ForceBite();
    g.Update(k, 0.5f);
    assert(g.catches == 0 && !g.Biting());
  }
  // Movement clamps to +-128.
  {
    game::Game g;
    g.Seed(1);
    input::Keys k;
    input::Set(k, 3, true);
    g.x = 127;
    g.Update(k, 5.f);
    assert(g.x == 128.f);
    input::Keys k2;
    input::Set(k2, 2, true);
    g.x = -127;
    g.Update(k2, 5.f);
    assert(g.x == -128.f);
    // Line depth clamps 0..30.
    g.lineDepth = 30;
    input::Keys k3;
    input::Set(k3, 5, true);
    g.Update(k3, 5.f);
    assert(g.lineDepth == 30.f);
  }
  // Drill edge: hold != repeat, dupe suppressed, move + drill adds.
  {
    game::Game g;
    g.Seed(2);
    input::Keys k;
    input::Set(k, 6, true);
    g.Update(k, 0.016f);
    assert(g.holes.size() == 1 && g.justDrilled);
    g.Update(k, 0.016f);  // held: no repeat
    assert(g.holes.size() == 1 && !g.justDrilled);
    input::Set(k, 6, false);
    g.Update(k, 0.016f);
    input::Set(k, 6, true);
    g.Update(k, 0.016f);  // same spot: dupe suppressed
    assert(g.holes.size() == 1);
    input::Set(k, 6, false);
    g.Update(k, 0.016f);
    input::Keys m;
    input::Set(m, 3, true);
    g.Update(m, 1.f);  // walk away (>1.5 units)
    input::Set(m, 6, true);
    g.Update(m, 0.016f);
    assert(g.holes.size() == 2);
  }
  // Bite -> strike increments count.
  {
    game::Game g;
    g.Seed(42);
    ves::Grid d = MkGrid(200), t = MkGrid(200);
    g.SetLake(&d, &t);
    assert(g.fishes.size() == 10);
    input::Keys k;
    input::Set(k, 6, true);
    g.Update(k, 0.016f);
    assert(g.AtHole());
    input::Set(k, 6, false);
    g.Update(k, 0.016f);
    g.ForceBite();
    assert(g.Biting());
    input::Set(k, 8, true);  // strike with jig
    g.Update(k, 0.05f);
    assert(g.catches == 1 && !g.Biting() && g.totalKg > 0);
  }
  printf("game tests pass\n");
  return 0;
}
