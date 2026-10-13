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

// ---- Original clip registry (FUN_006267f0): slot -> routing ----
// Sound manager (singleton @ DAT_007c8398) keeps 1000 slots at mgr+8+4*i,
// each a 0x128-byte Clip: path[256], +0x100 pan-bias double, +0x108 src,
// +0x10c loop mode, +0x110/+0x114 volL/volR permille, +0x118 crossfade
// param, +0x120 loopStart PCM byte offset. Fields:
// src  0 = WAV via CWaveSoundRead, 1 = Ogg/OSND via COggSoundRead
//      (FUN_0059feb0). The page demuxer FUN_00401870 accepts BOTH `OSND`
//      and `OggS` magics (DAT_00777888 compare vs "OggS"), so the two
//      plain-Ogg Tuuli*.sfo ride the same loader — no special routing.
// loop 0 = one-shot, 1 = loop (static buffer + DSBPLAY_LOOPING),
//      2 = crossfade loop (dual voice A/B, FUN_0062ccb0).
struct Clip {
  int slot;
  const char *path;
  uint8_t src;
  uint8_t loop;
  int volL, volR;      // per-ear permille, 2000 = nominal
  uint32_t loopStart;  // +0x120, PCM bytes; crossfade-loop restart point
};

inline const Clip kClips[] = {
    {0,  "sfx/Menu/glag.sfx",              0, 0, 2000, 2000, 0},
    {1,  "sfx/Menu/click.sfx",             0, 0, 2000, 2000, 0},
    {20, "sfx/Ambient/laukaus.sfx",        0, 0, 2000, 2000, 0},
    {21, "sfx/Ambient/koira2.sfx",         0, 0, 2000, 2000, 0},
    {22, "sfx/Ambient/raakku.sfx",         0, 0, 2000, 2000, 0},
    {23, "sfx/Ambient/Tuuli.sfo",          1, 2, 3000,  100, 132000},
    {24, "sfx/Ambient/Tuuli2.sfo",         1, 2, 1100,  100, 0},
    {25, "sfx/Human/ambient/Punnitus.sfo", 1, 2, 3000, 1000, 0},
    {26, "sfx/Ambient/raakku2.sfx",        0, 0, 2000, 2000, 0},
    {27, "sfx/Ambient/kuikka.sfo",         1, 0,    0,    0, 0},
    {28, "sfx/Ambient/bassloop.sfo",       1, 1,    0,    0, 0},
    {50, "sfx/Effects/KairausPlain.sfo",   1, 2, 3000, 1000, 308700},  // drill
    {51, "sfx/Effects/NostoPlain.sfx",     0, 0, 2000, 2000, 0},       // reel in
    {52, "sfx/Effects/Pohja.sfx",          0, 0, 2000, 2000, 0},       // bottom
    {54, "sfx/Effects/pilkki_veteen_01.sfx", 0, 0, 2000, 2000, 0},     // cast
    {55, "sfx/Effects/pilkki_veteen_02.sfx", 0, 0, 2000, 2000, 0},
    {56, "sfx/Effects/pilkki_veteen_03.sfx", 0, 0, 2000, 2000, 0},
    {57, "sfx/Effects/pilkki_veteen_04.sfx", 0, 0, 2000, 2000, 0},
    {58, "sfx/Effects/pilkki_veteen_05.sfx", 0, 0, 2000, 2000, 0},
    {59, "sfx/Effects/pilkki_veteen_06.sfx", 0, 0, 2000, 2000, 0},
    {70, "sfx/Effects/kuiva_askel1.sfx",   0, 0, 2000, 2000, 0},       // footsteps
    {71, "sfx/Effects/kuiva_askel2.sfx",   0, 0, 2000, 2000, 0},
    {72, "sfx/Effects/kuiva_askel3.sfx",   0, 0, 2000, 2000, 0},
    {73, "sfx/Effects/kuiva_askel4.sfx",   0, 0, 2000, 2000, 0},
    {90, "sfx/Music/P2Menu.mus",           1, 2, 5000,   50, 0},       // menu
    {91, "sfx/Music/P2Results.mus",        1, 0, 2000, 2000, 0},       // results
    {92, "sfx/Music/P2Load.mus",           1, 0, 2000, 2000, 0},       // load
    {93, "sfx/Music/P2Menu2.mus",          1, 0, 2000, 2000, 0},
    {94, "sfx/Music/P2Menu3.mus",          1, 0, 2000, 2000, 0},
};

inline const Clip *ClipBySlot(int slot) {
  for (const Clip &c : kClips)
    if (c.slot == slot) return &c;
  return nullptr;
}

// Music selection (FUN_005c8270, table DAT_007a60d0 = {-1,90,93,94}):
// per-lake style 1..3 -> fixed track, style 0 -> random of the three,
// anything else -> P2Menu. rng advanced by caller-visible LCG.
inline int MusicTrackForStyle(int style, uint32_t &rng) {
  static const int kMenu[3] = {90, 93, 94};
  if (style >= 1 && style <= 3) return kMenu[style - 1];
  if (style == 0) {
    rng = rng * 1103515245u + 12345u;
    return kMenu[(rng >> 16) % 3];
  }
  return 90;
}

// Splash/cast variant pick (random-variant rule of FUN_004c1390) over
// pilkki_veteen slots 54..59.
inline int SplashVariant(uint32_t &rng) {
  rng = rng * 1103515245u + 12345u;
  return 54 + ((rng >> 16) % 6);
}

}  // namespace audio
