// botinfo.dat line decoder (FUN_0059b070).
// Each fgets line (stripped of CR/LF, cf. strtok " \r\n\t" at 0059f270)
// is an even-length run of nibble pairs + trailing checksum pair:
//   pair0: lo = s[0]-'a', hi = s[1]-'i'; dh = hi<<4|lo (chain key)
//   mid k: lo = s-'h', hi = s-'c'; byte = (hi<<4|lo) ^ dh
//   last:  ((hi<<4|lo) ^ dh) must equal (dh^0x11) XOR all bytes.
// Nibbles must be < 0x10 (unsigned compare in binary); else line rejected.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace botinfo {

// Decode one stripped line. Returns false on bad length/nibble/checksum.
inline bool DecodeLine(const uint8_t *s, size_t n, std::vector<uint8_t> &out) {
  out.clear();
  if (n < 4 || (n & 1)) return false;
  auto nib = [](uint8_t c, uint8_t base, uint8_t &v) {
    v = (uint8_t)(c - base);
    return v < 0x10;
  };
  uint8_t lo, hi;
  if (!nib(s[0], 'a', lo) || !nib(s[1], 'i', hi)) return false;
  uint8_t dh = (uint8_t)((hi << 4) | lo);
  uint8_t chk = dh ^ 0x11;
  for (size_t k = 0; k < (n - 4) / 2; k++) {
    if (!nib(s[2 + 2 * k], 'h', lo) || !nib(s[3 + 2 * k], 'c', hi))
      return false;
    uint8_t b = (uint8_t)(((hi << 4) | lo) ^ dh);
    out.push_back(b);
    chk ^= b;
  }
  if (!nib(s[n - 2], 'h', lo) || !nib(s[n - 1], 'c', hi)) return false;
  return (uint8_t)(((hi << 4) | lo) ^ dh) == chk;
}

inline bool DecodeLine(const std::string &line, std::string &out) {
  std::vector<uint8_t> v;
  if (!DecodeLine(reinterpret_cast<const uint8_t *>(line.data()), line.size(),
                  v))
    return false;
  out.assign(v.begin(), v.end());
  return true;
}

}  // namespace botinfo
