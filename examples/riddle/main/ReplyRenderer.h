// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <Arduino.h>
#include <M5GFX.h>

#include "DiaryTypes.h"

namespace riddle {

class ReplyRenderer {
public:
    bool begin(lgfx::LovyanGFX &surface, const char *text);
    Rect drawNext(lgfx::LovyanGFX &surface, uint8_t glyph_budget = 2);
    bool done() const;
    Rect region() const;
    size_t textBytes() const;
    void clear();

private:
    static constexpr size_t kMaxLines = 28;
    static constexpr int kMarginX = 36;
    static constexpr int kMarginY = 44;

    String lines_[kMaxLines];
    int16_t line_x_[kMaxLines] = {};
    int16_t line_y_[kMaxLines] = {};
    size_t line_count_ = 0;
    size_t line_index_ = 0;
    size_t byte_index_ = 0;
    size_t text_bytes_ = 0;
    int16_t line_height_ = 0;
    Rect region_ = {};
};

} // namespace riddle

