// Smallest assert: bitmap set/clear per id, SDL key mapping.
#include <cassert>
#include <cstdio>
#include "input.hpp"

int main() {
  input::Keys k;
  input::Set(k, 0, true);
  input::Set(k, 6, true);
  assert(k.bits == 0x41 && input::Down(k, 0) && !input::Down(k, 1));
  input::Set(k, 0, false);
  assert(k.bits == 0x40);
  assert(input::SdlToId(SDLK_UP) == 0);
  assert(input::SdlToId(SDLK_DOWN) == 1);
  assert(input::SdlToId(SDLK_LEFT) == 2);
  assert(input::SdlToId(SDLK_RIGHT) == 3);
  assert(input::SdlToId(SDLK_PAGEUP) == 4);
  assert(input::SdlToId(SDLK_PAGEDOWN) == 5);
  assert(input::SdlToId(SDLK_RETURN) == 6);
  assert(input::SdlToId(SDLK_ESCAPE) == 7);
  assert(input::SdlToId(SDLK_A) == -1);
  printf("input tests pass\n");
  return 0;
}
