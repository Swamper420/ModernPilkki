// Game audio sniff + OSND chunk demux (tools/audio.py, FUN_00401870).
// .sfx = PCM WAV, .sfo = 2x plain Ogg + 194x OSND, .mus = OSND.
// OSND chunk = Ogg page with magic swapped: `OSND` + ver/type/granule/
// serial/seq (same offsets as Ogg) + u32 CRC @22 (standard Ogg CRC32 over
// chunk with field zeroed) + u8 segcount @26 + lacing + payload.
// Vorbis header packets carry 'pmusic' for 'vorbis'. Rebuild (magic swap
// + pmusic patch + CRC) yields stock Ogg Vorbis (199/199 ffmpeg-decodable).
#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace audio {

enum class Kind { Wav, Ogg, Osnd, Unknown };

inline Kind Sniff(const uint8_t *p, size_t n) {
  if (n >= 12 && memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WAVE", 4) == 0)
    return Kind::Wav;
  if (n >= 5 && memcmp(p, "OggS", 4) == 0 && p[4] == 0) return Kind::Ogg;
  if (n >= 4 && memcmp(p, "OSND", 4) == 0) return Kind::Osnd;
  return Kind::Unknown;
}

// Offsets of every OSND chunk header in [p, p+n). Empty if not OSND.
inline std::vector<size_t> OsndChunks(const uint8_t *p, size_t n) {
  std::vector<size_t> out;
  if (Sniff(p, n) != Kind::Osnd) return out;
  for (size_t i = 0; i + 4 <= n; i++)
    if (memcmp(p + i, "OSND", 4) == 0) out.push_back(i);
  if (out.empty() || out[0] != 0) throw std::runtime_error("bad osnd framing");
  return out;
}

// Standard Ogg CRC32 (poly 0x04C11DB7, init 0), as FUN_004016a0.
inline uint32_t OggCrc(const uint8_t *p, size_t n) {
  static uint32_t tab[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t r = i << 24;
      for (int k = 0; k < 8; k++)
        r = (r & 0x80000000) ? (r << 1) ^ 0x04C11DB7 : r << 1;
      tab[i] = r;
    }
    init = true;
  }
  uint32_t c = 0;
  for (size_t i = 0; i < n; i++)
    c = tab[((c >> 24) ^ p[i]) & 0xFF] ^ (c << 8);
  return c;
}

inline uint32_t U32le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

// Byte size of chunk at p (fun_00401870 framing: header = 27 + segcount,
// payload = lacing sum). Throws on truncation. CRC unchecked here.
inline size_t OsndChunkLen(const uint8_t *p, size_t n) {
  if (n < 27 || memcmp(p, "OSND", 4) != 0)
    throw std::runtime_error("bad osnd chunk");
  uint8_t segs = p[26];
  if (n < (size_t)27 + segs) throw std::runtime_error("truncated osnd head");
  size_t plen = 0;
  for (uint8_t i = 0; i < segs; i++) plen += p[27 + i];
  if (n < (size_t)27 + segs + plen)
    throw std::runtime_error("truncated osnd payload");
  return 27 + segs + plen;
}

// Rebuild stock Ogg Vorbis: per chunk verify CRC, swap magic, patch
// 'pmusic'->'vorbis', recompute CRC. ponytail: blind replace-all; a
// 'pmusic' byte run inside compressed audio would mis-patch, none seen.
inline std::vector<uint8_t> OsndToOgg(const uint8_t *p, size_t n) {
  if (Sniff(p, n) != Kind::Osnd) throw std::runtime_error("not osnd");
  std::vector<uint8_t> out;
  size_t pos = 0;
  while (pos < n) {
    size_t len = OsndChunkLen(p + pos, n - pos);
    std::vector<uint8_t> pg(p + pos, p + pos + len);
    uint32_t stored = U32le(pg.data() + 22);
    memset(pg.data() + 22, 0, 4);
    if (OggCrc(pg.data(), pg.size()) != stored)
      throw std::runtime_error("osnd crc mismatch");
    memcpy(pg.data(), "OggS", 4);
    for (size_t i = 0; i + 6 <= pg.size(); i++)
      if (memcmp(pg.data() + i, "pmusic", 6) == 0)
        memcpy(pg.data() + i, "vorbis", 6);
    memset(pg.data() + 22, 0, 4);
    uint32_t c = OggCrc(pg.data(), pg.size());
    pg[22] = c & 0xFF;
    pg[23] = (c >> 8) & 0xFF;
    pg[24] = (c >> 16) & 0xFF;
    pg[25] = (c >> 24) & 0xFF;
    out.insert(out.end(), pg.begin(), pg.end());
    pos += len;
  }
  return out;
}

}  // namespace audio
