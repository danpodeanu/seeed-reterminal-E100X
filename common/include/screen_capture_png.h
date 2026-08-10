#pragma once

#include <Arduino.h>

#include <stdint.h>
#include <stdlib.h>

#include "screen_capture_png_layout.h"
#include "screenshot_rotation.h"

namespace screen_capture_png {

inline void paletteColor(uint8_t index, uint8_t colorDepth, uint8_t& red,
                         uint8_t& green, uint8_t& blue) {
  (void)colorDepth;
#if RETERMINAL_MODEL == 1001
  const uint8_t gray = index <= 3 ? static_cast<uint8_t>(index * 85) : 255;
  red = green = blue = gray;
#elif RETERMINAL_MODEL == 1003
  const uint8_t gray = index <= 15 ? static_cast<uint8_t>(index * 17) : 255;
  red = green = blue = gray;
#elif RETERMINAL_MODEL == 1005
  const uint8_t gray = e1005PaletteGray(index, colorDepth);
  red = green = blue = gray;
#else
  red = green = blue = 255;
  switch (index) {
    case 0x0:
      red = 255;
      green = 255;
      blue = 255;
      break;
    case 0x2:
      red = 29;
      green = 185;
      blue = 84;
      break;
    case 0x6:
      red = 229;
      green = 57;
      blue = 53;
      break;
    case 0xB:
      red = 255;
      green = 216;
      blue = 0;
      break;
    case 0xD:
      red = 0;
      green = 76;
      blue = 255;
      break;
    case 0xF:
      red = 0;
      green = 0;
      blue = 0;
      break;
  }
#endif
}

template <typename Sink>
inline bool writeBytes(Sink& sink, const uint8_t* bytes, size_t length) {
  constexpr size_t kChunkSize = 512;
  size_t offset = 0;
  while (offset < length) {
    const size_t chunk =
        min(kChunkSize, static_cast<size_t>(length - offset));
    const size_t written = sink.write(bytes + offset, chunk);
    if (written == 0 || written > chunk) return false;
    offset += written;
  }
  return true;
}

template <typename Sink>
inline bool writeBigEndian32(Sink& sink, uint32_t value) {
  const uint8_t bytes[] = {
      static_cast<uint8_t>(value >> 24),
      static_cast<uint8_t>(value >> 16),
      static_cast<uint8_t>(value >> 8),
      static_cast<uint8_t>(value),
  };
  return writeBytes(sink, bytes, sizeof(bytes));
}

template <typename Sink>
class ChunkWriter {
 public:
  explicit ChunkWriter(Sink& sink) : sink_(sink) {}

  size_t write(const uint8_t* bytes, size_t length) {
    const size_t written = sink_.write(bytes, length);
    if (written <= length) crc_ = updateCrc32(crc_, bytes, written);
    return written;
  }

  uint32_t checksum() const { return crc_ ^ 0xFFFFFFFFUL; }

 private:
  Sink& sink_;
  uint32_t crc_ = 0xFFFFFFFFUL;
};

template <typename Sink>
inline bool writeChunk(Sink& sink, const uint8_t type[4], const uint8_t* data,
                       uint32_t length) {
  if (!writeBigEndian32(sink, length)) return false;
  ChunkWriter<Sink> chunk(sink);
  if (!writeBytes(chunk, type, 4) ||
      (length != 0 && !writeBytes(chunk, data, length))) {
    return false;
  }
  return writeBigEndian32(sink, chunk.checksum());
}

template <typename EPaper, typename Sink>
inline bool write(EPaper& epaper, uint32_t width, uint32_t height, Sink& sink) {
  if (width == 0 || height == 0 || width >= 65535U) return false;

  const Layout png = layout(width, height);
  const uint8_t colorDepth = static_cast<uint8_t>(epaper.getColorDepth());
  uint8_t* row = static_cast<uint8_t*>(malloc(width));
  if (row == nullptr) return false;

  const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
  const uint8_t ihdrType[] = {'I', 'H', 'D', 'R'};
  const uint8_t plteType[] = {'P', 'L', 'T', 'E'};
  const uint8_t idatType[] = {'I', 'D', 'A', 'T'};
  const uint8_t iendType[] = {'I', 'E', 'N', 'D'};
  const uint8_t ihdr[] = {
      static_cast<uint8_t>(width >> 24),
      static_cast<uint8_t>(width >> 16),
      static_cast<uint8_t>(width >> 8),
      static_cast<uint8_t>(width),
      static_cast<uint8_t>(height >> 24),
      static_cast<uint8_t>(height >> 16),
      static_cast<uint8_t>(height >> 8),
      static_cast<uint8_t>(height),
      8, 3, 0, 0, 0,
  };

  bool ok = writeBytes(sink, signature, sizeof(signature)) &&
            writeChunk(sink, ihdrType, ihdr, sizeof(ihdr)) &&
            writeBigEndian32(sink, kPaletteBytes);

  ChunkWriter<Sink> paletteChunk(sink);
  ok = ok && writeBytes(paletteChunk, plteType, sizeof(plteType));
  for (uint16_t index = 0; ok && index < kPaletteEntries; ++index) {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    paletteColor(static_cast<uint8_t>(index), colorDepth, red, green, blue);
    const uint8_t entry[] = {red, green, blue};
    ok = writeBytes(paletteChunk, entry, sizeof(entry));
  }
  ok = ok && writeBigEndian32(sink, paletteChunk.checksum()) &&
       writeBigEndian32(sink, png.idatDataBytes);

  ChunkWriter<Sink> idatChunk(sink);
  const uint8_t zlibHeader[] = {0x78, 0x01};
  ok = ok && writeBytes(idatChunk, idatType, sizeof(idatType)) &&
       writeBytes(idatChunk, zlibHeader, sizeof(zlibHeader));
  uint32_t adler = 1;
  const uint8_t filter = 0;
#if RETERMINAL_MODEL == 1005
  const uint8_t captureRotation = epaper.getRotation();
  epaper.setRotation(0);
#endif
  for (uint32_t y = 0; ok && y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
#if RETERMINAL_MODEL == 1005
      const screenshot::PixelCoordinate native =
          screenshot::nativePixelCoordinate(
              captureRotation, static_cast<int32_t>(width),
              static_cast<int32_t>(height), static_cast<int32_t>(x),
              static_cast<int32_t>(y));
      row[x] = static_cast<uint8_t>(
          epaper.readPixelValue(native.x, native.y));
#else
      row[x] = static_cast<uint8_t>(epaper.readPixelValue(x, y));
#endif
    }

    const uint16_t blockLength = static_cast<uint16_t>(png.rowBytes);
    const uint16_t inverseLength = static_cast<uint16_t>(~blockLength);
    const uint8_t blockHeader[] = {
        static_cast<uint8_t>(y + 1U == height ? 1 : 0),
        static_cast<uint8_t>(blockLength),
        static_cast<uint8_t>(blockLength >> 8),
        static_cast<uint8_t>(inverseLength),
        static_cast<uint8_t>(inverseLength >> 8),
    };
    ok = writeBytes(idatChunk, blockHeader, sizeof(blockHeader)) &&
         writeBytes(idatChunk, &filter, 1) &&
         writeBytes(idatChunk, row, width);
    adler = updateAdler32(adler, &filter, 1);
    adler = updateAdler32(adler, row, width);
    if ((y & 31U) == 0) delay(1);
  }
#if RETERMINAL_MODEL == 1005
  epaper.setRotation(captureRotation);
#endif

  ok = ok && writeBigEndian32(idatChunk, adler) &&
       writeBigEndian32(sink, idatChunk.checksum()) &&
       writeChunk(sink, iendType, nullptr, 0);
  free(row);
  return ok;
}

}  // namespace screen_capture_png
