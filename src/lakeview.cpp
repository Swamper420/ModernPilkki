// Lake scene viewer: .ves DEPTHMAP (palette-colored) + OBJECT_POSITIONS
// markers (class color), class -> objects.dat model path in title stats.
// Usage: ./lakeview <lake.ves> <objects.dat> [snow=0|1] [frames=0]
// Coords: objpos x/y are 8.24 fixed world units (+-128); mapped onto the
// grid by relative position. Group batch semantics open (all drawn).
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "objects.hpp"
#include "ves.hpp"

static std::vector<uint8_t> Read(const char *p) {
  FILE *f = fopen(p, "rb");
  if (!f) throw std::runtime_error("no such file");
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> d(n);
  if (fread(d.data(), 1, n, f) != (size_t)n) throw std::runtime_error("read");
  fclose(f);
  return d;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    printf("usage: lakeview <lake.ves> <objects.dat> [snow=0|1] [frames=0]\n");
    return 1;
  }
  bool snow = argc > 3 && atoi(argv[3]) != 0;
  int cap = argc > 4 ? atoi(argv[4]) : 0;
  std::vector<uint8_t> lake;
  std::string objdat;
  try {
    lake = Read(argv[1]);
    auto od = Read(argv[2]);
    objdat.assign(od.begin(), od.end());
  } catch (const std::exception &e) {
    printf("read fail: %s\n", e.what());
    return 1;
  }
  ves::Grid depth;
  std::vector<ves::ObjGroup> groups;
  try {
    auto secs = ves::Parse(lake);
    for (auto &s : secs) {
      if (s.tag == "DEPTHMAP") depth = ves::ParseGrid(s.payload);
      if (s.tag == "OBJECT_POSITIONS")
        groups = ves::ParseObjPos(s.payload.data(), s.payload.size());
    }
  } catch (const std::exception &e) {
    printf("lake fail: %s\n", e.what());
    return 1;
  }
  auto tables = objects::Parse(objdat);
  const objects::ObjTable *tab = &tables[0];
  for (auto &t : tables)
    if ((snow && t.name == "SNOW") || (!snow && t.name == "NORMAL")) tab = &t;
  size_t nobj = 0, withmod = 0, oob = 0;
  for (auto &g : groups) nobj += g.size();
  for (auto &g : groups)
    for (auto &r : g) {
      if (r.cls >= tab->rows.size()) {
        oob++;
        continue;
      }
      const std::string &f = tab->rows[r.cls].file;
      if (!f.empty() && f != "disabled") withmod++;
    }
  printf("grid %dx%d objs %zu with-model %zu oob-class %zu block %s\n",
         depth.w, depth.h, nobj, withmod, oob, tab->name.c_str());
  // Depth -> RGB via embedded palette.
  std::vector<uint8_t> rgb((size_t)depth.w * depth.h * 3);
  for (size_t i = 0; i < depth.cells.size(); i++) {
    rgb[3 * i] = depth.pal[3 * depth.cells[i]];
    rgb[3 * i + 1] = depth.pal[3 * depth.cells[i] + 1];
    rgb[3 * i + 2] = depth.pal[3 * depth.cells[i] + 2];
  }
  int winw = 960, winh = 960 * depth.h / depth.w;
  if (winh > 960) {
    winh = 960;
    winw = 960 * depth.w / depth.h;
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
  SDL_Window *w = SDL_CreateWindow(argv[1], winw, winh, 0);
  SDL_Renderer *r = SDL_CreateRenderer(w, nullptr);
  SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB24,
                                     SDL_TEXTUREACCESS_STATIC, depth.w,
                                     depth.h);
  SDL_UpdateTexture(t, nullptr, rgb.data(), 3 * depth.w);
  int frames = 0;
  bool run = true;
  while (run && (!cap || frames < cap)) {
    SDL_Event e;
    while (SDL_PollEvent(&e))
      if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_KEY_DOWN) run = false;
    SDL_RenderTexture(r, t, nullptr, nullptr);
    for (auto &g : groups)
      for (auto &rr : g) {
        float wx = rr.x / 16777216.f, wy = rr.y / 16777216.f;
        int px = (int)((wx + 128.f) / 256.f * winw);
        int py = (int)((wy + 128.f) / 256.f * winh);
        bool mapped = rr.cls < tab->rows.size() &&
                      !tab->rows[rr.cls].file.empty() &&
                      tab->rows[rr.cls].file != "disabled";
        if (mapped)
          SDL_SetRenderDrawColor(r, 255, 80 + (rr.cls * 37) % 175,
                                 60 + (rr.cls * 91) % 195, 255);
        else
          SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
        SDL_FRect dot{(float)px - 1, (float)py - 1, 3, 3};
        SDL_RenderFillRect(r, &dot);
      }
    SDL_RenderPresent(r);
    frames++;
    SDL_Delay(16);
  }
  printf("drew %d frames\n", frames);
  SDL_DestroyTexture(t);
  SDL_DestroyRenderer(r);
  SDL_DestroyWindow(w);
  SDL_Quit();
  return 0;
}
