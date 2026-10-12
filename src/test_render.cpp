// Smallest assert: synthetic bg builds, camera clamps, headless draw safe.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "render.hpp"

int main() {
  // Synthetic 4x2 grid (same runs as test_ves) + gray palette.
  ves::Grid depth;
  depth.w = 4;
  depth.h = 2;
  depth.cells = {7, 9, 9, 3, 3, 3, 3, 3};
  depth.pal.resize(768);
  for (int i = 0; i < 256; i++) {
    depth.pal[3 * i] = (uint8_t)i;
    depth.pal[3 * i + 1] = (uint8_t)i;
    depth.pal[3 * i + 2] = (uint8_t)i;
  }
  render::Bg b = render::BuildBackground(depth, nullptr, nullptr);
  assert(b.w == 4 && b.h == 2 && b.rgb.size() == 4 * 2 * 3);
  // Bad visual blob falls back to depth grid.
  std::vector<uint8_t> junk = {1, 2, 3, 4};
  render::Bg b2 = render::BuildBackground(depth, nullptr, &junk);
  assert(b2.w == 4 && b2.h == 2);
  // Typemap tint changes at least one pixel.
  ves::Grid type = depth;
  type.cells = {0, 0, 0, 0, 1, 1, 1, 1};
  render::Bg b3 = render::BuildBackground(depth, &type, nullptr);
  assert(b3.rgb != b.rgb);

  // Camera: zoom 1 = whole bg; zoom 2 = quarter; far-out clamps inside.
  SDL_FRect s1 = render::SrcRect(render::Camera{0, 0, 1}, 4, 2);
  assert(fabs(s1.x) < 1e-3 && fabs(s1.y) < 1e-3 && fabs(s1.w - 4) < 1e-3 &&
         fabs(s1.h - 2) < 1e-3);
  SDL_FRect s2 = render::SrcRect(render::Camera{0, 0, 2}, 4, 2);
  assert(fabs(s2.w - 2) < 1e-3 && fabs(s2.h - 1) < 1e-3);
  SDL_FRect s3 = render::SrcRect(render::Camera{9999, -9999, 2}, 4, 2);
  assert(s3.x + s3.w <= 4 + 1e-3 && s3.y >= -1e-3);
  SDL_FRect s4 = render::SrcRect(render::Camera{0, 0, 999}, 4, 2);
  assert(s4.w >= 0 && s4.w <= 4 && s4.h >= 0 && s4.h <= 2);

  // Letterbox: 4x2 bg in 800x600 -> 800x400 centered.
  SDL_FRect d = render::DstRect(4, 2, 800, 600);
  assert(fabs(d.w - 800) < 1e-3 && fabs(d.h - 400) < 1e-3 &&
         fabs(d.x) < 1e-3 && fabs(d.y - 100) < 1e-3);

  // Headless draw with synthetic scene does not crash.
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    printf("render tests pass (sdl unavailable, pure ok)\n");
    return 0;
  }
  SDL_Window *w = SDL_CreateWindow("t", 800, 600, 0);
  assert(w);
  SDL_Renderer *r = SDL_CreateRenderer(w, nullptr);
  assert(r);
  SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB24,
                                     SDL_TEXTUREACCESS_STATIC, b.w, b.h);
  assert(t);
  SDL_UpdateTexture(t, nullptr, b.rgb.data(), 3 * b.w);
  objects::ObjTable tab;
  tab.name = "NORMAL";
  objects::ObjDef row;
  row.file = "gfx\\a.obj";
  tab.rows.push_back(row);
  ves::ObjRec rec;
  rec.cls = 0;
  rec.x = 0;
  rec.y = 0;
  std::vector<ves::ObjGroup> groups = {{rec}};
  render::Player pl{0, 0};
  std::vector<render::Hole> holes = {{10, 10}};
  render::Camera cam{0, 0, 2};
  assert(render::Draw(r, t, b.w, b.h, groups, &tab, cam, pl, holes, true,
                      10, 10, 800, 600));
  SDL_RenderPresent(r);
  // Empty scene (no bg) also safe.
  render::Camera c0{0, 0, 1};
  assert(render::Draw(r, nullptr, 0, 0, groups, &tab, c0, pl, {}, false, 0, 0,
                      800, 600));
  assert(!render::Draw(nullptr, t, b.w, b.h, groups, &tab, cam, pl, holes,
                       false, 0, 0, 800, 600));
  SDL_DestroyTexture(t);
  SDL_DestroyRenderer(r);
  SDL_DestroyWindow(w);
  SDL_Quit();
  printf("render tests pass\n");
  return 0;
}
