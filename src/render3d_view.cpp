// Standalone SDL_GPU viewer. Opens a window, clears to the letterbox viewport
// each frame, and draws one of the iteration smoke tests:
//   default        : it4 hardcoded triangle (gl_VertexIndex, no VB)
//   --quad         : it5 indexed layout-B textured quad
//   --obj P [--tex Q] : it6 layout-A .obj mesh with a .pcx texture
//   --terrain      : it7 synthetic layout-B terrain grid (stand-in projection)
//   --lake P       : it8 world-space lake terrain (DEPTHMAP -> layout-A mesh)
// Usage: render3d_view [--frames N] [--quad] [--terrain] [--lake P]
//                      [--obj P] [--tex Q] [--vert P] [--frag P]
//        (default N=3; 0 = until quit. --vert/--frag default to the build-time
//        .spv paths; --obj/--tex override the shaders to their it6 pair.)
// Exits 0 even when no GPU device / no assets are available (headless/dummy).
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "render3d.hpp"
#include "render3d_camera.hpp"
#include "render3d_math.hpp"
#include "render3d_pipeline.hpp"
#include "render3d_scene.hpp"
#include "render3d_terrain.hpp"
#include "ves.hpp"

int main(int argc, char **argv) {
  int frames = 3;
  bool useQuad = false, useTerrain = false;
  const char *objPath = nullptr, *texPath = nullptr;
  const char *lakePath = nullptr;
  const char *vert = nullptr, *frag = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--quad")) useQuad = true;
    else if (!strcmp(argv[i], "--terrain")) useTerrain = true;
    else if (!strcmp(argv[i], "--lake") && i + 1 < argc) lakePath = argv[++i];
    else if (!strcmp(argv[i], "--obj") && i + 1 < argc) objPath = argv[++i];
    else if (!strcmp(argv[i], "--tex") && i + 1 < argc) texPath = argv[++i];
    else if (!strcmp(argv[i], "--vert") && i + 1 < argc) vert = argv[++i];
    else if (!strcmp(argv[i], "--frag") && i + 1 < argc) frag = argv[++i];
  }
  const bool useScene = lakePath != nullptr;
  const bool useMesh = objPath != nullptr && !useQuad && !useTerrain && !useScene;

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
  render3d::Triangle tri;
  render3d::Quad quad;
  render3d::MeshGpu mesh;
  render3d::TerrainGpu terra;
  bool ready = false;
#ifdef RENDER3D_VERT_SPV
  if (useMesh || useScene) {
    if (!vert) vert = RENDER3D_MESH_VERT_SPV;
    if (!frag) frag = RENDER3D_MESH_FRAG_SPV;
  } else if (useQuad || useTerrain) {
    if (!vert) vert = RENDER3D_QUAD_VERT_SPV;
    if (!frag) frag = RENDER3D_QUAD_FRAG_SPV;
  } else {
    if (!vert) vert = RENDER3D_VERT_SPV;
    if (!frag) frag = RENDER3D_FRAG_SPV;
  }
#endif
  render3d::Rect r0 = render3d::Letterbox(gpu.pixW, gpu.pixH, 4.f / 3.f);
  if (useScene) {
    // World-space lake terrain: |DEPTHMAP grid -> layout-A mesh, drawn under a
    // clean-room view-projection (stand-in for 004a7660, docs/render.md 8).
    ves::Grid depth;
    try {
      for (auto& s : ves::Parse(render3d::ReadWholeFile(lakePath)))
        if (s.tag == "DEPTHMAP") depth = ves::ParseGrid(s.payload);
    } catch (const std::exception& e) {
      fprintf(stderr, "render3d: lake parse failed (%s): %s\n", lakePath, e.what());
    }
    printf("lake %dx%d terrain\n", depth.w, depth.h);
    auto verts = render3d::BuildTerrainMesh(depth);
    auto tex = render3d::BuildTerrainTex(depth);
    ready = vert && frag &&
            render3d::InitMeshVerts(gpu.dev, gpu.swapFmt, vert, frag,
                                    std::move(verts), std::move(tex),
                                    render3d::TexWidthFor(depth.w), depth.h,
                                    mesh);
  } else if (useTerrain) {
    // Synthetic DEM (a few sine bumps) -> layout-B triangle list; a stand-in
    // for the 004a7660 depth-grid projection (docs/render.md 8).
    const int G = 24;
    std::vector<float> h((size_t)G * G);
    for (int y = 0; y < G; ++y)
      for (int x = 0; x < G; ++x) {
        auto s = [](float a) { return sinf(a * 3.14159f) * sinf(a * 6.28318f); };
        h[(size_t)y * G + x] = s((float)x / (G - 1)) + s((float)y / (G - 1));
      }
    auto verts = render3d::SyntheticGridVerts(G, G, r0.w, r0.h, &h);
    ready = vert && frag &&
            render3d::InitTerrain(gpu.dev, gpu.swapFmt, vert, frag, verts, terra);
  } else if (useMesh)
    ready = vert && frag &&
            render3d::InitMesh(gpu.dev, gpu.swapFmt, vert, frag, objPath, texPath, mesh);
  else if (useQuad)
    ready = vert && frag && render3d::InitQuad(gpu.dev, gpu.swapFmt, vert, frag, quad);
  else
    ready = vert && frag && render3d::InitTriangle(gpu.dev, gpu.swapFmt, vert, frag, tri);
  const char *what = useScene   ? "scene"
                     : useTerrain ? "terrain"
                     : useMesh  ? "mesh"
                     : useQuad  ? "quad"
                                : "triangle";
  printf("%s pipeline %s\n", what,
         ready ? "ready" : "unavailable (assets/shaders missing or GPU unsupported)");

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
      render3d::Rect r = render3d::Letterbox(gpu.pixW, gpu.pixH, 4.f / 3.f);
      render3d::Draw d;
      if (ready) {
        if (useScene) {
          // Slow orbit over the lake; the model matrix is the view-projection.
          float a = (float)n * 0.008f;
          render3d::SceneCam sc;
          sc.ex = sinf(a) * 150.f;
          sc.ey = 70.f;
          sc.ez = cosf(a) * 150.f;
          sc.tx = sc.ty = sc.tz = 0.f;
          float aspect = r.h > 0 ? r.w / r.h : 1.f;
          render3d::Mat4 vp = render3d::Transpose(render3d::ViewProj(sc, aspect));
          gpu.PushVertexUniform(vp.m, sizeof vp.m);
          d = render3d::Draw{mesh.pipe, mesh.vb, nullptr, mesh.tex, mesh.sampler,
                             mesh.vertexCount};
        } else if (useTerrain) {
          float vp[2] = {r.w, r.h};
          gpu.PushVertexUniform(vp, sizeof vp);
          d = render3d::Draw{terra.pipe, terra.vb, nullptr, terra.tex,
                             terra.sampler, terra.vertexCount};
        } else if (useMesh) {
          // Rotation-only model matrix (positions are pre-centered/unit).
          render3d::Mat4 rot = render3d::Transpose(
              render3d::WorldMatrix(0, 0, 0, 0, (float)n * 3.f, 0));
          gpu.PushVertexUniform(rot.m, sizeof rot.m);
          d = render3d::Draw{mesh.pipe, mesh.vb, nullptr, mesh.tex, mesh.sampler,
                             mesh.vertexCount};
        } else if (useQuad) {
          float vp[2] = {r.w, r.h};
          gpu.PushVertexUniform(vp, sizeof vp);
          d = render3d::Draw{quad.pipe, quad.vb, quad.ib, quad.tex, quad.sampler, 4};
        } else {
          d = render3d::Draw{tri.pipe, nullptr, nullptr, nullptr, nullptr, 3};
        }
      }
      gpu.EndFrame(ready ? &d : nullptr);
    }
    n++;
    SDL_Delay(16);
  }

  printf("rendered %d frames\n", n);
  if (tri.Ok()) render3d::ReleaseTriangle(gpu.dev, tri);
  if (quad.Ok()) render3d::ReleaseQuad(gpu.dev, quad);
  if (mesh.Ok()) render3d::ReleaseMesh(gpu.dev, mesh);
  if (terra.Ok()) render3d::ReleaseTerrain(gpu.dev, terra);
  gpu.Shutdown();
  SDL_DestroyWindow(w);
  SDL_Quit();
  return 0;
}
