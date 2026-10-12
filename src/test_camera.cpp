// Smallest assert: follow converges, zoom clamps 1-8, pitch derived,
// shake kicks then decays, tint alpha sane, null-renderer draws safe.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "camera.hpp"

int main() {
  // Follow converges: 60 steps at 60fps from (0,0) toward (100,-50).
  render::Camera c;
  for (int i = 0; i < 600; i++) camera::Update(c, 100, -50, 1 / 60.f);
  assert(fabs(c.x - 100) < 1 && fabs(c.y + 50) < 1);
  // Negative dt safe, clamps to world.
  camera::Update(c, 9999, -9999, -1);
  assert(c.x <= 128 && c.y >= -128);

  // Zoom clamps 1-8 both ways.
  render::Camera z{0, 0, 1, 0};
  camera::Zoom(z, 100);
  assert(z.zoom == 8);
  camera::Zoom(z, -100);
  assert(z.zoom == 1);
  camera::Update(z, 0, 0, 0);  // Update also clamps
  assert(z.zoom >= 1 && z.zoom <= 8);

  // Pitch derived from zoom: 0 flat, max at 8, monotonic.
  assert(camera::PitchForZoom(1) == 0);
  assert(fabs(camera::PitchForZoom(8) - camera::kPitchMax) < 1e-6);
  assert(camera::PitchForZoom(4) > camera::PitchForZoom(2));

  // Shake: kick nonzero, then decays to zero.
  camera::Fx f;
  float dx, dy;
  camera::StepShake(f, 0.016f, dx, dy);
  assert(dx == 0 && dy == 0);
  camera::Kick(f, 0.5f);
  camera::StepShake(f, 0.016f, dx, dy);
  assert(fabs(dx) + fabs(dy) > 0);
  camera::StepShake(f, 10, dx, dy);
  assert(f.shake == 0);

  // Tint alpha 0-255 sane, capped small; noon ~0, ends > 0.
  uint8_t r, g, b, a, a0, a1;
  camera::TintColor(600, 600, 1, r, g, b, a0);  // start = dawn
  camera::TintColor(300, 600, 1, r, g, b, a);   // midday
  camera::TintColor(0, 600, 1, r, g, b, a1);    // end = dusk
  assert(a == 0 && a0 > 0 && a0 <= 80 && a1 > 0 && a1 <= 80);
  camera::TintColor(0, 600, 0, r, g, b, a);  // daylight 0 kills tint
  assert(a == 0);
  assert(camera::DaylightAvg(nullptr) == 1);

  // Null-renderer draws safe (headless).
  assert(!camera::DrawTint(nullptr, 800, 600, 0, 600, 1));
  assert(!camera::Sparkle(nullptr, 800, 600, 0, true));
  assert(!camera::Sparkle(nullptr, 800, 600, 0, false));

  printf("camera tests pass\n");
  return 0;
}
