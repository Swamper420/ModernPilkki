// GameClient LAN checksum (FUN_00463370 / FUN_00446800).
// Packet holds its checksum as u16 at offset 4. Compute: XOR-fold all
// LE u16 words with word@2 treated as 0; odd trailing byte folds into
// low byte. Valid when low16 of fold equals stored word.
// ponytail: no capture verified yet; synthetic round-trip only.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace net {

inline uint16_t Fold(const uint8_t *p, size_t n) {
  uint32_t acc = 0;
  size_t i = 0;
  for (; i + 2 <= n; i += 2) {
    uint16_t w;
    memcpy(&w, p + i, 2);
    if (i == 4) w = 0;
    acc ^= w;
  }
  if (i < n) acc ^= p[i];
  return (uint16_t)(acc & 0xFFFF);
}

inline bool Verify(const uint8_t *p, size_t n) {
  if (n < 6) return false;
  uint16_t stored;
  memcpy(&stored, p + 4, 2);
  return Fold(p, n) == stored;
}

inline bool Verify(const std::vector<uint8_t> &d) {
  return Verify(d.data(), d.size());
}

}  // namespace net
