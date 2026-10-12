// Smallest assert: STRUCTURE line + block parse, depth/params/index;
// MOVES function/part/keyframes (short-row carry, NEXT list, DISABLE -,
// P99 part), SHADOWS pairs, linear Sample.
#include <cassert>
#include <cmath>
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

  std::string m =
      "<STARTMOVES>\n<FUNCTION=0=PAIKALLAAN>\n<NEXT=6,8>\n<LENGTH=3000>\n"
      "<DISABLE=13,14>\n****\n<PART=0=BASE>\n"
      "  0   0   0   0\n  3000   0   0   0   0\n"
      "<PART=9=OLKA>\n"
      "  0  10 -90   7\n  500  10  30\n  3000  10 -90   0\n"
      "<PART=99=POS>\n  0 0 0 0\n  3000 0 0 0\n"
      "<FUNCTION=1=LAHTO>\n<NEXT=0>\n<LENGTH=500>\n<DISABLE=->\n"
      "<PART=0=BASE>\n  0 0 0 0\n  500 0 0 0\n<ENDMOVES>\n";
  auto mv = model3ds::ParseMoves(m);
  assert(mv.size() == 2);
  assert(mv[0].id == 0 && mv[0].name == "PAIKALLAAN");
  assert(mv[0].length == 3000);
  assert((mv[0].next == std::vector<int>{6, 8}));
  assert((mv[0].disable == std::vector<int>{13, 14}));
  assert(mv[0].parts.size() == 3 && mv[0].parts[2].index == 99);
  const auto &k = mv[0].parts[1].keys;
  assert(k.size() == 3);
  assert(k[1].t == 500 && k[1].x == 10 && k[1].y == 30 && k[1].z == 7);
  assert(k[2].t == 3000 && k[2].z == 0);
  assert(mv[1].next == std::vector<int>{0} && mv[1].disable.empty());
  auto sm = model3ds::Sample(k, 250);
  assert(fabs(sm.x - 10) < 1e-9 && fabs(sm.y + 30) < 1e-9);
  auto lo = model3ds::Sample(k, -5), hi = model3ds::Sample(k, 9999);
  assert(lo.y == -90 && hi.y == -90);

  std::string s =
      "<STARTSHADOWS>\na.obj shad_a.obj\nb.obj shad_b.obj\n<ENDSHADOWS>\n";
  auto sh = model3ds::ParseShadows(s);
  assert(sh.size() == 2 && sh[0].shadow == "shad_a.obj");

  std::string im =
      "<IMPORTMOVES>\ngfx\\3ds\\human2.3ds\n<ENDIMPORTMOVES>\n";
  assert(model3ds::ParseImportMoves(im) == "gfx\\3ds\\human2.3ds");
  assert(model3ds::ParseImportMoves(m).empty());

  printf("model3ds tests pass\n");
  return 0;
}
