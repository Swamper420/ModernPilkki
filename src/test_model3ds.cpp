// Smallest assert: STRUCTURE line + block parse, depth/params/index.
#include <cassert>
#include <cstdio>
#include "model3ds.hpp"

int main() {
  auto p = model3ds::ParseLine(
      "gfx\\obj\\fish\\ahven\\ahv_2.obj 50 000 000 000                 1");
  assert(p.path == "gfx\\obj\\fish\\ahven\\ahv_2.obj");
  assert(p.p[0] == 50 && p.p[1] == 0 && p.index == 1 && p.depth == 0);

  auto q = model3ds::ParseLine(
      " gfx\\obj\\animals\\varis_p.obj      24 000 -27  0          11");
  assert(q.depth == 1 && q.p[0] == 24 && q.p[2] == -27 && q.index == 11);

  std::string d =
      "// comment\n<STARTSTRUCTURE>\n"
      "a.obj 0 000 000 000                 0\n"
      " b.obj 48 000 000 000             10\n"
      "<ENDSTRUCTURE>\n";
  auto v = model3ds::ParseStructure(d);
  assert(v.size() == 2 && v[1].depth == 1 && v[1].index == 10);

  bool threw = false;
  try {
    model3ds::ParseLine("a.obj 1 2 3");
  } catch (...) {
    threw = true;
  }
  assert(threw);
  printf("model3ds tests pass\n");
  return 0;
}
