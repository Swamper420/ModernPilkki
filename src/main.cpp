// Playable loop: lake scene (ves DEPTHMAP + OBJECT_POSITIONS) + input
// bitmap + tick driver + audio init.
// Usage: ./pilkki [lake.ves] [objects.dat] [snow=0|1] [frames=0]
// No lake args: opens window, ticks empty scene. Missing/unparseable
// files: prints reason, continues with empty scene (never hard-fails).
// Headless: SDL dummy driver auto-caps frames so bare run exits.
#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

#include "audio.hpp"
#include "input.hpp"
#include "objects.hpp"
#include "tick.hpp"
#include "ves.hpp"

static std::vector<uint8_t> Read(const char *p) {
  FILE *f = fopen(p, "rb");
  if (!f) throw std::runtime_error("no such file");
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> d(n > 0 ? (size_t)n : 0);
  if (n > 0 && fread(d.data(), 1, (size_t)n, f) != (size_t)n)
    throw std::runtime_error("read");
  fclose(f);
  return d;
}

int main(int argc, char **argv) {
  const char *lakePath = argc > 1 ? argv[1] : nullptr;
  const char *objPath = argc > 2 ? argv[2] : nullptr;
  bool snow = argc > 3 && atoi(argv[3]) != 0;
  int cap = argc > 4 ? atoi(argv[4]) : 0;

  ves::Grid depth;
  std::vector<ves::ObjGroup> groups;
  const objects::ObjTable *tab = nullptr;
  std::vector<objects::ObjTable> tables;
  if (lakePath && objPath) {
    try {
      auto lake = Read(lakePath);
      auto od = Read(objPath);
      std::string objdat(od.begin(), od.end());
      auto secs = ves::Parse(lake);
      for (auto &s : secs) {
        if (s.tag == "DEPTHMAP") depth = ves::ParseGrid(s.payload);
        if (s.tag == "OBJECT_POSITIONS")
          groups = ves::ParseObjPos(s.payload.data(), s.payload.size());
      }
      tables = objects::Parse(objdat);
      tab = &tables[0];
      for (auto &t : tables)
        if ((snow && t.name == "SNOW") || (!snow && t.name == "NORMAL"))
          tab = &t;
      size_t nobj = 0;
      for (auto &g : groups) nobj += g.size();
      printf("grid %dx%d objs %zu block %s\n", depth.w, depth.h, nobj,
             tab->name.c_str());
    } catch (const std::exception &e) {
      printf("scene load fail (%s), empty scene\n", e.what());
      depth = ves::Grid{};
      groups.clear();
      tables.clear();
      tab = nullptr;
    }
  }

  // Depth -> RGB via embedded palette (lakeview logic).
  std::vector<uint8_t> rgb;
  if (!depth.cells.empty())
    for (size_t i = 0; i < depth.cells.size(); i++) {
      rgb.push_back(depth.pal[3 * depth.cells[i]]);
      rgb.push_back(depth.pal[3 * depth.cells[i] + 1]);
      rgb.push_back(depth.pal[3 * depth.cells[i] + 2]);
    }

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    printf("sdl init fail: %s (headless ok)\n", SDL_GetError());
    return 0;
  }
  printf("audio driver: %s\n",
         SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none");

  int winw = 800, winh = 600;
  if (depth.w && depth.h) {
    winw = 960;
    winh = 960 * depth.h / depth.w;
    if (winh > 960) {
      winh = 960;
      winw = 960 * depth.w / depth.h;
    }
  }
  SDL_Window *w = SDL_CreateWindow("ModernPilkki", winw, winh,
                                   SDL_WINDOW_RESIZABLE);
  if (!w) {
    printf("window fail: %s (headless ok)\n", SDL_GetError());
    SDL_Quit();
    return 0;
  }
  // Headless dummy driver with no explicit cap: run a few tick-only
  // frames then exit instead of hanging forever.
  const char *drv = SDL_GetCurrentVideoDriver();
  if (!cap && drv && std::string(drv) == "dummy") cap = 5;

  SDL_Renderer *r = SDL_CreateRenderer(w, nullptr);
  SDL_Texture *t = nullptr;
  if (r && !depth.cells.empty()) {
    t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC,
                          depth.w, depth.h);
    if (t) SDL_UpdateTexture(t, nullptr, rgb.data(), 3 * depth.w);
  }

  input::Keys keys;
  tick::Driver driver;  // null stages: order/skip/quit protocol only
  tick::Clock clock;
  bool run = true;
  int frames = 0;
  while (run && (!cap || frames < cap)) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) {
        run = false;
        driver.quitRequested = true;
      }
      if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        int id = input::SdlToId(e.key.key);
        if (id >= 0)
          input::Set(keys, (uint32_t)id, e.type == SDL_EVENT_KEY_DOWN);
      }
    }
    clock.Update(SDL_GetTicks());
    if (driver.Step() == tick::Action::Quit) break;
    if (r) {
      if (t)
        SDL_RenderTexture(r, t, nullptr, nullptr);
      else {
        SDL_SetRenderDrawColor(r, 20, 40, 60, 255);
        SDL_RenderClear(r);
      }
      if (t && tab)
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
    }
    frames++;
    SDL_Delay(16);
  }
  printf("frames %d keys 0x%x tick-delta %u\n", frames, keys.bits,
         clock.delta);
  if (t) SDL_DestroyTexture(t);
  if (r) SDL_DestroyRenderer(r);
  SDL_DestroyWindow(w);
  SDL_Quit();
  return 0;
}
