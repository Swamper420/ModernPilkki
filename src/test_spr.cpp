// Smallest assert: synthetic magic-01 sprite round-trips.
#include <cassert>
#include <cstdio>
#include <vector>
#include "spr.hpp"

static void PutU16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(x & 0xFF);
  v.push_back(x >> 8);
}

int main() {
  // 3x2, key 0xF81F: (0x1234 x3) + (0x5678 x3).
  std::vector<uint8_t> d = {0x01, 0x02, 0x01, 0x00};
  PutU16(d, 3);
  PutU16(d, 2);
  PutU16(d, 0xF81F);
  PutU16(d, 0);
  PutU16(d, 3);
  PutU16(d, 0);
  PutU16(d, 0x1234);
  d.push_back(3);
  PutU16(d, 0x5678);
  d.push_back(3);

  auto im = spr::Decode(d);
  assert(im.w == 3 && im.h == 2 && im.key == 0xF81F);
  assert(im.px.size() == 6);
  assert(im.px[0] == 0x1234 && im.px[2] == 0x1234);
  assert(im.px[3] == 0x5678 && im.px[5] == 0x5678);

  // Truncated run throws.
  d.pop_back();
  bool threw = false;
  try {
    spr::Decode(d);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  // Bad magic throws.
  std::vector<uint8_t> bad = {0x02, 0x02, 0x01, 0x00, 0, 0, 0, 0,
                              0,    0,    0,    0,    0, 0, 0, 0};
  threw = false;
  try {
    spr::Decode(bad);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  printf("spr tests pass\n");
  return 0;
}
