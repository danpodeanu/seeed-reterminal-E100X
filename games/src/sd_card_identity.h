#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sd_card_identity {

struct Identity {
  uint32_t sectorCount = 0;
  uint32_t fingerprint = 0;

  constexpr bool valid() const { return sectorCount != 0; }
};

constexpr bool same(const Identity& left, const Identity& right) {
  return left.valid() && right.valid() &&
         left.sectorCount == right.sectorCount &&
         left.fingerprint == right.fingerprint;
}

constexpr uint32_t readLe32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

inline uint32_t partitionStartSector(const uint8_t* sector,
                                     size_t sectorBytes) {
  if (sector == nullptr || sectorBytes < 512 || sector[510] != 0x55 ||
      sector[511] != 0xAA) {
    return 0;
  }
  for (size_t entry = 0; entry < 4; ++entry) {
    const size_t offset = 446 + entry * 16;
    const uint8_t type = sector[offset + 4];
    const uint32_t start = readLe32(sector + offset + 8);
    const uint32_t count = readLe32(sector + offset + 12);
    if (type != 0 && start != 0 && count != 0) return start;
  }
  return 0;
}

inline uint32_t updateFingerprint(uint32_t hash, const uint8_t* bytes,
                                  size_t length) {
  for (size_t index = 0; index < length; ++index) {
    hash ^= bytes[index];
    hash *= 16777619UL;
  }
  return hash;
}

inline Identity identify(uint32_t sectorCount, const uint8_t* sectorZero,
                         size_t sectorBytes, const uint8_t* volumeBootSector) {
  if (sectorCount == 0 || sectorZero == nullptr || sectorBytes == 0) return {};
  uint32_t hash = 2166136261UL;
  const uint8_t countBytes[] = {
      static_cast<uint8_t>(sectorCount),
      static_cast<uint8_t>(sectorCount >> 8),
      static_cast<uint8_t>(sectorCount >> 16),
      static_cast<uint8_t>(sectorCount >> 24),
  };
  hash = updateFingerprint(hash, countBytes, sizeof(countBytes));
  hash = updateFingerprint(hash, sectorZero, sectorBytes);
  if (volumeBootSector != nullptr && volumeBootSector != sectorZero) {
    hash = updateFingerprint(hash, volumeBootSector, sectorBytes);
  }
  return {sectorCount, hash};
}

}  // namespace sd_card_identity
