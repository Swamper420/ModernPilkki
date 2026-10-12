// Minimal .ves lake parser (ProPilkki2.exe FUN_00430770 / FUN_004b6d70).
// Grammar: header + sections; every section = <TAG> u32 len payload u32 hash;
// OBJECT_POSITIONS payload = u32 groupcount + groups (no special framing).
// file ends with u32 hash of all preceding bytes.
// Hash: XOR_j ((byte_j ^ 0x5A) << (j % 24)) (verified all sections, all lakes).
#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace ves {

inline uint32_t Hash(const uint8_t *p, size_t n) {
  uint32_t h = 0;
  for (size_t j = 0; j < n; j++)
    h ^= (uint32_t)(p[j] ^ 0x5A) << (j % 24);
  return h;
}

struct Section {
  std::string tag;
  std::vector<uint8_t> payload;  // OBJECT_POSITIONS: raw span (incl slen/count)
  uint32_t trailer = 0;          // hash (absent for OBJECT_POSITIONS)
  bool checked = false;
};

// OBJECT_POSITIONS record (FUN_004b6d70, tools/objpos.py 48/48 exact).
// Field semantics open: a/b huge as u32, c small; tail mostly nonzero.
// ponytail: plain triple + tail bytes until Ghidra pins semantics.
struct ObjRec {
  uint8_t cls = 0;
  uint32_t a = 0, b = 0, c = 0;
  uint8_t tail[11] = {};
};

using ObjGroup = std::vector<ObjRec>;

inline uint32_t U32p(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

inline std::vector<ObjGroup> ParseObjPos(const uint8_t *p, size_t n) {
  if (n < 4) throw std::runtime_error("truncated objpos");
  uint32_t ng = U32p(p);
  size_t off = 4;
  std::vector<ObjGroup> out;
  for (uint32_t g = 0; g < ng; g++) {
    if (off + 4 > n) throw std::runtime_error("truncated objpos group");
    uint32_t ni = U32p(p + off);
    off += 4;
    if (off + (size_t)ni * 25 > n) throw std::runtime_error("truncated objpos recs");
    ObjGroup grp;
    for (uint32_t i = 0; i < ni; i++) {
      const uint8_t *r = p + off;
      if (r[0] != 0) throw std::runtime_error("bad objpos record");
      ObjRec rec;
      rec.cls = r[1];
      rec.a = U32p(r + 2);
      rec.b = U32p(r + 6);
      rec.c = U32p(r + 10);
      memcpy(rec.tail, r + 14, 11);
      grp.push_back(rec);
      off += 25;
    }
    out.push_back(std::move(grp));
  }
  if (off != n) throw std::runtime_error("objpos size mismatch");
  return out;
}

inline uint32_t U32(const std::vector<uint8_t> &d, size_t o) {
  uint32_t v;
  memcpy(&v, d.data() + o, 4);
  return v;
}

inline std::vector<Section> Parse(const std::vector<uint8_t> &d) {
  static const char kHdr[] = "Pro Pilkki datafile v002\nCopyright Procyon Products\n";
  const size_t kHl = sizeof(kHdr) - 1;
  if (d.size() < kHl || memcmp(d.data(), kHdr, kHl) != 0)
    throw std::runtime_error("bad ves header");
  size_t off = kHl;
  while (off < d.size() && (d[off] == '\n' || d[off] == '\r')) off++;
  std::vector<Section> out;
  while (d.size() - off > 4) {
    if (d[off] != '<') throw std::runtime_error("expected < at " + std::to_string(off));
    size_t end = off;
    while (d[end] != '>') end++;
    std::string tag(d.begin() + off + 1, d.begin() + end);
    Section s;
    s.tag = tag;
    uint32_t len = U32(d, end + 1);
    if (end + 9 + len > d.size()) throw std::runtime_error("truncated " + tag);
    s.payload.assign(d.begin() + end + 5, d.begin() + end + 5 + len);
    s.trailer = U32(d, end + 5 + len);
    s.checked = Hash(s.payload.data(), s.payload.size()) == s.trailer;
    off = end + 9 + len;
    out.push_back(std::move(s));
  }
  // file trailer = hash of everything before it
  uint32_t ft = U32(d, off);
  if (off + 4 != d.size()) throw std::runtime_error("trailing bytes");
  if (Hash(d.data(), off) != ft) throw std::runtime_error("file hash mismatch");
  return out;
}

}  // namespace ves
