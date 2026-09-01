// SPDX-License-Identifier: GPL-2.0-only

#include "DiaryInk.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace {

constexpr int32_t kDirtyMargin = 2;
constexpr int32_t kSnapshotPadding = 20;
constexpr uint32_t kSnapshotLongSide = 800;

int32_t clampCoordinate(int32_t value, int32_t extent)
{
    if (extent <= 0) {
        return 0;
    }
    return std::max<int32_t>(0, std::min<int32_t>(value, extent - 1));
}

int32_t clampRadius(int32_t radius)
{
    return std::max<int32_t>(0, std::min<int32_t>(radius, 255));
}

riddle::Rect brushBounds(int32_t x0, int32_t y0, int32_t radius0,
                         int32_t x1, int32_t y1, int32_t radius1,
                         int32_t width, int32_t height)
{
    const int64_t left = std::min<int64_t>(
        static_cast<int64_t>(x0) - radius0,
        static_cast<int64_t>(x1) - radius1) - kDirtyMargin;
    const int64_t top = std::min<int64_t>(
        static_cast<int64_t>(y0) - radius0,
        static_cast<int64_t>(y1) - radius1) - kDirtyMargin;
    const int64_t right = std::max<int64_t>(
        static_cast<int64_t>(x0) + radius0,
        static_cast<int64_t>(x1) + radius1) + kDirtyMargin + 1;
    const int64_t bottom = std::max<int64_t>(
        static_cast<int64_t>(y0) + radius0,
        static_cast<int64_t>(y1) + radius1) + kDirtyMargin + 1;
    if (right <= left || bottom <= top) {
        return {};
    }
    const int64_t max_value = std::numeric_limits<int32_t>::max();
    const riddle::Rect raw = {
        static_cast<int32_t>(std::max<int64_t>(
            std::numeric_limits<int32_t>::min(), left)),
        static_cast<int32_t>(std::max<int64_t>(
            std::numeric_limits<int32_t>::min(), top)),
        static_cast<int32_t>(std::min<int64_t>(max_value, right - left)),
        static_cast<int32_t>(std::min<int64_t>(max_value, bottom - top)),
    };
    return raw.clipped(width, height);
}

uint32_t pixelHash(int32_t x, int32_t y)
{
    uint32_t hash = static_cast<uint32_t>(x) * 0x9E3779B1u ^
                    static_cast<uint32_t>(y) * 0x85EBCA6Bu;
    hash ^= hash >> 13;
    hash *= 0xC2B2AE35u;
    return hash ^ (hash >> 16);
}

} // namespace

namespace riddle {

bool DiaryInk::begin(int32_t width, int32_t height)
{
    if (width <= 0 || height <= 0 || width > UINT16_MAX ||
        height > UINT16_MAX) {
        setError("ink surface dimensions are invalid");
        return false;
    }
    if (!page_.begin(static_cast<uint16_t>(width),
                     static_cast<uint16_t>(height))) {
        setError("PSRAM ink bitmap allocation failed");
        return false;
    }

    surface_width_ = width;
    surface_height_ = height;
    clear();
    return true;
}

void DiaryInk::clear()
{
    page_.clear(false);
    black_pixel_count_ = 0;
    last_pen_x_ = 0;
    last_pen_y_ = 0;
    last_pen_radius_ = 0;
    last_erase_x_ = 0;
    last_erase_y_ = 0;
    last_erase_radius_ = 0;
    pen_active_ = false;
    erase_active_ = false;
    bbox_dirty_ = false;
    bbox_.clear();
    last_error_ = nullptr;
}

bool DiaryInk::isEmpty() const
{
    return black_pixel_count_ == 0;
}

const BBox &DiaryInk::bbox() const
{
    if (bbox_dirty_) {
        recomputeBBox();
    }
    return bbox_;
}

const char *DiaryInk::lastError() const
{
    return last_error_;
}

Rect DiaryInk::penPoint(lgfx::LovyanGFX &surface, int32_t x, int32_t y,
                        int32_t radius)
{
    if (!page_.valid() || surface.width() != surface_width_ ||
        surface.height() != surface_height_) {
        setError("ink surface does not match the page bitmap");
        return {};
    }

    x = clampCoordinate(x, surface_width_);
    y = clampCoordinate(y, surface_height_);
    radius = clampRadius(radius);
    const bool can_join = pen_active_;
    const int32_t x0 = can_join ? last_pen_x_ : x;
    const int32_t y0 = can_join ? last_pen_y_ : y;
    const int32_t radius0 = can_join ? last_pen_radius_ : radius;
    const Rect dirty = brushBounds(x0, y0, radius0, x, y, radius,
                                   surface_width_, surface_height_);

    surface.startWrite();
    if (can_join) {
        drawSegment(surface, x0, y0, radius0, x, y, radius, true);
    } else {
        stamp(surface, x, y, radius, true);
    }
    surface.endWrite();

    bbox_.include(dirty);
    pen_active_ = true;
    erase_active_ = false;
    last_pen_x_ = static_cast<int16_t>(x);
    last_pen_y_ = static_cast<int16_t>(y);
    last_pen_radius_ = static_cast<uint8_t>(radius);
    setError(nullptr);
    return dirty;
}

void DiaryInk::penUp()
{
    pen_active_ = false;
    erase_active_ = false;
}

Rect DiaryInk::erasePoint(lgfx::LovyanGFX &surface, int32_t x, int32_t y,
                          int32_t radius)
{
    if (!page_.valid() || surface.width() != surface_width_ ||
        surface.height() != surface_height_) {
        setError("ink surface does not match the page bitmap");
        return {};
    }

    x = clampCoordinate(x, surface_width_);
    y = clampCoordinate(y, surface_height_);
    radius = clampRadius(radius);
    const bool can_join = erase_active_;
    const int32_t x0 = can_join ? last_erase_x_ : x;
    const int32_t y0 = can_join ? last_erase_y_ : y;
    const int32_t radius0 = can_join ? last_erase_radius_ : radius;
    const Rect dirty = brushBounds(x0, y0, radius0, x, y, radius,
                                   surface_width_, surface_height_);

    surface.startWrite();
    if (can_join) {
        drawSegment(surface, x0, y0, radius0, x, y, radius, false);
    } else {
        stamp(surface, x, y, radius, false);
    }
    surface.endWrite();

    pen_active_ = false;
    erase_active_ = true;
    last_erase_x_ = static_cast<int16_t>(x);
    last_erase_y_ = static_cast<int16_t>(y);
    last_erase_radius_ = static_cast<uint8_t>(radius);
    bbox_dirty_ = black_pixel_count_ != 0;
    if (black_pixel_count_ == 0) {
        bbox_.clear();
    }
    setError(nullptr);
    return dirty;
}

bool DiaryInk::snapshot(PageBitmap &bitmap, const char **error) const
{
    bitmap.reset();
    if (!page_.valid()) {
        setError("ink bitmap is not initialized", error);
        return false;
    }
    if (black_pixel_count_ == 0) {
        setError("page has no ink", error);
        return false;
    }

    const BBox &bounds = bbox();
    if (bounds.isEmpty()) {
        setError("ink bounds are invalid", error);
        return false;
    }
    const int32_t left = std::max<int32_t>(0, bounds.x0 - kSnapshotPadding);
    const int32_t top = std::max<int32_t>(0, bounds.y0 - kSnapshotPadding);
    const int32_t right = std::min<int32_t>(
        surface_width_ - 1, bounds.x1 + kSnapshotPadding);
    const int32_t bottom = std::min<int32_t>(
        surface_height_ - 1, bounds.y1 + kSnapshotPadding);
    if (left > right || top > bottom) {
        setError("ink bounds are invalid", error);
        return false;
    }

    const uint32_t crop_width = static_cast<uint32_t>(right - left + 1);
    const uint32_t crop_height = static_cast<uint32_t>(bottom - top + 1);
    const uint32_t long_side = std::max(crop_width, crop_height);
    const uint32_t scale_for_limit =
        (long_side + kSnapshotLongSide - 1U) / kSnapshotLongSide;
    const uint32_t scale = std::max<uint32_t>(2, scale_for_limit);
    const uint32_t output_width = (crop_width + scale - 1U) / scale;
    const uint32_t output_height = (crop_height + scale - 1U) / scale;
    if (output_width == 0 || output_height == 0 ||
        output_width > UINT16_MAX || output_height > UINT16_MAX) {
        setError("snapshot dimensions are unsupported", error);
        return false;
    }
    if (!bitmap.begin(static_cast<uint16_t>(output_width),
                      static_cast<uint16_t>(output_height))) {
        setError("PSRAM page bitmap allocation failed", error);
        return false;
    }

    // OR-reduction preserves thin strokes while reducing the image dimensions.
    for (int32_t y = top; y <= bottom; ++y) {
        for (int32_t x = left; x <= right; ++x) {
            if (page_.getPixel(x, y)) {
                bitmap.setPixel((x - left) / static_cast<int32_t>(scale),
                                (y - top) / static_cast<int32_t>(scale),
                                true);
            }
        }
    }

    setError(nullptr, error);
    return true;
}

bool DiaryInk::setPagePixel(int32_t x, int32_t y, bool black)
{
    const bool old_value = page_.getPixel(x, y);
    if (old_value == black || !page_.setPixel(x, y, black)) {
        return false;
    }
    if (black) {
        ++black_pixel_count_;
    } else {
        --black_pixel_count_;
    }
    return true;
}

void DiaryInk::stamp(lgfx::LovyanGFX &surface, int32_t cx, int32_t cy,
                     int32_t radius, bool black)
{
    const int32_t left = std::max<int32_t>(0, cx - radius);
    const int32_t top = std::max<int32_t>(0, cy - radius);
    const int32_t right = std::min<int32_t>(surface_width_ - 1, cx + radius);
    const int32_t bottom = std::min<int32_t>(surface_height_ - 1, cy + radius);
    const int64_t radius_squared = static_cast<int64_t>(radius) * radius;
    for (int32_t y = top; y <= bottom; ++y) {
        for (int32_t x = left; x <= right; ++x) {
            const int64_t dx = x - cx;
            const int64_t dy = y - cy;
            if (dx * dx + dy * dy <= radius_squared) {
                setPagePixel(x, y, black);
                surface.drawPixel(x, y, black ? TFT_BLACK : TFT_WHITE);
            }
        }
    }
}

void DiaryInk::drawSegment(lgfx::LovyanGFX &surface,
                           int32_t x0, int32_t y0, int32_t radius0,
                           int32_t x1, int32_t y1, int32_t radius1,
                           bool black)
{
    const int32_t dx = x1 - x0;
    const int32_t dy = y1 - y0;
    const int32_t steps = std::max(std::abs(dx), std::abs(dy));
    if (steps == 0) {
        stamp(surface, x1, y1, radius1, black);
        return;
    }
    for (int32_t step = 0; step <= steps; ++step) {
        const int32_t x = x0 + static_cast<int64_t>(dx) * step / steps;
        const int32_t y = y0 + static_cast<int64_t>(dy) * step / steps;
        const int32_t radius = radius0 +
            static_cast<int64_t>(radius1 - radius0) * step / steps;
        stamp(surface, x, y, radius, black);
    }
}

void DiaryInk::recomputeBBox() const
{
    bbox_.clear();
    if (black_pixel_count_ == 0 || !page_.valid()) {
        bbox_dirty_ = false;
        return;
    }
    for (int32_t y = 0; y < surface_height_; ++y) {
        for (int32_t x = 0; x < surface_width_; ++x) {
            if (page_.getPixel(x, y)) {
                bbox_.add(x, y);
            }
        }
    }
    bbox_dirty_ = false;
}

void DiaryInk::setError(const char *message, const char **error) const
{
    last_error_ = message;
    if (error != nullptr) {
        *error = message;
    }
}

void dissolvePass(lgfx::LovyanGFX &surface, const Rect &region,
                  uint32_t stage, uint32_t stages)
{
    if (stages == 0) {
        return;
    }
    const Rect clipped = region.clipped(surface.width(), surface.height());
    if (clipped.isEmpty()) {
        return;
    }

    surface.startWrite();
    if (stage >= stages - 1) {
        surface.fillRect(clipped.x, clipped.y, clipped.w, clipped.h,
                         TFT_WHITE);
        surface.endWrite();
        return;
    }
    const int32_t right = clipped.x + clipped.w;
    const int32_t bottom = clipped.y + clipped.h;
    for (int32_t y = clipped.y; y < bottom; ++y) {
        for (int32_t block = clipped.x; block < right;
             block += static_cast<int32_t>(stages)) {
            const uint32_t span = static_cast<uint32_t>(
                std::min<int32_t>(right - block, stages));
            if (stage >= span) {
                continue;
            }
            const uint32_t offset = pixelHash(block, y) % span;
            const int32_t x = block +
                static_cast<int32_t>((stage + offset) % span);
            surface.drawPixel(x, y, TFT_WHITE);
        }
    }
    surface.endWrite();
}

void dissolvePass(lgfx::LovyanGFX &surface, const BBox &region,
                  uint32_t stage, uint32_t stages)
{
    if (!region.isEmpty()) {
        dissolvePass(surface, region.toRect(), stage, stages);
    }
}

} // namespace riddle
