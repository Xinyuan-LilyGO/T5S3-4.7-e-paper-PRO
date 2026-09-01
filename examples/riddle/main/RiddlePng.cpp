// SPDX-License-Identifier: GPL-2.0-only

#include "RiddlePng.h"

#include <esp_heap_caps.h>

#include <cstring>
#include <limits>
#include <utility>

namespace riddle {
namespace {

void setError(char *error, size_t error_size, const char *message)
{
    if (error == nullptr || error_size == 0) {
        return;
    }
    std::strncpy(error, message, error_size - 1);
    error[error_size - 1] = '\0';
}

void appendU32(ByteBuffer &buffer, uint32_t value)
{
    const uint8_t bytes[] = {
        static_cast<uint8_t>(value >> 24),
        static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value),
    };
    buffer.append(bytes, sizeof(bytes));
}

uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t length)
{
    while (length-- != 0) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
        }
    }
    return crc;
}

bool appendChunk(ByteBuffer &png, const char type[4], const uint8_t *data,
                 size_t length)
{
    if (length > UINT32_MAX) {
        return false;
    }
    appendU32(png, static_cast<uint32_t>(length));
    if (!png.append(type, 4) || (length != 0 && !png.append(data, length))) {
        return false;
    }
    uint32_t crc = crc32Update(0xFFFFFFFFUL,
                               reinterpret_cast<const uint8_t *>(type), 4);
    crc = crc32Update(crc, data, length) ^ 0xFFFFFFFFUL;
    appendU32(png, crc);
    return true;
}

uint8_t rawByteAt(const PageBitmap &bitmap, size_t raw_offset)
{
    const size_t row_size = static_cast<size_t>(bitmap.stride) + 1U;
    const size_t column = raw_offset % row_size;
    if (column == 0) {
        return 0; // PNG filter type None.
    }
    const size_t row = raw_offset / row_size;
    // PNG grayscale uses 0 for black and 1 for white, opposite to PageBitmap.
    uint8_t value = static_cast<uint8_t>(
        ~bitmap.data[row * bitmap.stride + column - 1U]);
    const uint8_t remainder = bitmap.width & 7U;
    if (remainder != 0 && column == bitmap.stride) {
        value &= static_cast<uint8_t>(0xFFU << (8U - remainder));
    }
    return value;
}

} // namespace

PageBitmap::~PageBitmap()
{
    reset();
}

PageBitmap::PageBitmap(PageBitmap &&other) noexcept
{
    *this = std::move(other);
}

PageBitmap &PageBitmap::operator=(PageBitmap &&other) noexcept
{
    if (this == &other) {
        return *this;
    }
    reset();
    data = other.data;
    width = other.width;
    height = other.height;
    stride = other.stride;
    owns = other.owns;
    other.data = nullptr;
    other.width = 0;
    other.height = 0;
    other.stride = 0;
    other.owns = false;
    return *this;
}

bool PageBitmap::begin(uint16_t new_width, uint16_t new_height)
{
    reset();
    if (new_width == 0 || new_height == 0) {
        return false;
    }
    const size_t new_stride = (static_cast<size_t>(new_width) + 7U) / 8U;
    if (new_stride > UINT16_MAX || new_height > SIZE_MAX / new_stride) {
        return false;
    }
    const size_t bytes = new_stride * new_height;
    data = static_cast<uint8_t *>(heap_caps_malloc(
        bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data == nullptr) {
        return false;
    }
    width = new_width;
    height = new_height;
    stride = static_cast<uint16_t>(new_stride);
    owns = true;
    std::memset(data, 0, bytes);
    return true;
}

void PageBitmap::reset()
{
    if (data != nullptr && owns) {
        heap_caps_free(data);
    }
    data = nullptr;
    width = 0;
    height = 0;
    stride = 0;
    owns = false;
}

void PageBitmap::clear(bool black)
{
    if (valid()) {
        std::memset(data, black ? 0xFF : 0x00, sizeBytes());
    }
}

bool PageBitmap::setPixel(int x, int y, bool black)
{
    if (!valid() || x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    uint8_t &value = data[static_cast<size_t>(y) * stride +
                          static_cast<size_t>(x) / 8U];
    const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7));
    if (black) {
        value |= mask;
    } else {
        value &= static_cast<uint8_t>(~mask);
    }
    return true;
}

bool PageBitmap::getPixel(int x, int y) const
{
    if (!valid() || x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    const uint8_t value = data[static_cast<size_t>(y) * stride +
                               static_cast<size_t>(x) / 8U];
    return (value & static_cast<uint8_t>(0x80U >> (x & 7))) != 0;
}

bool PageBitmap::valid() const
{
    return data != nullptr && width != 0 && height != 0 &&
           stride >= (static_cast<size_t>(width) + 7U) / 8U;
}

size_t PageBitmap::sizeBytes() const
{
    return static_cast<size_t>(stride) * height;
}

ByteBuffer::~ByteBuffer()
{
    reset();
}

ByteBuffer::ByteBuffer(ByteBuffer &&other) noexcept
{
    *this = std::move(other);
}

ByteBuffer &ByteBuffer::operator=(ByteBuffer &&other) noexcept
{
    if (this == &other) {
        return *this;
    }
    reset();
    data = other.data;
    size = other.size;
    capacity = other.capacity;
    owns = other.owns;
    other.data = nullptr;
    other.size = 0;
    other.capacity = 0;
    other.owns = false;
    return *this;
}

bool ByteBuffer::reserve(size_t new_capacity)
{
    if (new_capacity <= capacity) {
        return true;
    }
    uint8_t *replacement = static_cast<uint8_t *>(heap_caps_malloc(
        new_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (replacement == nullptr) {
        return false;
    }
    if (data != nullptr && size != 0) {
        std::memcpy(replacement, data, size);
    }
    if (data != nullptr && owns) {
        heap_caps_free(data);
    }
    data = replacement;
    capacity = new_capacity;
    owns = true;
    return true;
}

bool ByteBuffer::append(const void *source, size_t length)
{
    if (length == 0) {
        return true;
    }
    if (source == nullptr || length > SIZE_MAX - size) {
        return false;
    }
    const size_t required = size + length;
    if (required > capacity) {
        size_t grown = capacity == 0 ? 256U : capacity;
        while (grown < required) {
            if (grown > SIZE_MAX / 2U) {
                grown = required;
                break;
            }
            grown *= 2U;
        }
        if (!reserve(grown)) {
            return false;
        }
    }
    std::memcpy(data + size, source, length);
    size = required;
    return true;
}

void ByteBuffer::reset()
{
    if (data != nullptr && owns) {
        heap_caps_free(data);
    }
    data = nullptr;
    size = 0;
    capacity = 0;
    owns = false;
}

bool ByteBuffer::valid() const
{
    return data != nullptr && size != 0 && size <= capacity;
}

bool encodePagePng(const PageBitmap &bitmap, ByteBuffer &png, char *error,
                   size_t error_size)
{
    png.reset();
    if (!bitmap.valid()) {
        setError(error, error_size, "invalid page bitmap");
        return false;
    }

    const size_t row_size = static_cast<size_t>(bitmap.stride) + 1U;
    if (bitmap.height > SIZE_MAX / row_size) {
        setError(error, error_size, "page bitmap is too large");
        return false;
    }
    const size_t raw_size = row_size * bitmap.height;
    const size_t block_count = (raw_size + 65534U) / 65535U;
    if (block_count > (SIZE_MAX - raw_size - 6U) / 5U) {
        setError(error, error_size, "PNG size overflow");
        return false;
    }
    const size_t idat_size = 2U + raw_size + block_count * 5U + 4U;
    if (idat_size > UINT32_MAX || idat_size > SIZE_MAX - 57U ||
        !png.reserve(idat_size + 57U)) {
        setError(error, error_size, "not enough PSRAM for PNG");
        return false;
    }

    static const uint8_t signature[] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
    };
    png.append(signature, sizeof(signature));

    uint8_t ihdr[13] = {};
    ihdr[2] = static_cast<uint8_t>(bitmap.width >> 8);
    ihdr[3] = static_cast<uint8_t>(bitmap.width);
    ihdr[6] = static_cast<uint8_t>(bitmap.height >> 8);
    ihdr[7] = static_cast<uint8_t>(bitmap.height);
    ihdr[8] = 1; // bit depth
    ihdr[9] = 0; // grayscale
    if (!appendChunk(png, "IHDR", ihdr, sizeof(ihdr))) {
        setError(error, error_size, "failed to write PNG header");
        png.reset();
        return false;
    }

    const size_t idat_start = png.size;
    appendU32(png, static_cast<uint32_t>(idat_size));
    if (!png.append("IDAT", 4)) {
        setError(error, error_size, "failed to write PNG data");
        png.reset();
        return false;
    }
    const size_t compressed_start = png.size;
    const uint8_t zlib_header[] = {0x78, 0x01};
    png.append(zlib_header, sizeof(zlib_header));

    uint32_t adler_a = 1;
    uint32_t adler_b = 0;
    size_t raw_offset = 0;
    while (raw_offset < raw_size) {
        const size_t remaining = raw_size - raw_offset;
        const uint16_t length = static_cast<uint16_t>(
            remaining > 65535U ? 65535U : remaining);
        const uint8_t block_header = remaining <= 65535U ? 0x01 : 0x00;
        const uint8_t length_bytes[] = {
            block_header,
            static_cast<uint8_t>(length),
            static_cast<uint8_t>(length >> 8),
            static_cast<uint8_t>(~length),
            static_cast<uint8_t>((~length) >> 8),
        };
        png.append(length_bytes, sizeof(length_bytes));
        for (uint32_t i = 0; i < length; ++i) {
            const uint8_t value = rawByteAt(bitmap, raw_offset++);
            png.append(&value, 1);
            adler_a += value;
            if (adler_a >= 65521U) {
                adler_a -= 65521U;
            }
            adler_b += adler_a;
            if (adler_b >= 65521U) {
                adler_b %= 65521U;
            }
        }
    }
    const uint32_t adler = (adler_b << 16) | adler_a;
    appendU32(png, adler);

    const uint8_t *type_and_data = png.data + idat_start + 4U;
    const size_t type_and_data_size = 4U + png.size - compressed_start;
    const uint32_t idat_crc =
        crc32Update(0xFFFFFFFFUL, type_and_data, type_and_data_size) ^
        0xFFFFFFFFUL;
    appendU32(png, idat_crc);
    if (!appendChunk(png, "IEND", nullptr, 0)) {
        setError(error, error_size, "failed to finish PNG");
        png.reset();
        return false;
    }
    return true;
}

} // namespace riddle
