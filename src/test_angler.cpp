// Smallest assert: hole rect sane, jig offset bounded, facing deadband,
// missing-sprite fallback never crashes.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "angler.hpp"

int main() {
  SDL_FRect hr = angler::HoleRect(100, 50, 1.0f);
  assert(hr.w > hr.h && hr.h > 0);  // ellipse bounds, wider than tall
  assert(hr.x < 100 && 100 < hr.x + hr.w);  // contains center
  assert(hr.y < 50 && 50 < hr.y + hr.h);
  for (float p = 0; p < 20; p += 0.25f) {
    float o = angler::JigOffset(p);
    assert(o >= -3.001f && o <= 3.001f);  // bounded bob
  }
  assert(angler::FacingUpdate(0, 5, -1) == 1);
  assert(angler::FacingUpdate(5, 0, 1) == -1);
  assert(angler::FacingUpdate(0, 0.01f, -1) == -1);  // deadband keeps
  assert(angler::FacingUpdate(0, 0, 0) == 1);

  angler::Assets a;
  angler::Load(a, nullptr);  // no renderer: no crash, no textures
  assert(!a.shad && !a.rod && a.fishFrames.empty());
  assert(!angler::DrawHole(nullptr, 0, 0, 1));
  assert(!angler::DrawAngler(nullptr, a, 0, 0, 1));
  game::Game g;
  g.Seed(1);
  render::Camera cam{0, 0, 1};
  assert(!angler::DrawOverlay(nullptr, a, cam, 0, 0, 800, 600, g, 0, 0,
                              false, 0, 0, true, 0.016f));
  angler::Free(a);  // empty free safe
  printf("angler tests pass\n");
  return 0;
}
