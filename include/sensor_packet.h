#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace sensor_packet {
constexpr uint8_t GRID = 1;
constexpr uint8_t AIR = 2;
constexpr uint8_t DHT = 4;
constexpr uint8_t MLX = 8;
constexpr uint8_t ALL = GRID | AIR | DHT | MLX;
// FFS4: magic[4], sequence[4], presence[1], validity[1], optional
// Grid-EYE[128], PM[6], DHT[4], MLX object/ambient[4], fire level[1], fire reasons[1].
// Multi-byte fields are little endian. Presence stays fixed until reset;
// validity is a subset of presence and changes with measurement success.
constexpr size_t packetSize(uint8_t present) {
  return 12 + ((present & GRID) ? 128 : 0) +
         ((present & AIR) ? 6 : 0) + ((present & DHT) ? 4 : 0) +
         ((present & MLX) ? 4 : 0);
}
constexpr size_t MAX_SIZE = packetSize(ALL);

inline bool validPacket(const uint8_t *data, size_t length) {
  return length >= 12 && memcmp(data, "FFS4", 4) == 0 &&
         (data[8] & ~ALL) == 0 && (data[9] & ~data[8]) == 0 &&
         length == packetSize(data[8]);
}
}  // namespace sensor_packet
