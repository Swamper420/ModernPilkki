// Smallest assert: glyph frame map, needle/bar math, atlas rect, and the
// no-renderer paths never crash.
#include <cassert>
#include <cstdio>

#include "hud.hpp"

int main() {
  // Frame = ASCII - 0x20 (verified on fonts/fnt.spr 181 frames).
  assert(hud::Glyph(' ', 181) == 0);
  assert(hud::Glyph('0', 181) == 16);
  assert(hud::Glyph('A', 181) == 33);
  assert(hud::Glyph('a', 181) == 65);
  assert(hud::Glyph('~', 181) == 94);
  assert(hud::Glyph(0x84, 181) == 100);  // game-font byte passes through
  assert(hud::Glyph(0xFF, 181) == (int)'.' - 0x20);  // out of sheet falls back
  assert(hud::Glyph(0, 181) == (int)'.' - 0x20);

  // Needle sits at the 10..40 m line buckets and clamps outside.
  assert(hud::NeedleFrac(10) == 0 && hud::NeedleFrac(40) == 1);
  assert(hud::NeedleFrac(25) > 0.49f && hud::NeedleFrac(25) < 0.51f);
  assert(hud::NeedleFrac(0) == 0 && hud::NeedleFrac(100) == 1);

  // Fill frame: deep = tall fill (frame 0), shallow = last frame.
  assert(hud::BarFrame(40, 7) == 0);
  assert(hud::BarFrame(0, 7) == 6);
  assert(hud::BarFrame(20, 7) == 3);
  assert(hud::BarFrame(-5, 7) == 6 && hud::BarFrame(99, 7) == 0);
  assert(hud::BarFrame(10, 1) == 0);

  hud::Font f{nullptr, 7, 8, 181};
  assert(hud::TextW(f, 1.f, "ABC") == 21.f);
  assert(hud::TextW(f, 2.f, "0123456789") == 140.f);

  // No renderer: load leaves everything empty, draw reports fallback.
  hud::Assets a;
  hud::Load(a, nullptr);
  assert(!hud::Ready(a));
  ui::Game g;
  ui::Hud h;
  assert(!hud::Draw(nullptr, 800, 600, g, h, a));
  hud::Free(a);  // empty free safe
  printf("hud tests pass\n");
  return 0;
}
