// Smallest assert: two blocks, row fields, special filenames, marker row.
#include <cassert>
#include <cstdio>
#include "objects.hpp"

int main() {
  std::string d =
      "*********\nNORMAL\n*********\n\n"
      "Angle Scale Daylight Shadow Filename   Description\n"
      "--------------------------------------------------\n"
      "30     1.0      1      1     gfx\\a.obj   (000) Tree\n"
      "30     1.0      1      1     <automatic>           Track\n"
      "30     3.0      1      1     disabled              Ad\n"
      "30     1.0      0      0     gfx\\b.obj             Fire\n"
      "30     1.0      1      1                               --\n"
      "\n*********\nSNOW\n*********\n"
      "30     1.0      1      1     gfx\\a_sn.obj (000) Tree\n";
  auto bs = objects::Parse(d);
  assert(bs.size() == 2 && bs[0].name == "NORMAL" && bs[1].name == "SNOW");
  assert(bs[0].rows.size() == 5 && bs[1].rows.size() == 1);
  const auto &r0 = bs[0].rows[0];
  assert(r0.angle == 30 && r0.scale == 1.0 && r0.daylight == 1);
  assert(r0.file == "gfx\\a.obj" && r0.desc == "(000) Tree");
  assert(bs[0].rows[1].file == "<automatic>");
  assert(bs[0].rows[2].file == "disabled");
  assert(bs[0].rows[3].shadow == 0);
  assert(bs[0].rows[4].file.empty() && bs[0].rows[4].desc == "--");
  assert(bs[1].rows[0].file == "gfx\\a_sn.obj");
  printf("objects tests pass\n");
  return 0;
}
