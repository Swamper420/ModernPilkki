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
