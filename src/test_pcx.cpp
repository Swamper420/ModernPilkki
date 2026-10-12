// Smallest assert: synthetic 2x1 images decode per planes mode.
// (Real-file check lives in tools/pcx.py: 348/348.)
#include <cassert>
#include <cstdio>
#include <vector>
#include "pcx.hpp"

static void PutS16(std::vector<uint8_t> &v, int x) {
  v.push_back(x & 0xFF);
  v.push_back((x >> 8) & 0xFF);
}

static std::vector<uint8_t> Hdr(int planes, int w, int h) {
  std::vector<uint8_t> d(128, 0);
  d[0] = 10;
  d[1] = 5;
  d[2] = 1;
  d[3] = 8;
  d[65] = planes;
  d[66] = (w - 1 - 0x80D) & 0xFF;
  d[67] = ((w - 1 - 0x80D) >> 8) & 0xFF;
  d[70] = (h - 1 - 0xBFF) & 0xFF;
  d[71] = ((h - 1 - 0xBFF) >> 8) & 0xFF;
  d[127] = 0x5B;
  return d;
}

static void Emit(std::vector<uint8_t> &d, uint8_t v, int cnt) {
  if (cnt == 1 && v <= 0xC0) {
    d.push_back(v);
    return;
  }
  while (cnt > 0) {
    int run = cnt > 63 ? 63 : cnt;
    d.push_back(0xC0 + run);
    d.push_back(v);
    cnt -= run;
  }
}

int main() {
  // planes 3, 2x1: row stream R R G G B B with a run crossing plane slot.
  {
    auto d = Hdr(3, 2, 1);
    Emit(d, 7, 3);  // R R + first G
    Emit(d, 8, 1);
    Emit(d, 9, 2);
    auto im = pcx::Decode(d);
    assert(im.w == 2 && im.h == 1 && im.planes == 3);
    assert(im.px[0] == 7 && im.px[3] == 7);  // R talk
    assert(im.px[1] == 7 && im.px[4] == 8);  // G talk
    assert(im.px[2] == 9 && im.px[5] == 9);  // B talk
  }
  // planes 1, 2x1 indices + palette.
  {
    auto d = Hdr(1, 2, 1);
    Emit(d, 4, 1);
    Emit(d, 200, 1);  // >0xC0 must be run-encoded
    d.push_back(0x0C);
    d.insert(d.end(), 768, 0);
    auto im = pcx::Decode(d);
    assert(im.idx.size() == 2 && im.idx[0] == 4 && im.idx[1] == 200);
    assert(im.pal.size() == 768);
  }
  // planes 4, 2x1 interleaved RGBA (widths pad even like standard PCX).
  {
    auto d = Hdr(4, 2, 1);
    d.insert(d.end(), {10, 20, 30, 40, 50, 60, 70, 80});
    auto im = pcx::Decode(d);
    assert(im.w == 2 && im.px[0] == 10 && im.px[3] == 40);
    assert(im.px[4] == 50 && im.px[7] == 80);
  }
  // Bad wrapper + truncated run throw.
  {
    auto d = Hdr(3, 2, 1);
    d[127] = 0;
    bool threw = false;
    try {
      pcx::Decode(d);
    } catch (const std::runtime_error &) {
      threw = true;
    }
    assert(threw);
    d = Hdr(3, 2, 1);
    d.push_back(0xFF);
    threw = false;
    try {
      pcx::Decode(d);
    } catch (const std::runtime_error &) {
      threw = true;
    }
    assert(threw);
  }
  printf("pcx tests pass\n");
  return 0;
}
