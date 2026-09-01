// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace riddle {

// MSB-first one-bit pixels. White is 0 and black is 1.
struct PageBitmap {
    uint8_t *data = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t stride = 0;
    bool owns = false;

    PageBitmap() = default;
    ~PageBitmap();
    PageBitmap(const PageBitmap &) = delete;
    PageBitmap &operator=(const PageBitmap &) = delete;
    PageBitmap(PageBitmap &&other) noexcept;
    PageBitmap &operator=(PageBitmap &&other) noexcept;

    bool begin(uint16_t new_width, uint16_t new_height);
    void reset();
    void clear(bool black = false);
    bool setPixel(int x, int y, bool black);
    bool getPixel(int x, int y) const;
    bool valid() const;
    size_t sizeBytes() const;
};

// A movable PSRAM byte allocation used for encoded PNG data.
struct ByteBuffer {
    uint8_t *data = nullptr;
    size_t size = 0;
    size_t capacity = 0;
    bool owns = false;

    ByteBuffer() = default;
    ~ByteBuffer();
    ByteBuffer(const ByteBuffer &) = delete;
    ByteBuffer &operator=(const ByteBuffer &) = delete;
    ByteBuffer(ByteBuffer &&other) noexcept;
    ByteBuffer &operator=(ByteBuffer &&other) noexcept;

    bool reserve(size_t new_capacity);
    bool append(const void *source, size_t length);
    void reset();
    bool valid() const;
};

// Produces a standards-compliant grayscale PNG with stored DEFLATE blocks.
bool encodePagePng(const PageBitmap &bitmap, ByteBuffer &png,
                   char *error = nullptr, size_t error_size = 0);

} // namespace riddle
