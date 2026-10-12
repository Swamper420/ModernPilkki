// Minimal textured .obj viewer: verts/faces from .obj, pixels from .pcx,
// SDL_RenderGeometry triangles, Y auto-rotate, painter-sorted.
// Usage: ./objview <file.obj> <file.pcx> [frames=0]
// Per-face texture select open: all faces use the given texture.
// Saves /tmp/opencode/objview.ppm (texture proof, headless check).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include "obj.hpp"
#include "pcx.hpp"

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

static void ToRGBA(const pcx::Image &im, std::vector<uint8_t> &rgba) {
  size_t n = (size_t)im.w * im.h;
  rgba.resize(n * 4);
  for (size_t i = 0; i < n; i++) {
    uint8_t r, g, b, a = 255;
    if (im.planes == 1) {
      uint8_t k = im.idx[i];
      r = im.pal[3 * k];
      g = im.pal[3 * k + 1];
      b = im.pal[3 * k + 2];
    } else {
      size_t o = im.planes == 3 ? 3 * i : 4 * i;
      r = im.px[o];
      g = im.px[o + 1];
      b = im.px[o + 2];
      if (im.planes == 4) a = im.px[o + 3];
    }
    rgba[4 * i] = r;
    rgba[4 * i + 1] = g;
    rgba[4 * i + 2] = b;
    rgba[4 * i + 3] = a;
  }
}

int main(int argc, char **argv) {
  if (argc < 3) {
    printf("usage: objview <file.obj> <file.pcx> [frames=0]\n");
    return 1;
  }
  obj::Model m;
  pcx::Image im;
  try {
    m = obj::Parse(Read(argv[1]));
    im = pcx::Decode(Read(argv[2]));
  } catch (const std::exception &e) {
    printf("decode fail: %s\n", e.what());
    return 1;
  }
  int cap = argc > 3 ? atoi(argv[3]) : 0;
  printf("nv=%u nf=%u nt=%u tex=%dx%dx%d\n", m.nvert, m.nface, m.nt, im.w,
         im.h, im.planes);
  std::vector<uint8_t> rgba;
  ToRGBA(im, rgba);
  FILE *ppm = fopen("/tmp/opencode/objview.ppm", "wb");
  if (ppm) {
    fprintf(ppm, "P6\n%d %d\n255\n", im.w, im.h);
    for (size_t i = 0; i < (size_t)im.w * im.h; i++)
      fwrite(rgba.data() + 4 * i, 1, 3, ppm);
    fclose(ppm);
  }
  // Fit mesh to an 800x600 view.
  int32_t mnx = m.verts[0].x, mxx = mnx, mny = m.verts[0].y, mxy = mny,
          mnz = m.verts[0].z, mxz = mnz;
  for (auto &v : m.verts) {
    mnx = v.x < mnx ? v.x : mnx;
    mxx = v.x > mxx ? v.x : mxx;
    mny = v.y < mny ? v.y : mny;
    mxy = v.y > mxy ? v.y : mxy;
    mnz = v.z < mnz ? v.z : mnz;
    mxz = v.z > mxz ? v.z : mxz;
  }
  float cx = (mnx + mxx) / 2.f, cy = (mny + mxy) / 2.f,
        cz = (mnz + mxz) / 2.f;
  float span = mxx - mnx;
  if (mxy - mny > span) span = mxy - mny;
  if (mxz - mnz > span) span = mxz - mnz;
  if (span < 1) span = 1;
  if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
  SDL_Window *w = SDL_CreateWindow(argv[1], 800, 600, 0);
  SDL_Renderer *r = SDL_CreateRenderer(w, nullptr);
  SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STATIC, im.w, im.h);
  SDL_UpdateTexture(t, nullptr, rgba.data(), 4 * im.w);
  std::vector<int> order(m.nface);
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  int frames = 0;
  bool run = true;
  while (run && (!cap || frames < cap)) {
    SDL_Event e;
    while (SDL_PollEvent(&e))
      if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_KEY_DOWN) run = false;
    float a = frames * 0.05f, ca = cosf(a), sa = sinf(a);
    struct P {
      float x, y, z;
    };
    std::vector<P> pt(m.nvert);
    for (size_t i = 0; i < m.verts.size(); i++) {
      float x = (m.verts[i].x - cx) / span, y = (m.verts[i].y - cy) / span,
            z = (m.verts[i].z - cz) / span;
      pt[i] = {x * ca + z * sa, y, -x * sa + z * ca};
    }
    std::sort(order.begin(), order.end(), [&](int A, int B) {
      float za = (pt[m.faces[A].v[0]].z + pt[m.faces[A].v[1]].z +
                  pt[m.faces[A].v[2]].z) / 3;
      float zb = (pt[m.faces[B].v[0]].z + pt[m.faces[B].v[1]].z +
                  pt[m.faces[B].v[2]].z) / 3;
      return za < zb;
    });
    SDL_SetRenderDrawColor(r, 20, 24, 40, 255);
    SDL_RenderClear(r);
    for (int fi : order) {
      const obj::Face &f = m.faces[fi];
      SDL_Vertex v[3];
      for (int k = 0; k < 3; k++) {
        const P &p = pt[f.v[k]];
        float persp = 1.f / (1.6f - p.z);
        v[k].position = {400 + 500 * p.x * persp, 300 - 500 * p.y * persp};
        v[k].color = {255, 255, 255, 255};
        float u = f.uv[2 * k] % (uint32_t)im.w;
        float vv = f.uv[2 * k + 1] % (uint32_t)im.h;
        v[k].tex_coord = {u / im.w, vv / im.h};
      }
      if (SDL_RenderGeometry(r, t, v, 3, nullptr, 0) < 0) {
        printf("draw fail: %s\n", SDL_GetError());
        return 1;
      }
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
