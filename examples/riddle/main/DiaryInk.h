// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <M5GFX.h>

#include <cstddef>
#include <cstdint>

#include "DiaryTypes.h"
#include "RiddlePng.h"

namespace riddle {

class DiaryInk {
public:
    DiaryInk() = default;
    ~DiaryInk() = default;

    DiaryInk(const DiaryInk &) = delete;
    DiaryInk &operator=(const DiaryInk &) = delete;

    bool begin(int32_t width, int32_t height);
    void clear();

    bool isEmpty() const;
    const BBox &bbox() const;
    const char *lastError() const;

    Rect penPoint(lgfx::LovyanGFX &surface, int32_t x, int32_t y,
                  int32_t radius);
    void penUp();
    Rect erasePoint(lgfx::LovyanGFX &surface, int32_t x, int32_t y,
                    int32_t radius);

    bool snapshot(PageBitmap &bitmap, const char **error = nullptr) const;

private:
    bool setPagePixel(int32_t x, int32_t y, bool black);
    void stamp(lgfx::LovyanGFX &surface, int32_t x, int32_t y,
               int32_t radius, bool black);
    void drawSegment(lgfx::LovyanGFX &surface,
                     int32_t x0, int32_t y0, int32_t radius0,
                     int32_t x1, int32_t y1, int32_t radius1,
                     bool black);
    void recomputeBBox() const;
    void setError(const char *message, const char **error = nullptr) const;

    PageBitmap page_;
    size_t black_pixel_count_ = 0;
    int32_t surface_width_ = 0;
    int32_t surface_height_ = 0;
    int16_t last_pen_x_ = 0;
    int16_t last_pen_y_ = 0;
    uint8_t last_pen_radius_ = 0;
    int16_t last_erase_x_ = 0;
    int16_t last_erase_y_ = 0;
    uint8_t last_erase_radius_ = 0;
    bool pen_active_ = false;
    bool erase_active_ = false;
    mutable bool bbox_dirty_ = false;
    mutable BBox bbox_;
    mutable const char *last_error_ = nullptr;
};

void dissolvePass(lgfx::LovyanGFX &surface, const Rect &region,
                  uint32_t stage, uint32_t stages);
void dissolvePass(lgfx::LovyanGFX &surface, const BBox &region,
                  uint32_t stage, uint32_t stages);

} // namespace riddle
