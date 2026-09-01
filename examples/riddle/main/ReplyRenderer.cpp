// SPDX-License-Identifier: GPL-2.0-only

#include "ReplyRenderer.h"

#include <algorithm>

namespace {

size_t utf8CodepointBytes(const String &text, size_t offset)
{
    if (offset >= text.length()) {
        return 0;
    }
    const uint8_t lead = static_cast<uint8_t>(text[offset]);
    size_t bytes = 1;
    if ((lead & 0xE0U) == 0xC0U) {
        bytes = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
        bytes = 3;
    } else if ((lead & 0xF8U) == 0xF0U) {
        bytes = 4;
    }
    return std::min(bytes, text.length() - offset);
}

riddle::Rect includeRect(const riddle::Rect &a, const riddle::Rect &b)
{
    if (a.w <= 0 || a.h <= 0) {
        return b;
    }
    if (b.w <= 0 || b.h <= 0) {
        return a;
    }
    const int left = std::min(a.x, b.x);
    const int top = std::min(a.y, b.y);
    const int right = std::max(a.x + a.w, b.x + b.w);
    const int bottom = std::max(a.y + a.h, b.y + b.h);
    return {left, top, right - left, bottom - top};
}

} // namespace

namespace riddle {

constexpr size_t ReplyRenderer::kMaxLines;
constexpr int ReplyRenderer::kMarginX;
constexpr int ReplyRenderer::kMarginY;

bool ReplyRenderer::begin(lgfx::LovyanGFX &surface, const char *text)
{
    clear();
    if (text == nullptr || text[0] == '\0') {
        text = "...";
    }

    surface.setFont(&fonts::efontCN_24);
    surface.setTextSize(1.0f);
    surface.setTextWrap(false);
    line_height_ = static_cast<int16_t>(surface.fontHeight() + 12);
    const int max_width = surface.width() - kMarginX * 2;
    const int max_lines = std::max(1, (surface.height() - kMarginY * 2) /
                                          line_height_);

    line_count_ = 1;
    int current_width = 0;
    const String input(text);
    text_bytes_ = input.length();
    for (size_t offset = 0; offset < input.length();) {
        const size_t count = utf8CodepointBytes(input, offset);
        String glyph = input.substring(offset, offset + count);
        offset += count;

        if (glyph == "\r") {
            continue;
        }
        if (glyph == "\n") {
            if (line_count_ >= kMaxLines ||
                static_cast<int>(line_count_) >= max_lines) {
                break;
            }
            ++line_count_;
            current_width = 0;
            continue;
        }

        int glyph_width = surface.textWidth(glyph);
        if (glyph_width <= 0) {
            glyph = "?";
            glyph_width = surface.textWidth(glyph);
        }
        if (current_width > 0 && current_width + glyph_width > max_width) {
            if (line_count_ >= kMaxLines ||
                static_cast<int>(line_count_) >= max_lines) {
                break;
            }
            ++line_count_;
            current_width = 0;
            if (glyph == " ") {
                continue;
            }
        }
        lines_[line_count_ - 1] += glyph;
        current_width += glyph_width;
    }

    const int total_height = static_cast<int>(line_count_) * line_height_;
    const int start_y = std::max(kMarginY,
        std::min(surface.height() - kMarginY - total_height,
                 (surface.height() - total_height) / 3));
    for (size_t i = 0; i < line_count_; ++i) {
        const int width = surface.textWidth(lines_[i]);
        line_x_[i] = static_cast<int16_t>(
            std::max(kMarginX, (surface.width() - width) / 2));
        line_y_[i] = static_cast<int16_t>(start_y + i * line_height_);
        region_ = includeRect(region_,
                              {line_x_[i], line_y_[i], width,
                               surface.fontHeight() + 3});
    }
    return line_count_ != 0;
}

Rect ReplyRenderer::drawNext(lgfx::LovyanGFX &surface, uint8_t glyph_budget)
{
    Rect dirty = {};
    if (done() || glyph_budget == 0) {
        return dirty;
    }

    surface.setFont(&fonts::efontCN_24);
    surface.setTextSize(1.0f);
    surface.setTextWrap(false);
    surface.setTextDatum(textdatum_t::top_left);
    surface.setTextColor(TFT_BLACK);

    while (glyph_budget != 0 && line_index_ < line_count_) {
        const String &line = lines_[line_index_];
        if (byte_index_ >= line.length()) {
            ++line_index_;
            byte_index_ = 0;
            continue;
        }

        const size_t count = utf8CodepointBytes(line, byte_index_);
        String glyph = line.substring(byte_index_, byte_index_ + count);
        byte_index_ += count;
        int width = surface.textWidth(glyph);
        if (width <= 0) {
            glyph = "?";
            width = surface.textWidth(glyph);
        }

        int x = line_x_[line_index_];
        if (byte_index_ > count) {
            x += surface.textWidth(line.substring(0, byte_index_ - count));
        }
        const int y = line_y_[line_index_];
        surface.drawString(glyph, x, y);
        dirty = includeRect(dirty, {x - 1, y - 1, width + 3,
                                    surface.fontHeight() + 4});
        --glyph_budget;
    }
    return dirty;
}

bool ReplyRenderer::done() const
{
    return line_index_ >= line_count_;
}

Rect ReplyRenderer::region() const
{
    return region_;
}

size_t ReplyRenderer::textBytes() const
{
    return text_bytes_;
}

void ReplyRenderer::clear()
{
    for (String &line : lines_) {
        line = String();
    }
    line_count_ = 0;
    line_index_ = 0;
    byte_index_ = 0;
    text_bytes_ = 0;
    line_height_ = 0;
    region_ = {};
}

} // namespace riddle
