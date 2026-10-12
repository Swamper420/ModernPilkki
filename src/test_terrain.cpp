// Smallest assert: 4x4 mesh builds, height monotonic, camera clamps,
// headless terrain draw safe.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "terrain.hpp"

int main() {
  ves::Grid depth;
  depth.w = 4;
  depth.h = 4;
  depth.cells = {0, 0, 40, 80, 0, 60, 120, 160, 10, 90, 150, 200, 20, 100, 180,
                 255};
  depth.pal.resize(768);
  for (int i = 0; i < 256; i++) {
    depth.pal[3 * i] = (uint8_t)i;
    depth.pal[3 * i + 1] = (uint8_t)i;
    depth.pal[3 * i + 2] = (uint8_t)i;
  }
  // Mesh builds at grid size.
  terrain::Mesh m = terrain::BuildMesh(depth);
  assert(m.w == 4 && m.h == 4 && m.z.size() == 16);
  // Height monotonic with cell value.
  for (int c = 0; c < 255; c++)
    assert(terrain::HeightOf((uint8_t)c) <=
           terrain::HeightOf((uint8_t)(c + 1)));
  for (size_t i = 0; i < depth.cells.size(); i++)
    for (size_t j = 0; j < depth.cells.size(); j++)
      if (depth.cells[i] <= depth.cells[j]) assert(m.z[i] <= m.z[j]);
  // Shaded bg keeps dims; foam fires on shore cells, deep darkens.
  render::Bg b = terrain::BuildBackground(depth, nullptr, nullptr);
  assert(b.w == 4 && b.h == 4 && b.rgb.size() == 4 * 4 * 3);
  assert(b.rgb[3] == 235 && b.rgb[4] == 242 && b.rgb[5] == 248);  // foam
  // Camera clamp holds through reused render::Camera math.
  SDL_FRect s = render::SrcRect(render::Camera{9999, -9999, 2}, 4, 4);
  assert(s.x + s.w <= 4 + 1e-3 && s.y >= -1e-3);
  assert(terrain::HeightAt(m, 9999, -9999) == m.z[0 * 4 + 3]);

  // Headless draw safe.
  setenv("SDL_VIDEODRIVER", "dummy", 1);
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    printf("terrain tests pass (sdl unavailable, pure ok)\n");
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
  std::vector<ves::ObjGroup> groups;
  render::Player pl{0, 0};
  std::vector<render::Hole> holes = {{10, 10}};
  render::Camera cam{0, 0, 2};
  assert(terrain::Draw(r, t, b.w, b.h, groups, &tab, cam, pl, holes, false, 0,
                       0, 800, 600));
  SDL_RenderPresent(r);
  assert(!terrain::Draw(nullptr, t, b.w, b.h, groups, &tab, cam, pl, holes,
                        false, 0, 0, 800, 600));
  SDL_DestroyTexture(t);
  SDL_DestroyRenderer(r);
  SDL_DestroyWindow(w);
  SDL_Quit();
  printf("terrain tests pass\n");
  return 0;
}
