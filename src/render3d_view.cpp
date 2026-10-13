// Standalone SDL_GPU skeleton viewer. Opens a window, clears to the letterbox
// viewport each frame. No shaders/geometry yet.
// Usage: render3d_view [--frames N]   (default N=3; 0 = until quit)
// Exits 0 even when no GPU device is available (headless/dummy).
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "render3d.hpp"

int main(int argc, char **argv) {
  int frames = 3;
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    printf("GPU unavailable: SDL_Init failed (%s)\n", SDL_GetError());
    return 0;
  }
  SDL_Window *w = SDL_CreateWindow("ModernPilkki (SDL_GPU)", 960, 720,
                                   SDL_WINDOW_RESIZABLE);
  if (!w) {
    printf("GPU unavailable: window creation failed (%s)\n", SDL_GetError());
    SDL_Quit();
    return 0;
  }

  render3d::Ctx gpu;
  if (!gpu.Init(w)) {
    printf("GPU unavailable: %s\n", SDL_GetError());
    SDL_DestroyWindow(w);
    SDL_Quit();
    return 0;
  }
  printf("GPU driver: %s\n", SDL_GetGPUDeviceDriver(gpu.dev));

  bool run = true;
  int n = 0;
  while (run && (!frames || n < frames)) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT)
        run = false;
      else if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        gpu.Resize(e.window.data1, e.window.data2);
    }
    if (gpu.BeginFrame()) gpu.EndFrame();
    n++;
    SDL_Delay(16);
  }

  printf("rendered %d frames\n", n);
  gpu.Shutdown();
  SDL_DestroyWindow(w);
  SDL_Quit();
  return 0;
}
