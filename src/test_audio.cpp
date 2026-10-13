// Smallest assert: sniff WAV/Ogg/OSND, chunk iterator finds headers,
// CRC + rebuild round-trip (synthetic chunk), corruption throws, and the
// ported clip routing table matches the original registry (FUN_006267f0).
// (Real-file check: tools/audio.py rebuilds 199/199, ffmpeg-decodable.)
#include <cassert>
#include <cstdio>
#include <vector>
#include "audio.hpp"
#include "audio_play.hpp"

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

  // Routing table matches original clip registry (FUN_006267f0 dump):
  using audio::ClipBySlot;
  const audio::Clip *c;
  c = ClipBySlot(50);  // drill: OSND/Ogg, crossfade loop from byte 308700
  assert(c && c->src == 1 && c->loop == 2 && c->loopStart == 308700);
  assert(c->volL == 3000 && c->volR == 1000);
  c = ClipBySlot(51);  // reel-in: plain WAV one-shot
  assert(c && c->src == 0 && c->loop == 0 && strstr(c->path, "NostoPlain"));
  c = ClipBySlot(23);  // wind: 1 of 2 non-OSND plain-Ogg .sfo, same loader
  assert(c && c->src == 1 && c->loop == 2 && c->loopStart == 132000);
  c = ClipBySlot(90);  // menu music: Ogg crossfade loop, 5000/50 balance
  assert(c && c->src == 1 && c->loop == 2 && c->volL == 5000 && c->volR == 50);
  c = ClipBySlot(92);  // load music: Ogg one-shot
  assert(c && c->src == 1 && c->loop == 0);
  assert(ClipBySlot(54)->src == 0 && ClipBySlot(59)->src == 0);  // cast WAVs
  assert(ClipBySlot(999) == nullptr);

  // Music selection (FUN_005c8270): style 1..3 fixed, style 0 random.
  uint32_t rng = 1;
  assert(audio::MusicTrackForStyle(1, rng) == 90);
  assert(audio::MusicTrackForStyle(2, rng) == 93);
  assert(audio::MusicTrackForStyle(3, rng) == 94);
  assert(audio::MusicTrackForStyle(7, rng) == 90);  // default
  for (int i = 0; i < 50; i++) {
    int t = audio::MusicTrackForStyle(0, rng);
    assert(t == 90 || t == 93 || t == 94);
  }
  // Splash variant pick stays within pilkki_veteen slots 54..59.
  for (int i = 0; i < 100; i++) {
    int s = audio::SplashVariant(rng);
    assert(s >= 54 && s <= 59);
  }

  // Synth buffers are non-silent (playback fallback when files missing).
  auto tone = audio::play::Tone(440.0f, 0.1f);
  assert(!tone.empty() && audio::play::MaxAbs(tone) > 0.01f);
  auto wind = audio::play::NoiseWind(0.2f);
  assert(!wind.empty() && audio::play::MaxAbs(wind) > 0.001f);
  auto jing = audio::play::Jingle({523.25f, 784.0f}, 0.05f);
  assert(!jing.empty() && jing.size() >= tone.size() - 2 &&
         jing.size() <= tone.size() + 2 && audio::play::MaxAbs(jing) > 0.01f);
  // Synthetic S16 mono WAV converts to non-silent F32 mono.
  {
    SDL_AudioSpec spec{SDL_AUDIO_S16, 1, 22050};
    std::vector<uint8_t> raw(200 * 2);
    for (int i = 0; i < 200; i++) {
      int16_t s = (int16_t)(30000.0f * sinf(6.2831853f * 440.0f * i / 22050));
      memcpy(raw.data() + 2 * i, &s, 2);
    }
    auto pcm = audio::play::WavToMonoF32(spec, raw.data(), raw.size());
    assert(pcm.size() == 200 && audio::play::MaxAbs(pcm) > 0.5f);
  }
  printf("audio play tests pass\n");
  return 0;
}
