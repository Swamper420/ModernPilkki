// Camera feel + daylight atmosphere. Reuses render::Camera (no second
// camera type): follow/zoom/shake/tint/sparkle only. All draw entry
// points null-safe so headless/tick-only stays safe. Boring by design:
// lerp follow, exp zoom, trauma-decay shake, fullscreen tint rect,
// deterministic snow dots (no RNG dep).
// Bindings: wheel + +/- zoom. PgUp/PgDn stay line depth (game.hpp).
#pragma once
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>

#include "render.hpp"

namespace camera {

inline constexpr float kMinZoom = 1.f, kMaxZoom = 8.f;
inline constexpr float kFollow = 6.f;    // lerp rate (/s)
inline constexpr float kZoomStep = 1.15f;  // per wheel notch / key press
inline constexpr float kPitchMax = 0.5f;   // pitch at max zoom (2.5D tilt)

// Extra per-frame camera state (shake energy 0..1).
struct Fx {
  float shake = 0;
};

// Smooth follow: exponential lerp toward target, zoom clamped 1-8.
// Pure math except struct writes (tested).
inline void Update(render::Camera &c, float tx, float ty, float dt) {
  if (dt < 0) dt = 0;
  float a = dt * kFollow;
  if (a > 1) a = 1;
  c.x += (tx - c.x) * a;
  c.y += (ty - c.y) * a;
  c.x = render::ClampF(c.x, -128.f, 128.f);
  c.y = render::ClampF(c.y, -128.f, 128.f);
  c.zoom = render::ClampF(c.zoom, kMinZoom, kMaxZoom);
}

// steps > 0 zooms in (wheel up / '+' key). Pure math (tested).
inline void Zoom(render::Camera &c, float steps) {
  c.zoom =
      render::ClampF(c.zoom * powf(kZoomStep, steps), kMinZoom, kMaxZoom);
}

// 2.5D tilt derived from zoom: 0 at zoom 1, kPitchMax at zoom 8.
// Derived, not bound (no key taken). Pure (tested).
inline float PitchForZoom(float zoom) {
  float t = (render::ClampF(zoom, kMinZoom, kMaxZoom) - kMinZoom) /
            (kMaxZoom - kMinZoom);
  return t * kPitchMax;
}

// Shake: drill/catch kick. Kick adds trauma, StepShake emits a world-unit
// offset and decays. Deterministic wobble (no RNG). Tested.
inline void Kick(Fx &f, float mag) {
  f.shake = render::ClampF(f.shake + mag, 0.f, 1.f);
}

inline void StepShake(Fx &f, float dt, float &dx, float &dy) {
  if (dt < 0) dt = 0;
  float s = render::ClampF(f.shake, 0.f, 1.f);
  dx = s * s * 1.5f * sinf(s * 40.f);
  dy = s * s * 1.5f * cosf(s * 31.f);
  f.shake = s > dt * 2 ? s - dt * 2 : 0;
}

// Mean of objects.dat Daylight column (0/1 per row), 0..1; 1 when unknown.
// Gating strength for the tint overlay.
inline float DaylightAvg(const objects::ObjTable *tab) {
  if (!tab || tab->rows.empty()) return 1.f;
  double sum = 0;
  for (auto &r : tab->rows) sum += r.daylight ? 1 : 0;
  return render::ClampF((float)(sum / tab->rows.size()), 0.f, 1.f);
}

// Dawn->noon->dusk tint from match clock: alpha peaks at the ends (dawn
// warm, dusk orange), 0 at midday. Scaled by daylight. Alpha <= 80.
// Pure math (tested).
inline void TintColor(float timeLeft, float timeSec, float daylight,
                      uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a) {
  float t = timeSec > 0 ? timeLeft / timeSec : 0.5f;
  t = render::ClampF(t, 0.f, 1.f);
  float edge = fabsf(t - 0.5f) * 2;  // 0 noon .. 1 ends
  float al = edge * 80.f * render::ClampF(daylight, 0.f, 1.f);
  a = (uint8_t)(al > 255 ? 255 : al);
  if (t >= 0.5f) {
    r = 255;
    g = 210;
    b = 170;
  } else {
    r = 255;
    g = 150;
    b = 120;
  }
}

// Fullscreen tint overlay. No-op false when r null; true otherwise
// (true with alpha 0 = nothing to draw, still success).
inline bool DrawTint(SDL_Renderer *r, int winW, int winH, float timeLeft,
                     float timeSec, float daylight) {
  if (!r) return false;
  uint8_t cr, cg, cb, ca;
  TintColor(timeLeft, timeSec, daylight, cr, cg, cb, ca);
  if (!ca) return true;
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
  SDL_FRect d{0, 0, (float)winW, (float)winH};
  SDL_RenderFillRect(r, &d);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  return true;
}

// Snow sparkle: deterministic white dots when the SNOW block is active.
// frame = main loop counter. No-op false when r null or snow off.
inline bool Sparkle(SDL_Renderer *r, int winW, int winH, int frame,
                    bool snow) {
  if (!r || !snow || winW <= 0 || winH <= 0) return false;
  SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
  for (int i = 0; i < 60; i++) {
    uint32_t h = (uint32_t)i * 2654435761u + (uint32_t)frame * 97u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    if ((h + (uint32_t)i) % 3 == 0) continue;  // twinkle skip
    float sx = (float)(h % (uint32_t)winW);
    float sy = (float)((h >> 11) % (uint32_t)winH);
    SDL_FRect d{sx, sy, 2, 2};
    SDL_RenderFillRect(r, &d);
  }
  return true;
}

}  // namespace camera
