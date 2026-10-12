// Smallest assert: checksum round-trips, corruption fails, odd tail folds.
#include <cassert>
#include <cstdio>
#include <vector>
#include "net.hpp"

int main() {
  // Even-length packet: id u16 + len u16 + checksum u16 + payload.
  std::vector<uint8_t> pkt = {0x07, 0x00, 0x0A, 0x00, 0x00, 0x00,
                              0x41, 0x42, 0x43, 0x44};
  uint16_t c = net::Fold(pkt.data(), pkt.size());
  pkt[4] = c & 0xFF;
  pkt[5] = c >> 8;
  assert(net::Verify(pkt));
  assert(c == (0x0007 ^ 0x000A ^ 0x4241 ^ 0x4443));

  pkt[7]++;
  assert(!net::Verify(pkt));

  // Odd trailing byte folds into low byte.
  std::vector<uint8_t> odd = {0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0xFF};
  uint16_t co = net::Fold(odd.data(), odd.size());
  assert(co == (0x0001 ^ 0x0002 ^ 0x00FF));
  odd[4] = co & 0xFF;
  odd[5] = co >> 8;
  assert(net::Verify(odd));

  assert(!net::Verify(nullptr, 0));
  printf("net tests pass\n");
  return 0;
}
