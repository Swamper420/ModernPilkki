// Smallest assert: synthetic .ves round-trips with original hashes.
// (Real-lake check lives in tools/ves_hash.py: 768/768.)
#include <cassert>
#include <cstdio>
#include "ves.hpp"

static void PutU32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(x & 0xFF);
  v.push_back((x >> 8) & 0xFF);
  v.push_back((x >> 16) & 0xFF);
  v.push_back((x >> 24) & 0xFF);
}

int main() {
  // 1. Hash matches the game: DEPTH_TABLE payload -> 0x04EC5BAE (meri.ves).
  uint32_t depths[] = {0, 30, 60, 130, 160, 200, 250, 300, 350, 400, 500,
                       700, 900, 1000, 1500, 2000, 2500};
  assert(ves::Hash((uint8_t *)depths, sizeof(depths)) == 0x04EC5BAE);

  // 2. Synthetic file parses: header + OBJECT_POSITIONS + 1 section + trailer.
  std::vector<uint8_t> f;
  const char *hdr = "Pro Pilkki datafile v002\nCopyright Procyon Products\n\n";
  f.insert(f.end(), hdr, hdr + strlen(hdr));
  const char *op = "<OBJECT_POSITIONS>";
  f.insert(f.end(), op, op + strlen(op));
  PutU32(f, 4);  // slen: 4 + count + 0 record bytes + trailer
  PutU32(f, 0);  // ngroups = 0
  PutU32(f, 0x12345678);  // OBJECT_POSITIONS trailer (unchecked by game)
  const char *dt = "<DEPTH_TABLE>";
  f.insert(f.end(), dt, dt + strlen(dt));
  PutU32(f, sizeof(depths));
  f.insert(f.end(), (uint8_t *)depths, (uint8_t *)depths + sizeof(depths));
  PutU32(f, 0x04EC5BAE);
  uint32_t ft = ves::Hash(f.data(), f.size());
  PutU32(f, ft);

  auto secs = ves::Parse(f);
  assert(secs.size() == 2);
  assert(secs[0].tag == "OBJECT_POSITIONS");
  assert(secs[1].tag == "DEPTH_TABLE");
  assert(secs[1].checked);

  // 3. Corrupt a byte -> section + file hash fail.
  f[100]++;
  bool threw = false;
  try {
    ves::Parse(f);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);
  printf("ves tests pass\n");
  return 0;
}
