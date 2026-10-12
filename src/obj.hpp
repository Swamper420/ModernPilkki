// .obj mesh layout (FUN_0040d320, tools/obj.py, verified 843/843).
// Magic[18] u32 nvert,u32 ?,u32 nface,u32 nt, nt x 50B names,
// 0x300B block, nvert x 12B verts (3 x i32 xyz, world units; rare
// garbage slot, e.g. rod_5_base vert 0), nface x 0x34B faces
// (u32 idx[3] + u32 aux[3] + u32 uv[6] pixel coords + u32 flags;
// exporter filler uv 0x46500000/0x2EE00000 on untextured faces),
// u32 nf, nf x 12B, footer u32 + 6 x u64 + 0x68B block
// (+'U' conditional (c0+c1+c2) x 0x24B extra).
// Per-face texture select + aux semantics open (flags low byte
// correlates with texture count on small meshes, not on 7-tex ones).
#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace obj {

struct Vert {
  int32_t x = 0, y = 0, z = 0;
};

struct Face {
  uint32_t v[3] = {};
  uint32_t aux[3] = {};
  uint32_t uv[6] = {};  // (u0,v0,u1,v1,u2,v2) in texture pixels
  uint32_t flags = 0;
};

struct Model {
  uint32_t nvert = 0, nface = 0, nt = 0, nf = 0;
  std::vector<std::string> tex;
  std::vector<Vert> verts;
  std::vector<Face> faces;
};

inline uint32_t U32(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

inline Model Parse(const uint8_t *p, size_t n) {
  static const char kMagic[] = "Pro Pilkki Object";
  if (n < 34 || memcmp(p, kMagic, 16) != 0 || p[16] != 't' || p[17] != '\0')
    throw std::runtime_error("bad obj magic");
  Model m;
  m.nvert = U32(p + 18);
  m.nface = U32(p + 26);
  m.nt = U32(p + 30);
  size_t off = 34 + (size_t)m.nt * 0x32 + 0x300;
  if (off > n) throw std::runtime_error("truncated obj tex/block");
  m.tex.reserve(m.nt);
  for (uint32_t i = 0; i < m.nt; i++) {
    const char *s = (const char *)(p + 34 + (size_t)i * 0x32);
    m.tex.emplace_back(s, strnlen(s, 0x32));
  }
  off += (size_t)m.nvert * 12 + (size_t)m.nface * 0x34;
  if (off + 4 > n) throw std::runtime_error("truncated obj faces");
  // Fill verts/faces from the spans above (port: was counts-only).
  size_t vo = 34 + (size_t)m.nt * 0x32 + 0x300;
  m.verts.resize(m.nvert);
  for (uint32_t i = 0; i < m.nvert; i++) {
    int32_t v[3];
    memcpy(v, p + vo + (size_t)i * 12, 12);
    m.verts[i] = {v[0], v[1], v[2]};
  }
  size_t fo = vo + (size_t)m.nvert * 12;
  m.faces.resize(m.nface);
  for (uint32_t i = 0; i < m.nface; i++) {
    const uint8_t *r = p + fo + (size_t)i * 0x34;
    Face f;
    memcpy(f.v, r, 12);
    memcpy(f.aux, r + 12, 12);
    memcpy(f.uv, r + 24, 24);
    memcpy(&f.flags, r + 48, 4);
    for (int k = 0; k < 3; k++)
      if (f.v[k] >= m.nvert) throw std::runtime_error("obj index OOB");
    m.faces[i] = f;
  }
  m.nf = U32(p + off);
  off += 4 + (size_t)m.nf * 12;
  if (off + 4 + 48 + 0x68 > n) throw std::runtime_error("truncated obj footer");
  const uint8_t *blk = p + off + 4 + 48;
  off += 4 + 48 + 0x68;
  if (blk[0] == 'U') {
    uint32_t c0, c1, c2;
    memcpy(&c0, blk + 8, 4);
    memcpy(&c1, blk + 12, 4);
    memcpy(&c2, blk + 16, 4);
    off += (size_t)(c0 + c1 + c2) * 0x24;
  }
  if (off != n) throw std::runtime_error("obj size mismatch");
  return m;
}

inline Model Parse(const std::vector<uint8_t> &d) {
  return Parse(d.data(), d.size());
}

}  // namespace obj
