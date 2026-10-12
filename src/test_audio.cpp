// Smallest assert: sniff WAV/Ogg/OSND, chunk iterator finds headers,
// CRC + rebuild round-trip (synthetic chunk), corruption throws.
// (Real-file check: tools/audio.py rebuilds 199/199, ffmpeg-decodable.)
#include <cassert>
#include <cstdio>
#include <vector>
#include "audio.hpp"

int main() {
  std::vector<uint8_t> wav = {'R', 'I', 'F', 'F', 0, 0, 0, 0,
                              'W', 'A', 'V', 'E'};
  assert(audio::Sniff(wav.data(), wav.size()) == audio::Kind::Wav);
  std::vector<uint8_t> ogg = {'O', 'g', 'g', 'S', 0, 2, 0, 0};
  assert(audio::Sniff(ogg.data(), ogg.size()) == audio::Kind::Ogg);

  // Two back-to-back OSND chunks: 58 B header + 100 B data.
  std::vector<uint8_t> osnd(58 + 100 + 16, 0);
  memcpy(osnd.data(), "OSND", 4);
  memcpy(osnd.data() + 58, "OSND", 4);
  assert(audio::Sniff(osnd.data(), osnd.size()) == audio::Kind::Osnd);
  auto offs = audio::OsndChunks(osnd.data(), osnd.size());
  assert(offs.size() == 2 && offs[0] == 0 && offs[1] == 58);

  std::vector<uint8_t> junk = {1, 2, 3, 4, 5};
  assert(audio::Sniff(junk.data(), junk.size()) == audio::Kind::Unknown);
  assert(audio::OsndChunks(junk.data(), junk.size()).empty());

  // Synthetic 1-segment chunk: OSND + hdr + segcount 1 + lacing 6 +
  // payload 'pmusic'. CRC filled with OggCrc itself (round-trip).
  std::vector<uint8_t> ch(27 + 1 + 6, 0);
  memcpy(ch.data(), "OSND", 4);
  ch[26] = 1;
  ch[27] = 6;
  memcpy(ch.data() + 28, "pmusic", 6);
  assert(audio::OsndChunkLen(ch.data(), ch.size()) == ch.size());
  // zero CRC field first, then stamp
  {
    uint32_t c = audio::OggCrc(ch.data(), ch.size());
    ch[22] = c & 0xFF;
    ch[23] = (c >> 8) & 0xFF;
    ch[24] = (c >> 16) & 0xFF;
    ch[25] = (c >> 24) & 0xFF;
  }
  auto rb = audio::OsndToOgg(ch.data(), ch.size());
  assert(rb.size() == ch.size() && memcmp(rb.data(), "OggS", 4) == 0);
  assert(memcmp(rb.data() + 28, "vorbis", 6) == 0);
  // rebuilt page CRC verifies as stock Ogg
  uint32_t stored = audio::U32le(rb.data() + 22);
  memset(rb.data() + 22, 0, 4);
  assert(audio::OggCrc(rb.data(), rb.size()) == stored);
  ch[30]++;
  bool threw = false;
  try {
    audio::OsndToOgg(ch.data(), ch.size());
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  printf("audio tests pass\n");
  return 0;
}
