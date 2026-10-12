// Smallest assert: synthetic .obj round-trips, truncation throws.
// (Real-file check lives in tools/obj.py: 843/843.)
#include <cassert>
#include <cstdio>
#include <vector>
#include "obj.hpp"

static void PutU32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(x & 0xFF);
  v.push_back((x >> 8) & 0xFF);
  v.push_back((x >> 16) & 0xFF);
  v.push_back((x >> 24) & 0xFF);
}

static std::vector<uint8_t> Make() {
  std::vector<uint8_t> d;
  const char *m = "Pro Pilkki Object";
  d.insert(d.end(), m, m + 16);
  d.push_back('t');
  d.push_back('\0');
  PutU32(d, 1);  // nvert
  PutU32(d, 0);  // ?
  PutU32(d, 1);  // nface
  PutU32(d, 1);  // nt
  const char *t = "gfx\\txt\\hum\\HUMSHAD.PCX";
  for (int i = 0; i < 0x32; i++) d.push_back(i < 23 ? t[i] : 0);
  d.insert(d.end(), 0x300, 0);
  PutU32(d, 100);                // vert0 (100,0,200)
  PutU32(d, 0);
  PutU32(d, 200);
  std::vector<uint8_t> face(0x34, 0);
  auto put = [&](size_t o, uint32_t x) {
    face[o] = x & 0xFF;
    face[o + 1] = (x >> 8) & 0xFF;
    face[o + 2] = (x >> 16) & 0xFF;
    face[o + 3] = (x >> 24) & 0xFF;
  };
  put(0, 0);
  put(24, 220);
  put(28, 12);  // uv0 = (220,12)
  put(48, 1);   // flags
  d.insert(d.end(), face.begin(), face.end());
  PutU32(d, 0);                  // nf
  PutU32(d, 100);                // footer u32
  d.insert(d.end(), 48, 0);      // 6 x u64
  d.insert(d.end(), 0x68, 0);    // block (not 'U')
  return d;
}

int main() {
  auto d = Make();
  auto m = obj::Parse(d);
  assert(m.nvert == 1 && m.nface == 1 && m.nt == 1 && m.nf == 0);
  assert(m.tex.size() == 1 && m.tex[0] == "gfx\\txt\\hum\\HUMSHAD.PCX");
  assert(m.verts.size() == 1 && m.verts[0].x == 100 && m.verts[0].z == 200);
  assert(m.faces.size() == 1 && m.faces[0].v[0] == 0);
  assert(m.faces[0].uv[0] == 220 && m.faces[0].uv[1] == 12);
  assert(m.faces[0].flags == 1);

  d.pop_back();
  bool threw = false;
  try {
    obj::Parse(d);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);
  printf("obj tests pass\n");
  return 0;
}
