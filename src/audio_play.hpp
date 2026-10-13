// Audible game audio: synth SFX + WAV playback, no new deps.
// Routing matches original clip registry (FUN_006267f0, docs/audio.md):
// .sfx = PCM WAV (SDL_LoadWAV): cast = pilkki_veteen_01..06 (random pick,
// slots 54..59), reel-in = NostoPlain (slot 51). .mus/.sfo = OSND/Ogg; no
// SDL3 Vorbis decoder and no new deps -> synth fallback (Drill stands in
// for KairausPlain.sfo slot 50). Missing files = synth, never hard-fails.
#pragma once
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "audio.hpp"

namespace audio {
namespace play {

constexpr int kRate = 22050;

inline std::vector<float> Tone(float freq, float sec, int rate = kRate,
                               float vol = 0.5f) {
  if (freq <= 0 || sec <= 0 || rate <= 0) return {};
  size_t n = (size_t)(sec * rate);
  if (!n) return {};
  std::vector<float> o(n);
  const float ph = 6.2831853f * freq / (float)rate;
  float atk = 0.005f * rate, rel = 0.05f * rate;
  if (atk < 1) atk = 1;
  if (rel < 1) rel = 1;
  for (size_t i = 0; i < n; i++) {
    float e = i / atk;
    float d = (n - i) / rel;
    if (e > 1) e = 1;
    if (d > 1) d = 1;
    o[i] = vol * sinf(ph * (float)i) * (e < d ? e : d);
  }
  return o;
}

// Deterministic wind: lowpassed LCG noise + slow whistle wobble.
inline std::vector<float> NoiseWind(float sec, int rate = kRate,
                                    float vol = 0.22f) {
  if (sec <= 0 || rate <= 0) return {};
  size_t n = (size_t)(sec * rate);
  if (!n) return {};
  std::vector<float> o(n);
  uint32_t s = 12345;
  float y = 0;
  for (size_t i = 0; i < n; i++) {
    s = s * 1103515245u + 12345u;
    float w = (float)((s >> 8) & 0xFFFF) / 32768.0f - 1.0f;
    y += 0.04f * (w - y);
    float t = (float)i / rate;
    float whistle =
        0.25f * sinf(6.2831853f * 480.0f * t + 3.0f * sinf(6.2831853f * 0.4f * t));
    o[i] = vol * (y * 1.5f + whistle * 0.3f);
  }
  return o;
}

inline std::vector<float> Jingle(const std::vector<float> &freqs,
                                 float noteSec = 0.1f, int rate = kRate,
                                 float vol = 0.45f) {
  std::vector<float> o;
  for (float f : freqs) {
    auto t = Tone(f, noteSec, rate, vol);
    o.insert(o.end(), t.begin(), t.end());
  }
  return o;
}

inline float MaxAbs(const std::vector<float> &p) {
  float m = 0;
  for (float v : p) {
    float a = v < 0 ? -v : v;
    if (a > m) m = a;
  }
  return m;
}

// WAV bytes -> mono F32 @outRate. Handles U8/S8/S16/S32/F32, any channels,
// naive linear resample. Unknown format = empty (caller falls back).
inline std::vector<float> WavToMonoF32(const SDL_AudioSpec &spec,
                                       const uint8_t *data, uint32_t len,
                                       int outRate = kRate) {
  if (!data || !len || spec.channels <= 0 || spec.freq <= 0 || outRate <= 0)
    return {};
  int bps = 0, kind = 0;  // 1=u8 2=s8 3=s16 4=s32 5=f32
  if (spec.format == SDL_AUDIO_U8) { bps = 1; kind = 1; }
  else if (spec.format == SDL_AUDIO_S8) { bps = 1; kind = 2; }
  else if (spec.format == SDL_AUDIO_S16) { bps = 2; kind = 3; }
  else if (spec.format == SDL_AUDIO_S32) { bps = 4; kind = 4; }
  else if (spec.format == SDL_AUDIO_F32) { bps = 4; kind = 5; }
  else return {};
  int ch = spec.channels;
  size_t frames = len / (size_t)(bps * ch);
  if (!frames) return {};
  std::vector<float> mono(frames);
  for (size_t f = 0; f < frames; f++) {
    double acc = 0;
    for (int c = 0; c < ch; c++) {
      const uint8_t *q = data + (f * ch + c) * bps;
      float v = 0;
      if (kind == 1) v = ((int)q[0] - 128) / 128.0f;
      else if (kind == 2) v = (int8_t)q[0] / 128.0f;
      else if (kind == 3) { int16_t s; memcpy(&s, q, 2); v = s / 32768.0f; }
      else if (kind == 4) { int32_t s; memcpy(&s, q, 4); v = (float)(s / 2147483648.0); }
      else { memcpy(&v, q, 4); if (v > 1) v = 1; if (v < -1) v = -1; }
      acc += v;
    }
    mono[f] = (float)(acc / ch);
  }
  if (spec.freq == outRate) return mono;
  size_t n = frames * (size_t)outRate / (size_t)spec.freq;
  if (!n) return {};
  std::vector<float> o(n);
  for (size_t i = 0; i < n; i++) {
    float pos = (float)i * spec.freq / outRate;
    size_t a = (size_t)pos;
    if (a + 1 >= frames) { o[i] = mono.back(); continue; }
    o[i] = mono[a] + (mono[a + 1] - mono[a]) * (pos - a);
  }
  return o;
}

struct Player {
  SDL_AudioStream *stream = nullptr;
  bool muted = false;
  std::vector<float> ambient;
  // Original routing (docs/audio.md): splash/cast = pilkki_veteen_01..06.sfx
  // (slots 54..59, one picked at random per trigger), reel-in = NostoPlain
  // .sfx (slot 51). Both plain WAV -> real files. Drill = KairausPlain.sfo
  // (slot 50) is OSND; no Vorbis decoder without new deps -> synth fallback.
  std::vector<std::vector<float>> splash;  // 6 variants, empty = synth
  std::vector<float> reel;                 // NostoPlain.sfx, empty = synth
  uint32_t rng = 0x2f6e2b1u;               // LCG for variant pick (FUN_0059db20-style)
  int nSfx = 0, nMus = 0, nSfo = 0;

  Player() = default;
  Player(const Player &) = delete;
  Player &operator=(const Player &) = delete;
  ~Player() { Close(); }

  void Close() {
    if (stream) SDL_DestroyAudioStream(stream);
    stream = nullptr;
  }

  bool Init() {
    SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kRate};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                       &spec, nullptr, nullptr);
    if (!stream) return false;
    ambient = NoiseWind(4.0f);
    SDL_ResumeAudioStreamDevice(stream);
    Update();
    return true;
  }

  void Play(const std::vector<float> &pcm) {
    if (!stream || muted || pcm.empty()) return;
    SDL_PutAudioStreamData(stream, pcm.data(),
                           (int)(pcm.size() * sizeof(float)));
  }
  void Drill() {  // auger growl: 75 Hz + 150 Hz octave (KairausPlain.sfo = OSND, synth stand-in)
    auto a = Tone(75.0f, 0.5f, kRate, 0.5f);
    auto b = Tone(150.0f, 0.5f, kRate, 0.25f);
    for (size_t i = 0; i < a.size() && i < b.size(); i++) a[i] += b[i];
    Play(a);
  }
  void Jig() { Play(Tone(880.0f, 0.07f, kRate, 0.4f)); }
  void Splash() {  // random of pilkki_veteen_01..06 (slots 54..59), else jingle
    const std::vector<float> *v = nullptr;
    for (const auto &c : splash)
      if (!c.empty()) { v = &c; break; }
    if (v) {
      rng = rng * 1103515245u + 12345u;
      size_t pick = splash.size();
      size_t seen = 0, want = (rng >> 16) % 6;
      for (size_t i = 0; i < splash.size(); i++)
        if (!splash[i].empty() && seen++ == want) { pick = i; break; }
      Play(splash[pick]);
      return;
    }
    Play(Jingle({660.0f, 880.0f}, 0.09f));
  }
  void Catch() {  // reel-in: NostoPlain.sfx (slot 51), else jingle
    if (!reel.empty()) { Play(reel); return; }
    Play(Jingle({523.25f, 659.25f, 783.99f, 1046.5f}, 0.11f));
  }
  void ToggleMute() {
    muted = !muted;
    if (!stream) return;
    if (muted) SDL_PauseAudioStreamDevice(stream);
    else SDL_ResumeAudioStreamDevice(stream);
  }
  void Update() {  // re-queue ambient loop when running low
    if (!stream || muted || ambient.empty()) return;
    if (SDL_GetAudioStreamQueued(stream) < kRate * (int)sizeof(float))
      SDL_PutAudioStreamData(stream, ambient.data(),
                             (int)(ambient.size() * sizeof(float)));
  }

  // Runtime scan of extracted tree (read-only, never committed).
  // Loads the original routing targets: 6x pilkki_veteen cast WAVs (slots
  // 54..59) + NostoPlain.sfx reel-in (slot 51). Missing files = synth,
  // never hard-fails. OSND/Ogg clips (.sfo/.mus) are counted only.
  int Scan(const char *root = "extracted/sfx") {
    try {
      std::error_code ec;
      if (!std::filesystem::exists(root, ec)) {
        printf("audio scan: no %s (synth fallback)\n", root);
        return 0;
      }
      std::filesystem::recursive_directory_iterator it(root, ec), end;
      for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        std::string p = it->path().string();
        auto endswith = [&](const char *s) {
          size_t a = strlen(s);
          return p.size() >= a && p.compare(p.size() - a, a, s) == 0;
        };
        if (endswith(".sfx")) nSfx++;
        else if (endswith(".mus")) nMus++;
        else if (endswith(".sfo")) nSfo++;
      }
      auto loadWav = [&](const std::string &cand, std::vector<float> &out) {
        SDL_AudioSpec spec{};
        uint8_t *buf = nullptr;
        uint32_t blen = 0;
        if (SDL_LoadWAV(cand.c_str(), &spec, &buf, &blen) && buf) {
          out = WavToMonoF32(spec, buf, blen);
          if (out.size() > (size_t)kRate * 5) out.resize((size_t)kRate * 5);
          SDL_free(buf);
        }
      };
      splash.resize(6);
      for (int i = 0; i < 6; i++) {
        char name[64];
        snprintf(name, sizeof(name), "/Effects/pilkki_veteen_0%d.sfx", i + 1);
        loadWav(std::string(root) + name, splash[i]);
      }
      loadWav(std::string(root) + "/Effects/NostoPlain.sfx", reel);
    } catch (...) {
    }
    size_t nsplash = 0;
    for (const auto &c : splash) nsplash += !c.empty();
    printf("audio scan: %d sfx %d mus %d sfo (%d/6 cast, reel %s)\n", nSfx,
           nMus, nSfo, (int)nsplash, reel.empty() ? "synth" : "file");
    return nSfx + nMus + nSfo;
  }
};

}  // namespace play
}  // namespace audio
