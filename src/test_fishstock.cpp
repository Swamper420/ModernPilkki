// test_fishstock.cpp — asserts for src/fishstock.hpp (docs/sim-stock.md):
// DATA1 deobfuscation round-trip, entry record field extraction, placement
// rule, and (when extracted/ is present) one real lake + data0.dat load.
#include "fishstock.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using fishstock::Entry;

void Put32(std::vector<uint8_t> &d, size_t o, uint32_t v) {
  memcpy(d.data() + o, &v, 4);
}
void Put64(std::vector<uint8_t> &d, size_t o, double v) {
  memcpy(d.data() + o, &v, 8);
}

// One hand-built DATA1 record: version 5, one mixture component and one
// spawn area (so 376 bytes = 332 + 44).
std::vector<uint8_t> MakeRecord() {
  std::vector<uint8_t> r(376, 0);
  Put32(r, 0x00, 5);         // version
  Put32(r, 0x04, 0x1c);      // flags
  Put32(r, 0x08, 26);        // type (Made)
  Put32(r, 0x0c, 2000);      // attraction radius
  Put32(r, 0x10, 68);        // ratio denominator
  Put32(r, 0x14, 136);       // ratio numerator
  Put32(r, 0x18, 1);         // area count
  for (int b = 0; b < 9; b++) r[0x1c + b] = (uint8_t)(100 - b);
  r[0x25] = 1;
  r[0x26] = 2;
  Put32(r, 0x28, 7);
  Put64(r, 0x30, 27.5);      // mix[0] mean
  Put64(r, 0x38, 6.25);      // mix[0] sd
  Put64(r, 0x40, 0.5);       // mix[0] share
  Put32(r, 0x140, 0);        // area kind (box)
  Put32(r, 0x14c, 1000);     // x0
  Put32(r, 0x150, 1000);     // y0
  Put32(r, 0x158, 2000);     // x1
  Put32(r, 0x15c, 2000);     // y1
  Put32(r, 0x16c, 50912);    // tail -> entry+0xe60
  Put32(r, 0x170, 17254);
  Put32(r, 0x174, 0);
  return r;
}

std::vector<uint8_t> MakeStream() {
  std::vector<uint8_t> rec = MakeRecord();
  std::vector<uint8_t> s(8 + rec.size(), 0);
  Put32(s, 0, 1);  // count
  Put32(s, 4, (uint32_t)rec.size());
  memcpy(s.data() + 8, rec.data(), rec.size());
  return s;
}

std::string FindExtracted(const char *rel) {
  const char *roots[] = {"extracted/", "../extracted/", "../../extracted/"};
  for (const char *root : roots) {
    std::vector<uint8_t> d = fishstock::ReadWholeFile(std::string(root) + rel);
    if (!d.empty()) return std::string(root) + rel;
  }
  return "";
}

}  // namespace

int main() {
  // 1. Deobfuscation round-trip on a synthetic DATA1 blob.
  {
    std::vector<uint8_t> plain = MakeStream();
    std::vector<uint8_t> enc = plain;
    fishstock::Obfuscate(enc, fishstock::kData1Key);
    assert(enc != plain);
    std::vector<uint8_t> dec = enc;
    fishstock::Deobfuscate(dec, fishstock::kData1Key);
    assert(dec == plain);
    // And the framing survives the trip.
    std::vector<Entry> fish;
    assert(fishstock::ParseData1(dec, fish));
    assert(fish.size() == 1);
  }

  // 2. Entry field extraction on the hand-built record.
  {
    std::vector<uint8_t> dec = MakeStream();
    std::vector<Entry> fish;
    assert(fishstock::ParseData1(dec, fish));
    assert(fish.size() == 1);
    const Entry &f = fish[0];
    assert(f.type == 26);
    assert(f.radius == 2000);
    assert(f.ratioDen == 68 && f.ratioNum == 136);
    assert(f.mat[0] == 100 && f.mat[8] == 92);
    assert(f.f25 == 1 && f.f26 == 2 && f.f28 == 7);
    assert(f.mix[0].mean == 27.5 && f.mix[0].sd == 6.25 && f.mix[0].share == 0.5);
    assert(f.mix[1].share == 0.0);
    assert(f.areas.size() == 1);
    assert(f.areas[0].x0 == 1000 && f.areas[0].y0 == 1000);
    assert(f.areas[0].x1 == 2000 && f.areas[0].y1 == 2000);
    assert(f.e60 == 50912 && f.e64 == 17254 && f.e68 == 0);
    assert(fishstock::WeightG(f) == 28);  // round(27.5)
    // A centred area (kind 1) expands to its bounding box with rx/ry >= 50.
    std::vector<uint8_t> rec = MakeRecord();
    Put32(rec, 0x140, 1);
    Put32(rec, 0x144, 10);   // rx: clamped to 50
    Put32(rec, 0x148, 60);   // ry
    Put32(rec, 0x14c, 4000); // centre x
    Put32(rec, 0x150, 5000); // centre y
    std::vector<uint8_t> s(8 + rec.size(), 0);
    Put32(s, 0, 1);
    Put32(s, 4, (uint32_t)rec.size());
    memcpy(s.data() + 8, rec.data(), rec.size());
    std::vector<Entry> g;
    assert(fishstock::ParseData1(s, g) && g.size() == 1);
    assert(g[0].areas[0].rx == 50 && g[0].areas[0].ry == 60);
    assert(g[0].areas[0].x0 == 3950 && g[0].areas[0].y0 == 4940);
    assert(g[0].areas[0].x1 == 4050 && g[0].areas[0].y1 == 5060);
  }

  // 3. Placement rule: uniform in the spawn box, cell via CellOf.
  {
    std::vector<uint8_t> dec = MakeStream();
    std::vector<Entry> fish;
    assert(fishstock::ParseData1(dec, fish));
    fishstock::Stock st;
    st.fish = fish;
    st.SetSeed(12345);
    st.Place(10, 10);  // 6000 x 6000 grid
    const Entry &f = st.fish[0];
    assert(f.x >= 1000 && f.x <= 2000);
    assert(f.y >= 1000 && f.y <= 2000);
    int cell = f.cell;
    assert(cell == st.CellOf(f.x, f.y));
    int cx = cell % 10, cy = cell / 10;
    assert(st.Count(cx, cy) == 1);
    assert(st.TypeAt(cx, cy, 0) == 26);
    assert(st.TagAt(cx, cy, 0) == 0);
    assert(st.WeightAt(cx, cy, 0) == 28);
    assert(!st.Stocked());  // no type-2 fish
    // The distance gate rejects outside the radius*1.872 reach (005f7ee0).
    bite::Rng rng;
    rng.Seed(1);
    bite::Context near;
    near.x = f.x + 100;
    near.y = f.y;
    bite::Eval e = st.Evaluate(cx, cy, 0, near, rng);
    assert(!e.hardReject);
    bite::Context far;
    far.x = f.x + 5000;
    far.y = f.y;
    e = st.Evaluate(cx, cy, 0, far, rng);
    assert(e.hardReject && !e.bite);
  }

  // 4. Real-lake check (skipped cleanly when extracted/ is absent).
  std::string lake = FindExtracted("lakes/haapolahti.ves");
  std::string dat0 = FindExtracted("data/nyky/data0.dat");
  if (lake.empty() || dat0.empty()) {
    printf("test_fishstock: no extracted/ tree, real-lake check skipped\n");
    return 0;
  }
  {
    fishstock::Stock st;
    assert(st.LoadLake(lake));
    assert(st.LoadSpecies(dat0));
    assert(!st.fish.empty());
    int hist[33] = {};
    for (const Entry &f : st.fish) {
      assert(f.type >= 1 && f.type <= 32);  // species ids in range
      hist[f.type]++;
    }
    st.SetSeed(1);
    st.Place();
    int placed = 0;
    for (int i = 0; i < st.gridW * st.gridH; i++) placed += (int)st.cells[i].size();
    assert(placed == (int)st.fish.size());
    for (int s = 1; s <= 9; s++) assert(st.SpeciesId(s) == s);
    printf("test_fishstock: haapolahti %d fish, grid %dx%d, type histogram:",
           (int)st.fish.size(), st.gridW, st.gridH);
    for (int t = 1; t <= 32; t++)
      if (hist[t]) printf(" %d:%d", t, hist[t]);
    printf("\n");
  }
  printf("test_fishstock: ok\n");
  return 0;
}
