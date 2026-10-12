// Smallest assert: synthetic round-trip + checksum/charset rejection +
// one hand-computed known vector. (Real-file check: tools/botinfo.py
// decodes extracted/data/botinfo.dat 505/505 checksum-OK.)
#include <cassert>
#include <cstdio>
#include <string>
#include "botinfo.hpp"

// Inverse of FUN_0059b070 (game has no encoder; test-only helper).
static std::string Encode(const std::string &plain, uint8_t dh) {
  std::string s;
  s += (char)('a' + (dh & 0xF));
  s += (char)('i' + (dh >> 4));
  uint8_t chk = dh ^ 0x11;
  for (uint8_t b : plain) {
    uint8_t r = b ^ dh;
    s += (char)('h' + (r & 0xF));
    s += (char)('c' + (r >> 4));
    chk ^= b;
  }
  uint8_t r = chk ^ dh;
  s += (char)('h' + (r & 0xF));
  s += (char)('c' + (r >> 4));
  return s;
}

int main() {
  // Known vector: payload "A" (0x41), seed dh=0 -> "ai" + "ig" + "hh".
  std::string out;
  assert(botinfo::DecodeLine("aiighh", out) && out == "A");

  // Round-trip incl. spaces, colon, UTF-8 bytes, empty payload.
  for (uint8_t seed : {0x00, 0x2A, 0xFF}) {
    for (const std::string &p :
         {std::string("NAME: Pentti Juola"), std::string(""),
          std::string("Per\xc3\xa4j\xc3\xa4rvi CUP")}) {
      assert(botinfo::DecodeLine(Encode(p, seed), out) && out == p);
    }
  }

  // Corruption (any 1-char tweak) fails; odd length / bad nibble fail.
  std::string good = Encode("FI: kala", 0x11);
  std::string bad = good;
  bad[3] = bad[3] == 'h' ? 'i' : 'h';
  assert(!botinfo::DecodeLine(bad, out));
  assert(!botinfo::DecodeLine(good.substr(0, good.size() - 1), out));
  assert(!botinfo::DecodeLine("ai", out));
  assert(!botinfo::DecodeLine("!!ighh", out));

  printf("botinfo tests pass\n");
  return 0;
}
