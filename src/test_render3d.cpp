// Smallest assert: letterbox math (aspect preserved/centered/degenerate) and
// the clear-grey factor. No SDL window or GPU device needed.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "render3d.hpp"

static bool eq(float a, float b) { return fabsf(a - b) < 1e-4f; }

int main() {
  // 16:9 target in a 4:3 window -> full width, letterbox top/bottom.
  render3d::Rect r = render3d::Letterbox(800, 600, 16.f / 9.f);
  assert(eq(r.x, 0) && eq(r.w, 800));
  assert(eq(r.h, 450) && eq(r.y, 75));

  // Square target in a 4:3 window -> full height, pillarbox left/right.
  r = render3d::Letterbox(800, 600, 1.f);
  assert(eq(r.y, 0) && eq(r.h, 600));
  assert(eq(r.w, 600) && eq(r.x, 100));

  // Exact fit -> whole window.
  r = render3d::Letterbox(1600, 900, 16.f / 9.f);
  assert(eq(r.x, 0) && eq(r.y, 0) && eq(r.w, 1600) && eq(r.h, 900));

  // Wider window than target -> pillarbox.
  r = render3d::Letterbox(1000, 500, 4.f / 3.f);
  assert(eq(r.w, 500.f * 4.f / 3.f) && eq(r.h, 500) && eq(r.x, 1000.f / 2 - r.w / 2));

  // Degenerate -> empty, no crash / no negative sizes.
  r = render3d::Letterbox(0, 0, 16.f / 9.f);
  assert(eq(r.w, 0) && eq(r.h, 0));
  r = render3d::Letterbox(800, 600, 0.f);
  assert(eq(r.w, 0) && eq(r.h, 0));
  r = render3d::Letterbox(-5, 600, 1.f);
  assert(eq(r.w, 0) && eq(r.h, 0));

  assert(eq(render3d::ClearGrey(), 0.75f));

  printf("render3d tests pass\n");
  return 0;
}
