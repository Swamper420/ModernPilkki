// Game audio sniff + OSND chunk iterator (tools/audio.py).
// .sfx = PCM WAV, .sfo = 2x plain Ogg + 194x OSND, .mus = OSND.
// OSND = chunks back-to-back, first 58 B header; payloads carry Vorbis
// packets with 'pmusic' for 'vorbis'.
// ponytail: full Ogg-page rebuild deferred to FUN_00401870 decomp.
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

}  // namespace audio
