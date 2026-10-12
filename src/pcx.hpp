// Custom PCX decoder (ProPilkki2.exe FUN_005fefd0/005fb040/005fb7c0/005fc360,
// FUN_00600aa0 dims de-obfuscation; tools/pcx.py 348/348 exact).
// Header: 0A 05 01 08, box zero, '[' (0x5B) @127. Dims: xmax = s16(@0x42)
// + 0x80D, ymax = s16(@0x46) + 0xBFF; w = xmax+1 (+1 if (xmax-1) odd).
// RLE: byte <= 0xC0 literal else run of (byte-0xC0). Pixels @128:
// planes 1 = indices + 0x0C + 768B palette; 3 = planar RGB (one stream
// per row, runs cross plane slots); 4 = interleaved RGBA (.32.pcx).
#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace pcx {

struct Image {
  int planes = 0, w = 0, h = 0;
  std::vector<uint8_t> px;   // planes 3: RGB; planes 4: RGBA
  std::vector<uint8_t> idx;  // planes 1: indices
  std::vector<uint8_t> pal;  // planes 1: 768 B palette
};

inline int16_t S16(const uint8_t *p) {
  int16_t v;
  memcpy(&v, p, 2);
  return v;
}

struct Cursor {
  const uint8_t *d;
  size_t p, n;
  void Take(int cnt, uint8_t *out) {
    int got = 0;
    while (got < cnt) {
      if (p >= n) throw std::runtime_error("truncated pcx run");
      uint8_t b = d[p++];
      if (b <= 0xC0) {
        out[got++] = b;
      } else {
        if (p >= n) throw std::runtime_error("truncated pcx run");
        uint8_t v = d[p++];
        int run = b - 0xC0, need = cnt - got;
        memset(out + got, v, run < need ? run : need);
        got += run < need ? run : need;
      }
    }
  }
};

inline Image Decode(const uint8_t *d, size_t n) {
  if (n < 128 || d[0] != 10 || d[1] != 5 || d[2] != 1 || d[3] != 8)
    throw std::runtime_error("bad pcx magic");
  if (d[127] != 0x5B) throw std::runtime_error("bad pcx wrapper");
  int planes = d[65];
  if (planes != 1 && planes != 3 && planes != 4)
    throw std::runtime_error("unsupported pcx planes");
  int xmax = S16(d + 0x42) + 0x80D, ymax = S16(d + 0x46) + 0xBFF;
  int w = xmax + 1;
  if (((xmax - 1) & 1) != 0) w++;
  int h = ymax + 1;
  if (w < 1 || h < 1) throw std::runtime_error("bad pcx dims");
  Image im{planes, w, h};
  Cursor s{d, 128, n};
  std::vector<uint8_t> row;
  if (planes == 1) {
    im.idx.resize((size_t)w * h);
    s.Take(w * h, im.idx.data());
    if (n - s.p != 769 || d[s.p] != 0x0C)
      throw std::runtime_error("bad pcx palette trailer");
    im.pal.assign(d + s.p + 1, d + s.p + 769);
    return im;
  }
  if (planes == 3) {
    im.px.resize((size_t)w * h * 3);
    row.resize((size_t)3 * w);
    for (int y = 0; y < h; y++) {
      s.Take(3 * w, row.data());
      for (int x = 0; x < w; x++) {
        size_t o = ((size_t)y * w + x) * 3;
        im.px[o] = row[x];
        im.px[o + 1] = row[w + x];
        im.px[o + 2] = row[2 * w + x];
      }
    }
    if (s.p != n) throw std::runtime_error("pcx size mismatch");
    return im;
  }
  im.px.resize((size_t)w * h * 4);
  row.resize((size_t)4 * w);
  for (int y = 0; y < h; y++) {
    s.Take(4 * w, row.data());
    memcpy(im.px.data() + (size_t)y * w * 4, row.data(), (size_t)4 * w);
  }
  if (s.p != n) throw std::runtime_error("pcx size mismatch");
  return im;
}

inline Image Decode(const std::vector<uint8_t> &d) {
  return Decode(d.data(), d.size());
}

}  // namespace pcx
