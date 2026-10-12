// Smallest assert: synthetic .ves round-trips with original hashes.
// (Real-lake check lives in tools/ves_hash.py: 816/816.)
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

  // 4. OBJECT_POSITIONS: 1 group x 2 records round-trips, truncation throws.
  std::vector<uint8_t> ob;
  PutU32(ob, 1);  // ngroups
  PutU32(ob, 2);  // nitems
  // rec0: cls 79, x = 89.0 in 8.24, y = -77*2^24+316 (real meri bytes)
  uint8_t r0[25] = {0,   79,  0,    0,    0,    0, 0, 0, 0x59,
                    0x3c, 1,  0,    0xB3, 0,    0, 0, 0, 0};  // rest zero
  uint8_t r1[25] = {0, 5};
  ob.insert(ob.end(), r0, r0 + 25);
  ob.insert(ob.end(), r1, r1 + 25);
  auto grps = ves::ParseObjPos(ob.data(), ob.size());
  assert(grps.size() == 1 && grps[0].size() == 2);
  assert(grps[0][0].cls == 79 && grps[0][0].x == 89 * 16777216);
  assert(grps[0][0].y == -77 * 16777216 + 316);  // low bits kept raw
  assert(grps[0][1].cls == 5 && grps[0][1].z == 0);
  ob.pop_back();
  threw = false;
  try {
    ves::ParseObjPos(ob.data(), ob.size());
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  // 5. Grid: literals + run + overrun clip + 768 B palette split.
  std::vector<uint8_t> gp = {0x0a, 0x05, 0x01, 0x08, 0, 0, 0, 0,
                             4, 0, 2, 0, 0xc8, 0, 0xc8, 0,
                             7, 0xc2, 9, 0xc6, 3};  // 1+2+6=9, clip to 8
  gp.insert(gp.end(), 768, 0x7e);                    // fake palette
  auto gr = ves::ParseGrid(gp);
  assert(gr.w == 4 && gr.h == 2 && gr.cells.size() == 8);
  uint8_t want[8] = {7, 9, 9, 3, 3, 3, 3, 3};  // last run clipped 6->5
  assert(memcmp(gr.cells.data(), want, 8) == 0);
  assert(gr.pal.size() == 768 && gr.pal[0] == 0x7e);

  printf("ves tests pass\n");
  return 0;
}
