// Smallest assert: synthetic cube projects finite + culls one winding;
// one real extracted .obj parses, projects finite, shade finite.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "models.hpp"

static bool Finite(float v) { return v == v && v != 1e30f; }

static models::Mesh Cube() {
  models::Mesh m;
  float c[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                   {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
  for (auto &p : c) {
    m.xyz.push_back(p[0]);
    m.xyz.push_back(p[1]);
    m.xyz.push_back(p[2]);
  }
  uint32_t t[12][3] = {{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6},
                       {0, 4, 5}, {0, 5, 1}, {2, 6, 7}, {2, 7, 3},
                       {0, 3, 7}, {0, 7, 4}, {1, 5, 6}, {1, 6, 2}};
  for (auto &q : t) {
    models::Tri tri;
    tri.v[0] = q[0];
    tri.v[1] = q[1];
    tri.v[2] = q[2];
    m.tris.push_back(tri);
  }
  m.span = 2;
  m.ok = true;
  return m;
}

int main() {
  models::Mesh cu = Cube();
  std::vector<float> X, Y, Z;
  models::Rotate(cu, 30, 0.55f, X, Y, Z);
  assert(X.size() == 8);
  for (float v : X) assert(Finite(v));
  for (float v : Y) assert(Finite(v));
  for (float v : Z) assert(Finite(v));
  size_t vis = 0;
  for (size_t i = 0; i < cu.tris.size(); i++) {
    float nz, sh;
    models::FaceShade(cu, X, Y, Z, i, nz, sh);
    assert(Finite(nz) && Finite(sh) && sh >= 0.44f && sh <= 1.0f);
    if (nz > 0) vis++;
  }
  assert(vis > 0 && vis < cu.tris.size());  // cull drops back faces only

  // Real file: candidates for repo-layout vs build-dir CWD.
  const char *cands[] = {"extracted/gfx/obj/nature/kuusi2.obj",
                         "../extracted/gfx/obj/nature/kuusi2.obj",
                         "../../extracted/gfx/obj/nature/kuusi2.obj"};
  std::string hit;
  for (auto c : cands) {
    FILE *f = fopen(c, "rb");
    if (f) {
      fclose(f);
      hit = c;
      break;
    }
  }
  if (hit.empty()) {
    printf("models tests pass (synthetic only, no extracted/)\n");
    return 0;
  }
  obj::Model o = obj::Parse(models::ReadFile(hit));
  assert(o.nvert > 0 && o.nface > 0 && !o.tex.empty());
  models::Bank bank;
  size_t slash = hit.find("extracted");
  bank.root = hit.substr(0, slash + 9);
  const models::Mesh &m =
      bank.Get("gfx\\obj\\nature\\kuusi2.obj");  // backslash ok
  assert(m.ok && !m.xyz.empty() && !m.tris.empty());
  assert(!m.tex0.empty());  // texture wired (kuusi3.pcx)
  models::Rotate(m, 30, 0.55f, X, Y, Z);
  for (float v : X) assert(Finite(v));
  for (float v : Y) assert(Finite(v));
  for (float v : Z) assert(Finite(v));
  size_t rv = 0;
  for (size_t i = 0; i < m.tris.size(); i++) {
    float nz, sh;
    models::FaceShade(m, X, Y, Z, i, nz, sh);
    assert(Finite(nz) && Finite(sh));
    if (nz > 0) rv++;
  }
  assert(rv > 0 && rv <= m.tris.size());
  printf("models tests pass (real %s nv=%zu nt=%zu)\n", hit.c_str(),
         m.xyz.size() / 3, m.tris.size());
  return 0;
}
