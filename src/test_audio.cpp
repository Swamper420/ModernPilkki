// Smallest assert: sniff WAV/Ogg/OSND, chunk iterator finds headers.
// (Real-file check lives in tools/audio.py.)
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

  printf("audio tests pass\n");
  return 0;
}
