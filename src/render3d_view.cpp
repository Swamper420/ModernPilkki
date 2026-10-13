// Standalone SDL_GPU viewer. Opens a window, clears to the letterbox viewport
// each frame, and draws one of the iteration smoke tests:
//   default : the it4 hardcoded triangle (render3d_pipeline.hpp Triangle)
//   --quad  : the it5 indexed layout-B textured quad (Quad + fixed VB/IB)
// Usage: render3d_view [--frames N] [--quad] [--vert P] [--frag P]
//        (default N=3; 0 = until quit. P default to the build-time .spv paths.)
// Exits 0 even when no GPU device / no shaders are available (headless/dummy).
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "render3d.hpp"
#include "render3d_pipeline.hpp"

int main(int argc, char **argv) {
  int frames = 3;
  bool useQuad = false;
  const char *vert = nullptr, *frag = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--quad")) useQuad = true;
    else if (!strcmp(argv[i], "--vert") && i + 1 < argc) vert = argv[++i];
    else if (!strcmp(argv[i], "--frag") && i + 1 < argc) frag = argv[++i];
  }

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

  // Shaders: argv override, else the .spv paths baked in by CMake (when glslc
  // was found). No shaders -> clear-only, still no crash.
#ifdef RENDER3D_VERT_SPV
  if (!vert) vert = useQuad ? RENDER3D_QUAD_VERT_SPV : RENDER3D_VERT_SPV;
  if (!frag) frag = useQuad ? RENDER3D_QUAD_FRAG_SPV : RENDER3D_FRAG_SPV;
#endif
  render3d::Triangle tri;
  render3d::Quad quad;
  bool ready = false;
  if (useQuad)
    ready = vert && frag && render3d::InitQuad(gpu.dev, gpu.swapFmt, vert, frag, quad);
  else
    ready = vert && frag && render3d::InitTriangle(gpu.dev, gpu.swapFmt, vert, frag, tri);
  printf("%s pipeline %s\n", useQuad ? "quad" : "triangle",
         ready ? "ready" : "unavailable (shaders missing or GPU unsupported)");

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
    if (gpu.BeginFrame()) {
      if (useQuad && quad.Ok()) {
        render3d::Rect r = render3d::Letterbox(gpu.pixW, gpu.pixH, 4.f / 3.f);
        float vp[2] = {r.w, r.h};
        gpu.PushVertexUniform(vp, sizeof vp);
        render3d::QuadDraw qd{quad.pipe, quad.vb, quad.ib, quad.tex, quad.sampler, 4};
        gpu.EndFrame(nullptr, &qd);
      } else {
        gpu.EndFrame(tri.Ok() ? tri.pipe : nullptr);
      }
    }
    n++;
    SDL_Delay(16);
  }

  printf("rendered %d frames\n", n);
  if (tri.Ok()) render3d::ReleaseTriangle(gpu.dev, tri);
  if (quad.Ok()) render3d::ReleaseQuad(gpu.dev, quad);
  gpu.Shutdown();
  SDL_DestroyWindow(w);
  SDL_Quit();
  return 0;
}
