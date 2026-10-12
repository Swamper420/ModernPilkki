// Minimal sprite viewer: decodes one .spr, shows frame 0 in SDL window.
// Usage: ./sprview <file.spr> [frame]
// Saves /tmp/opencode/sprview.ppm too (headless check).
#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>
#include "spr.hpp"

static void ToRGB24(const spr::Sprite &s, int frame, std::vector<uint8_t> &rgb) {
  size_t n = (size_t)s.w * s.h;
  rgb.resize(n * 3);
  const uint8_t *pal = s.pal.data();
  for (size_t i = 0; i < n; i++) {
    uint8_t r, g, b;
    if (s.bpp == 2) {
      uint16_t v = s.px[(size_t)frame * n + i];
      r = (v >> 11) * 255 / 31;
      g = ((v >> 5) & 0x3F) * 255 / 63;
      b = (v & 0x1F) * 255 / 31;
    } else {
      uint8_t k = s.idx[(size_t)frame * n + i];
      r = pal[3 * k];
      g = pal[3 * k + 1];
      b = pal[3 * k + 2];
    }
    rgb[3 * i] = r;
    rgb[3 * i + 1] = g;
    rgb[3 * i + 2] = b;
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: sprview <file.spr> [frame]\n");
    return 1;
  }
  FILE *f = fopen(argv[1], "rb");
  if (!f) {
    printf("no such file\n");
    return 1;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> d(n);
  if (fread(d.data(), 1, n, f) != (size_t)n) return 1;
  fclose(f);
  spr::Sprite s;
  try {
    s = spr::Decode(d);
  } catch (const std::exception &e) {
    printf("decode fail: %s\n", e.what());
    return 1;
  }
  int frame = argc > 2 ? atoi(argv[2]) : 0;
  printf("nf=%d bpp=%d enc=%d %dx%d frames, showing %d\n", s.nf, s.bpp, s.enc,
         s.w, s.h, frame);
  std::vector<uint8_t> rgb;
  ToRGB24(s, frame, rgb);
  FILE *ppm = fopen("/tmp/opencode/sprview.ppm", "wb");
  if (ppm) {
    fprintf(ppm, "P6\n%d %d\n255\n", s.w, s.h);
    fwrite(rgb.data(), 1, rgb.size(), ppm);
    fclose(ppm);
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
  SDL_Window *w = SDL_CreateWindow(argv[1], s.w, s.h, 0);
  if (!w) {
    SDL_Quit();
    return 1;
  }
  SDL_Renderer *r = SDL_CreateRenderer(w, nullptr);
  SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB24,
                                     SDL_TEXTUREACCESS_STATIC, s.w, s.h);
  SDL_UpdateTexture(t, nullptr, rgb.data(), 3 * s.w);
  bool run = true;
  while (run) {
    SDL_Event e;
    while (SDL_PollEvent(&e))
      if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_KEY_DOWN) run = false;
    SDL_RenderTexture(r, t, nullptr, nullptr);
    SDL_RenderPresent(r);
    SDL_Delay(16);
  }
  SDL_DestroyTexture(t);
  SDL_DestroyRenderer(r);
  SDL_DestroyWindow(w);
  SDL_Quit();
  return 0;
}
