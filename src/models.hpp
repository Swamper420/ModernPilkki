// Software-projected .obj/.3ds meshes for lake objects (SDL3, no new deps).
// Loads .obj verts/faces via obj::Parse; when a sibling assembly script
// exists (gfx/obj/X/Y.obj -> gfx/3ds/X/Y.3ds) merges its STARTSTRUCTURE
// parts via model3ds::ParseStructure. Child offset = accumulated p1..p3
// along indent parents (p0 looks like a joint id; MOVES bind pose ignored).
// Draw: fixed-pitch orbit projection, backface cull (view-space n.z > 0),
// painter sort, flat shade (|n.l| so flipped winding can't go black) or
// first-texture UVs (per-face texture select open, same as objview).
// Missing files -> ok=false, caller draws fallback dot, never throws out.
// ponytail: static bind pose; animate via model3ds::Sample when needed.
#pragma once
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "model3ds.hpp"
#include "obj.hpp"
#include "pcx.hpp"

namespace models {

constexpr float kPi = 3.14159265f;

inline std::string Norm(const std::string &p) {
  std::string o = p;
  for (auto &c : o)
    if (c == '\\') c = '/';
  return o;
}

// "extracted/data/objects.dat" -> "extracted"; else "extracted".
inline std::string RootFromObjPath(const char *objPath) {
  if (!objPath) return "extracted";
  std::string p = Norm(objPath);
  size_t i = p.rfind("/data/objects.dat");
  if (i != std::string::npos) return p.substr(0, i);
  return "extracted";
}

inline std::vector<uint8_t> ReadFile(const std::string &path) {
  FILE *f = fopen(path.c_str(), "rb");
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

struct Tri {
  uint32_t v[3] = {};
  uint32_t uv[6] = {};
};

struct Mesh {
  std::vector<float> xyz;  // 3*n local units, bbox-centered
  std::vector<Tri> tris;
  float span = 1;
  std::string tex0;  // normalized rel path, "" when untextured
  uint8_t r = 140, g = 150, b = 140;
  bool ok = false;
};

// Base flat-shade color from asset path keywords.
inline void Tint(Mesh &m, const std::string &rel) {
  std::string p = Norm(rel);
  for (auto &c : p) c = (char)tolower((unsigned char)c);
  auto has = [&](const char *k) { return p.find(k) != std::string::npos; };
  if (has("kuusi") || has("manty") || has("koivu") || has("paju") ||
      has("tammi") || has("kaisla") || has("tree") || has("havu") ||
      has("leaf")) {
    m.r = 55;
    m.g = 135;
    m.b = 65;
  } else if (has("kivi") || has("stone")) {
    m.r = 130;
    m.g = 130;
    m.b = 135;
  } else if (has("mokki") || has("majakka") || has("koppi") || has("vaja") ||
             has("laavu") || has("teltta") || has("kota") ||
             has("house") || has("tupa")) {
    m.r = 155;
    m.g = 112;
    m.b = 78;
  } else if (has("car") || has("auto")) {
    m.r = 165;
    m.g = 60;
    m.b = 60;
  } else if (has("hum")) {
    m.r = 70;
    m.g = 110;
    m.b = 200;
  } else if (has("snow") || has("_sn.") || has("ice") || has("lumi")) {
    m.r = 215;
    m.g = 228;
    m.b = 238;
  }
}

inline void AppendObj(Mesh &m, const obj::Model &o, float ox, float oy,
                      float oz) {
  size_t base = m.xyz.size() / 3;
  for (auto &v : o.verts) {
    m.xyz.push_back((float)v.x + ox);
    m.xyz.push_back((float)v.y + oy);
    m.xyz.push_back((float)v.z + oz);
  }
  for (auto &f : o.faces) {
    Tri t;
    for (int k = 0; k < 3; k++) t.v[k] = (uint32_t)(base + f.v[k]);
    for (int k = 0; k < 6; k++) t.uv[k] = f.uv[k];
    m.tris.push_back(t);
  }
  if (m.tex0.empty() && !o.tex.empty()) m.tex0 = Norm(o.tex[0]);
}

inline void Finish(Mesh &m, const std::string &rel) {
  if (m.xyz.empty() || m.tris.empty()) return;
  float mnx = m.xyz[0], mxx = mnx, mny = m.xyz[1], mxy = mny, mnz = m.xyz[2],
        mxz = mnz;
  for (size_t i = 0; i < m.xyz.size(); i += 3) {
    mnx = m.xyz[i] < mnx ? m.xyz[i] : mnx;
    mxx = m.xyz[i] > mxx ? m.xyz[i] : mxx;
    mny = m.xyz[i + 1] < mny ? m.xyz[i + 1] : mny;
    mxy = m.xyz[i + 1] > mxy ? m.xyz[i + 1] : mxy;
    mnz = m.xyz[i + 2] < mnz ? m.xyz[i + 2] : mnz;
    mxz = m.xyz[i + 2] > mxz ? m.xyz[i + 2] : mxz;
  }
  float cx = (mnx + mxx) / 2, cyy = (mny + mxy) / 2, cz = (mnz + mxz) / 2;
  for (size_t i = 0; i < m.xyz.size(); i += 3) {
    m.xyz[i] -= cx;
    m.xyz[i + 1] -= cyy;
    m.xyz[i + 2] -= cz;
  }
  m.span = mxx - mnx;
  if (mxy - mny > m.span) m.span = mxy - mny;
  if (mxz - mnz > m.span) m.span = mxz - mnz;
  if (m.span < 1) m.span = 1;
  Tint(m, rel);
  m.ok = true;
}

struct Bank {
  std::string root = "extracted";
  std::map<std::string, Mesh> meshes;
  struct Tex {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    bool ok = false, tried = false;
  };
  std::map<std::string, Tex> texs;
  size_t nTri = 0;  // loaded polycount for reporting

  Mesh LoadSingle(const std::string &rel) {
    Mesh m;
    AppendObj(m, obj::Parse(ReadFile(root + "/" + Norm(rel))), 0, 0, 0);
    Finish(m, rel);
    return m;
  }

  // Merge STARTSTRUCTURE parts; child offset = parent chain + (p1,p2,p3).
  Mesh LoadAssembled(const std::string &rel3ds) {
    Mesh m;
    auto d = ReadFile(root + "/" + Norm(rel3ds));
    auto parts = model3ds::ParseStructure(std::string(d.begin(), d.end()));
    if (parts.empty()) throw std::runtime_error("empty STRUCTURE");
    std::vector<float> stack;  // 3 floats per depth level
    for (auto &pt : parts) {
      float px = 0, py = 0, pz = 0;
      if (pt.depth > 0 && stack.size() >= 3) {
        size_t pi = (size_t)(pt.depth - 1) * 3;
        if (pi + 2 < stack.size()) {
          px = stack[pi];
          py = stack[pi + 1];
          pz = stack[pi + 2];
        }
      }
      float ox = px + pt.p[1], oy = py + pt.p[2], oz = pz + pt.p[3];
      size_t si = (size_t)pt.depth * 3;
      if (stack.size() < si + 3) stack.resize(si + 3);
      stack[si] = ox;
      stack[si + 1] = oy;
      stack[si + 2] = oz;
      AppendObj(m, obj::Parse(ReadFile(root + "/" + Norm(pt.path))), ox, oy,
                oz);
      if (m.xyz.size() > (size_t)60000) break;  // runaway guard
    }
    Finish(m, rel3ds);
    return m;
  }

  // objects.dat gfx path -> merged mesh (sibling .3ds when present).
  const Mesh &Get(const std::string &gfxPath) {
    std::string key = Norm(gfxPath);
    auto it = meshes.find(key);
    if (it != meshes.end()) return it->second;
    Mesh m;
    try {
      std::string s3 = key;
      size_t p = s3.find("gfx/obj/");
      if (p != std::string::npos) {
        s3.replace(p, 8, "gfx/3ds/");
        size_t e = s3.rfind(".obj");
        if (e != std::string::npos) s3.replace(e, 4, ".3ds");
        FILE *f = fopen((root + "/" + s3).c_str(), "rb");
        if (f) {
          fclose(f);
          m = LoadAssembled(s3);
        } else {
          m = LoadSingle(key);
        }
      } else if (key.size() > 4 && key.compare(key.size() - 4, 4, ".3ds") == 0) {
        m = LoadAssembled(key);
      } else {
        m = LoadSingle(key);
      }
    } catch (...) {
    }
    if (m.ok) nTri += m.tris.size();
    return meshes.emplace(key, m).first->second;
  }

  const Mesh &Human() { return Get("gfx/3ds/human.3ds"); }

  const Tex &TexPixels(const std::string &texPath) {
    std::string key = Norm(texPath);
    auto it = texs.find(key);
    if (it != texs.end()) return it->second;
    Tex t;
    t.tried = true;
    try {
      pcx::Image im = pcx::Decode(ReadFile(root + "/" + key));
      t.w = im.w;
      t.h = im.h;
      size_t n = (size_t)t.w * t.h;
      t.rgba.resize(n * 4);
      for (size_t i = 0; i < n; i++) {
        uint8_t r, g, b, a = 255;
        if (im.planes == 1) {
          uint8_t k = im.idx[i];
          r = im.pal[3 * k];
          g = im.pal[3 * k + 1];
          b = im.pal[3 * k + 2];
        } else {
          size_t o = i * (im.planes == 3 ? 3 : 4);
          r = im.px[o];
          g = im.px[o + 1];
          b = im.px[o + 2];
          if (im.planes == 4) a = im.px[o + 3];
        }
        t.rgba[4 * i] = r;
        t.rgba[4 * i + 1] = g;
        t.rgba[4 * i + 2] = b;
        t.rgba[4 * i + 3] = a;
      }
      t.ok = true;
    } catch (...) {
    }
    return texs.emplace(key, std::move(t)).first->second;
  }
};

struct TexCache {
  std::map<std::string, SDL_Texture *> t;
  std::map<std::string, bool> missing;
  int w = 0, h = 0;
  SDL_Texture *Get(SDL_Renderer *r, Bank &bank, const std::string &name) {
    if (name.empty() || !r) return nullptr;
    auto it = t.find(name);
    if (it != t.end()) return it->second;
    if (missing[name]) return nullptr;
    const Bank::Tex &px = bank.TexPixels(name);
    if (!px.ok) {
      missing[name] = true;
      return nullptr;
    }
    SDL_Texture *tx = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32,
                                        SDL_TEXTUREACCESS_STATIC, px.w, px.h);
    if (!tx) {
      missing[name] = true;
      return nullptr;
    }
    SDL_UpdateTexture(tx, nullptr, px.rgba.data(), 4 * px.w);
    w = px.w;
    h = px.h;
    t[name] = tx;
    return tx;
  }
  void Destroy() {
    for (auto &kv : t) SDL_DestroyTexture(kv.second);
    t.clear();
    missing.clear();
  }
};

// Rotate mesh into view space. Out: X/Y/Z per vert (Z = depth, larger near).
inline void Rotate(const Mesh &m, float yawDeg, float pitch, std::vector<float> &X,
                   std::vector<float> &Y, std::vector<float> &Z) {
  float y = yawDeg * kPi / 180.f;
  float cyw = cosf(y), syw = sinf(y), cp = cosf(pitch), sp = sinf(pitch);
  size_t n = m.xyz.size() / 3;
  X.resize(n);
  Y.resize(n);
  Z.resize(n);
  for (size_t i = 0; i < n; i++) {
    float x = m.xyz[3 * i], yy = m.xyz[3 * i + 1], z = m.xyz[3 * i + 2];
    float x1 = x * cyw + z * syw, z1 = -x * syw + z * cyw;
    Y[i] = yy * cp - z1 * sp;
    Z[i] = yy * sp + z1 * cp;
    X[i] = x1;
  }
}

// Per-face view-space normal z (>0 faces camera) + |n.l| shade 0..1.
inline void FaceShade(const Mesh &m, const std::vector<float> &X,
                      const std::vector<float> &Y,
                      const std::vector<float> &Z, size_t tri, float &nz,
                      float &shade) {
  const Tri &f = m.tris[tri];
  float ax = X[f.v[0]], ay = Y[f.v[0]], az = Z[f.v[0]];
  float e1x = X[f.v[1]] - ax, e1y = Y[f.v[1]] - ay, e1z = Z[f.v[1]] - az;
  float e2x = X[f.v[2]] - ax, e2y = Y[f.v[2]] - ay, e2z = Z[f.v[2]] - az;
  float nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z,
        nzz = e1x * e2y - e1y * e2x;
  float len = sqrtf(nx * nx + ny * ny + nzz * nzz);
  if (!(len > 0)) {
    nz = 0;
    shade = 0.45f;
    return;
  }
  nx /= len;
  ny /= len;
  nzz /= len;
  nz = nzz;
  float dot = nx * 0.35f + ny * 0.75f + nzz * 0.55f;
  if (dot < 0) dot = -dot;
  if (dot > 1) dot = 1;
  shade = 0.45f + 0.55f * dot;
}

// One billboard mesh at screen (cx,cy), height ~pix px. False -> dot fallback.
inline bool DrawOne(SDL_Renderer *r, Bank &bank, TexCache &tc, const Mesh &m,
                    float yawDeg, float pitch, float pix, float cx, float cy) {
  if (!r || !m.ok || m.xyz.empty() || m.tris.empty() || m.span <= 0 ||
      !(pix >= 2))
    return false;
  std::vector<float> X, Y, Z;
  Rotate(m, yawDeg, pitch, X, Y, Z);
  std::vector<size_t> order;
  order.reserve(m.tris.size());
  for (size_t i = 0; i < m.tris.size(); i++) {
    float nz, sh;
    FaceShade(m, X, Y, Z, i, nz, sh);
    if (nz > 0) order.push_back(i);
  }
  if (order.empty()) return true;  // fully backfacing: drew nothing, no dot
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    const Tri &fa = m.tris[a], &fb = m.tris[b];
    float za = (Z[fa.v[0]] + Z[fa.v[1]] + Z[fa.v[2]]) / 3;
    float zb = (Z[fb.v[0]] + Z[fb.v[1]] + Z[fb.v[2]]) / 3;
    return za < zb;
  });
  SDL_Texture *tex = nullptr;
  int tw = 0, th = 0;
  if (!m.tex0.empty()) {
    tex = tc.Get(r, bank, m.tex0);
    if (tex) {
      tw = tc.w;
      th = tc.h;
      if (tw <= 0 || th <= 0) tex = nullptr;
    }
  }
  float k = pix / m.span;
  std::vector<SDL_Vertex> v;
  v.reserve(order.size() * 3);
  for (size_t ti : order) {
    const Tri &f = m.tris[ti];
    float nz, sh;
    FaceShade(m, X, Y, Z, ti, nz, sh);
    for (int kk = 0; kk < 3; kk++) {
      SDL_Vertex sv;
      sv.position = {cx + X[f.v[kk]] * k, cy - Y[f.v[kk]] * k};
      if (tex) {
        sv.color = {255 * sh, 255 * sh, 255 * sh, 255};
        float u = (float)(f.uv[2 * kk] % (uint32_t)tw) / tw;
        float vv = (float)(f.uv[2 * kk + 1] % (uint32_t)th) / th;
        sv.tex_coord = {u, vv};
      } else {
        sv.color = {m.r * sh, m.g * sh, m.b * sh, 255};
        sv.tex_coord = {0, 0};
      }
      v.push_back(sv);
    }
  }
  return SDL_RenderGeometry(r, tex, v.data(), (int)v.size(), nullptr, 0) == 0;
}

}  // namespace models
