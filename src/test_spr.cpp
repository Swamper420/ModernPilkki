// Smallest assert: synthetic bpp2 multi-frame + bpp1 RLE round-trip.
// (Real-file check lives in tools/spr.py: 1212/1212.)
#include <cassert>
#include <cstdio>
#include <vector>
#include "spr.hpp"

static void PutU16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back(x & 0xFF);
  v.push_back(x >> 8);
}

int main() {
  // bpp2, 2 frames of 3x2, key 0xF81F, enc 1: (0x1234 x6) + (0x5678 x6).
  std::vector<uint8_t> d = {0x02, 0x02, 0x01, 0x00};
  PutU16(d, 3);
  PutU16(d, 2);
  PutU16(d, 0xF81F);
  PutU16(d, 0);
  PutU16(d, 3);
  PutU16(d, 3);  // t1
  PutU16(d, 0);
  PutU16(d, 0);  // t2
  PutU16(d, 0x1234);
  d.push_back(6);
  PutU16(d, 0x5678);
  d.push_back(6);
  auto s = spr::Decode(d);
  assert(s.nf == 2 && s.bpp == 2 && s.enc == 1);
  assert(s.w == 3 && s.h == 2 && s.key == 0xF81F);
  assert(s.px.size() == 12 && s.px[0] == 0x1234 && s.px[11] == 0x5678);
  assert(s.t1[0] == 3 && s.t2[1] == 0);

  // bpp1, 1 frame 2x1, enc 1 + palette.
  std::vector<uint8_t> e = {0x01, 0x01, 0x01, 0x00};
  PutU16(e, 2);
  PutU16(e, 1);
  PutU16(e, 0x00FE);
  PutU16(e, 0);
  PutU16(e, 2);  // t1
  PutU16(e, 0);  // t2
  e.push_back(7);
  e.push_back(2);  // idx 7 x2
  e.insert(e.end(), 768, 0);
  auto t = spr::Decode(e);
  assert(t.bpp == 1 && t.idx.size() == 2 && t.idx[0] == 7);
  assert(t.pal.size() == 768);

  // Truncated run throws; enc-2 throws.
  d.pop_back();
  bool threw = false;
  try {
    spr::Decode(d);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);
  std::vector<uint8_t> f = {0x06, 0x01, 0x02, 0x35, 0, 0, 0, 0, 0, 0, 0, 0};
  threw = false;
  try {
    spr::Decode(f);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  printf("spr tests pass\n");
  return 0;
}
