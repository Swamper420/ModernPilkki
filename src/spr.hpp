// Full .spr decoder (ProPilkki2.exe FUN_0062f750, tools/spr.py 1212/1212).
// Header: u8 nframes, bpp(1/2), enc(0 raw/1 RLE/2 interleaved), flags,
// u16 w,h,transkey, u16 extra_len (frames.spr only, unsupported).
// Then u16[nframes] x2 tables, runs (value:bpp + count:u8)*, palette if bpp==1.
#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace spr {

struct Sprite {
  uint8_t nf = 0, bpp = 0, enc = 0, flags = 0;
  uint16_t w = 0, h = 0, key = 0;
  std::vector<uint16_t> t1, t2;
  std::vector<uint16_t> px;   // bpp==2: RGB565, size w*h*nf
  std::vector<uint8_t> idx;   // bpp==1: palette indices, size w*h*nf
  std::vector<uint8_t> pal;   // bpp==1: 256xRGB, 768 B
};

inline uint16_t U16(const uint8_t *p) {
  uint16_t v;
  memcpy(&v, p, 2);
  return v;
}

inline Sprite Decode(const uint8_t *p, size_t n) {
  if (n < 12) throw std::runtime_error("truncated spr header");
  Sprite s;
  s.nf = p[0];
  s.bpp = p[1];
  s.enc = p[2];
  s.flags = p[3];
  s.w = U16(p + 4);
  s.h = U16(p + 6);
  s.key = U16(p + 8);
  if (U16(p + 10) != 0) throw std::runtime_error("spr extra header (enc-2 file)");
  if ((s.bpp != 1 && s.bpp != 2) || s.enc > 2)
    throw std::runtime_error("unsupported spr format");
  if (s.enc == 2) throw std::runtime_error("spr interleaved enc open");
  size_t off = 12;
  if (off + 4 * (size_t)s.nf > n) throw std::runtime_error("truncated spr tables");
  for (int i = 0; i < s.nf; i++) s.t1.push_back(U16(p + off + 2 * i));
  off += 2 * s.nf;
  for (int i = 0; i < s.nf; i++) s.t2.push_back(U16(p + off + 2 * i));
  off += 2 * s.nf;
  size_t total = (size_t)s.w * s.h * s.nf;
  if (s.enc == 0) {
    if (off + total * s.bpp > n) throw std::runtime_error("truncated spr raw");
    if (s.bpp == 2)
      for (size_t i = 0; i < total; i++) s.px.push_back(U16(p + off + 2 * i));
    else
      s.idx.assign(p + off, p + off + total);
    off += total * s.bpp;
  } else {
    size_t got = 0;
    while (got < total) {
      if (off + s.bpp + 1 > n) throw std::runtime_error("truncated spr run");
      uint8_t count = p[off + s.bpp];
      if (count == 0) throw std::runtime_error("zero spr run");
      if (s.bpp == 2) {
        uint16_t color = U16(p + off);
        for (int k = 0; k < count; k++) s.px.push_back(color);
      } else {
        for (int k = 0; k < count; k++) s.idx.push_back(p[off]);
      }
      got += count;
      off += s.bpp + 1;
    }
    if (got != total) throw std::runtime_error("spr pixel count mismatch");
  }
  if (s.bpp == 1) {
    if (off + 768 > n) throw std::runtime_error("truncated spr palette");
    s.pal.assign(p + off, p + off + 768);
    off += 768;
  }
  if (off != n) throw std::runtime_error("spr size mismatch");
  return s;
}

inline Sprite Decode(const std::vector<uint8_t> &d) {
  return Decode(d.data(), d.size());
}

}  // namespace spr
