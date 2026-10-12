// Magic-01 .spr decoder (tools/spr.py, verified 1094/1094).
// Header: u32 0x00010201, u16 w,h,key, u16 0, u16 w, u16 0.
// Pixels: (u16 RGB565, u8 count)*, sum(count) == w*h. Count 1..255.
#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace spr {

struct Image {
  uint16_t w = 0, h = 0, key = 0;
  std::vector<uint16_t> px;  // row-major, RGB565, size w*h
};

inline uint16_t U16(const uint8_t *p) {
  uint16_t v;
  memcpy(&v, p, 2);
  return v;
}

inline Image Decode(const uint8_t *p, size_t n) {
  if (n < 16 || memcmp(p, "\x01\x02\x01\x00", 4) != 0)
    throw std::runtime_error("bad spr magic");
  Image im;
  im.w = U16(p + 4);
  im.h = U16(p + 6);
  im.key = U16(p + 8);
  if (U16(p + 10) != 0 || U16(p + 14) != 0 || U16(p + 12) != im.w)
    throw std::runtime_error("bad spr header");
  im.px.reserve((size_t)im.w * im.h);
  for (size_t i = 16; i < n;) {
    if (i + 3 > n) throw std::runtime_error("truncated spr run");
    uint16_t color = U16(p + i);
    uint8_t count = p[i + 2];
    if (count == 0) throw std::runtime_error("zero spr run");
    for (int k = 0; k < count; k++) im.px.push_back(color);
    i += 3;
  }
  if (im.px.size() != (size_t)im.w * im.h)
    throw std::runtime_error("spr pixel count mismatch");
  return im;
}

inline Image Decode(const std::vector<uint8_t> &d) {
  return Decode(d.data(), d.size());
}

}  // namespace spr
