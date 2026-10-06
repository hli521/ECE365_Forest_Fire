// Host check: c++ -std=c++11 -Iinclude test/test_sensor_packet/main.cpp -o /tmp/ffs-packet-test && /tmp/ffs-packet-test
#include <assert.h>
#include "sensor_packet.h"

int main() {
  using namespace sensor_packet;
  const size_t expected[] = {12, 140, 18, 146, 16, 144, 22, 150,
                             16, 144, 22, 150, 20, 148, 26, 154};
  uint8_t packet[MAX_SIZE + 1] = {'F', 'F', 'S', '4'};
  for (uint8_t present = 0; present <= ALL; ++present) {
    packet[8] = present;
    assert(packetSize(present) == expected[present]);
    for (uint8_t valid = 0; valid <= ALL; ++valid) {
      packet[9] = valid;
      assert(validPacket(packet, expected[present]) == ((valid & ~present) == 0));
    }
    packet[9] = present;
    for (size_t length = 0; length <= MAX_SIZE + 1; ++length) {
      assert(validPacket(packet, length) == (length == expected[present]));
    }
  }
  packet[8] = 16; packet[9] = 0;
  assert(!validPacket(packet, 12));
  packet[8] = ALL; packet[9] = 128;
  assert(!validPacket(packet, MAX_SIZE));
  packet[9] = ALL; packet[3] = '3';
  assert(!validPacket(packet, MAX_SIZE));
}
