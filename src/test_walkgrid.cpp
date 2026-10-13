// Smallest asserts for walkgrid.hpp: the 16-case corner-mask rule
// (004af230 jump table 0x004af4d4), grid lookup, and the click-order
// decision (0047f530/00476c00). See docs/sim-walk.md.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "walkgrid.hpp"

int main() {
  using namespace walkgrid;
  // Corner-mask rule: full/passthrough and never-walkable masks.
  assert(MaskWalkable(15, 0, 0) && MaskWalkable(15, 599, 599));
  assert(!MaskWalkable(0, 300, 300));
  assert(!MaskWalkable(5, 0, 600) && !MaskWalkable(5, 300, 300));
  assert(!MaskWalkable(10, 300, 300));
  // Two/three-corner masks: straight half-cell split, strict at 300.
  assert(MaskWalkable(3, 100, 301) && !MaskWalkable(3, 100, 300));
  assert(MaskWalkable(6, 301, 100) && !MaskWalkable(6, 300, 100));
  assert(MaskWalkable(9, 299, 100) && !MaskWalkable(9, 300, 100));
  assert(MaskWalkable(12, 100, 299) && !MaskWalkable(12, 100, 300));
  // Single-corner masks: triangles at the mask corner.
  assert(MaskWalkable(8, 100, 100) && !MaskWalkable(8, 150, 150));
  assert(MaskWalkable(7, 150, 150) && !MaskWalkable(7, 100, 100));
  assert(MaskWalkable(4, 500, 100) && !MaskWalkable(4, 500, 250));
  assert(MaskWalkable(1, 100, 500) && !MaskWalkable(1, 250, 500));
  // Diagonal boundaries (mixed corner pairs), 900 split inclusive.
  assert(MaskWalkable(13, 450, 450) && !MaskWalkable(13, 451, 450));
  assert(MaskWalkable(2, 451, 450) && !MaskWalkable(2, 450, 450));
  assert(MaskWalkable(11, 500, 250) && !MaskWalkable(11, 500, 100));
  assert(MaskWalkable(14, 250, 500) && !MaskWalkable(14, 100, 500));
  // Tiny synthetic lake: 3x3 corners, corner (2,1) impassable (bit 0x10).
  // Cell (1,1) then has mask 11 (corners (1,1),(2,2),(1,2) passable).
  std::vector<uint8_t> cells(9, 0x00);
  cells[1 * 3 + 2] = 0x10;
  GridView g{cells.data(), 3, 3};
  assert(!Walkable(g, 1100, 700));  // u=500,v=100: blocked triangle
  assert(Walkable(g, 900, 900));    // u=300,v=300: passable side
  assert(Walkable(g, 100, 100));    // full-passable corner
  assert(!Walkable(g, 2000, 100));  // cell 3 off the grid: wall
  assert(!Walkable(g, -1, 50));     // negative guard
  // Click-order decisions (0047f530 / 00476c00).
  ClickOrder o = Decide(2000, 0, 0, 0, false, 1500, nullptr);
  assert(o.kind == kWalk && o.tx == 2000 && o.ty == 0);  // in-bounds far
  assert(Decide(1000, 0, 0, 0, false, 1500, nullptr).kind == kReject);  // near
  assert(Decide(1000, 0, 0, 0, true, 1500, nullptr).kind == kWalk);     // arrived
  assert(Decide(-5, 100, 0, 100, true, 1500, nullptr).kind == kReject); // negative
  // Out-of-bounds (x,y >= 0) is ACCEPTED at order entry (verified:
  // 0047f530 rejects only negative coords) and blocked by the grid later.
  o = Decide(99999, 50, 0, 50, false, 1500, nullptr);
  assert(o.kind == kWalk && o.tx == 99999);
  HoleView hv{5000, 5000, false};
  o = Decide(5000, 5000, 4000, 5000, false, 1500, &hv);
  assert(o.kind == kHoleSnap);
  assert(std::fabs(o.tx - 4910.0) < 1e-9 && std::fabs(o.ty - 5000.0) < 1e-9);
  o = Decide(5000, 5000, 5000, 5000, false, 1500, &hv, -1, 0);  // on the hole
  assert(o.kind == kHoleSnap && std::fabs(o.tx - 4910.0) < 1e-9);
  hv.busy = true;
  assert(Decide(5000, 5000, 4000, 5000, false, 1500, &hv).kind == kReject);
  printf("walkgrid tests pass\n");
  return 0;
}
