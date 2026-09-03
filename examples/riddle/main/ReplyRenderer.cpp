// SPDX-License-Identifier: GPL-2.0-only

#include "ReplyRenderer.h"

#include <algorithm>
#include <cstdint>

namespace {

const lgfx::IFont *const kReplyFonts[] = {
    &fonts::efontCN_24,
    &fonts::efontTW_24,
    &fonts::efontJA_24,
};

struct DecodedCodepoint {
    DecodedCodepoint(uint32_t value_in, size_t bytes_in, bool valid_in)
        : value(value_in), bytes(bytes_in), valid(valid_in)
    {
    }

    uint32_t value;
    size_t bytes;
    bool valid;
};

bool isUtf8Continuation(uint8_t value)
{
    return (value & 0xC0U) == 0x80U;
}

DecodedCodepoint decodeUtf8(const String &text, size_t offset)
{
    if (offset >= text.length()) {
        return {0, 0, false};
    }
    const uint8_t lead = static_cast<uint8_t>(text[offset]);
    if (lead < 0x80U) {
        return {lead, 1, true};
    }

    size_t bytes = 0;
    uint32_t codepoint = 0;
    uint32_t minimum = 0;
    if (lead >= 0xC2U && lead <= 0xDFU) {
        bytes = 2;
        codepoint = lead & 0x1FU;
        minimum = 0x80U;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        bytes = 3;
        codepoint = lead & 0x0FU;
        minimum = 0x800U;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        bytes = 4;
        codepoint = lead & 0x07U;
        minimum = 0x10000U;
    } else {
        return {0xFFFDU, 1, false};
    }
    if (offset + bytes > text.length()) {
        return {0xFFFDU, 1, false};
    }
    for (size_t index = 1; index < bytes; ++index) {
        const uint8_t value = static_cast<uint8_t>(text[offset + index]);
        if (!isUtf8Continuation(value)) {
            return {0xFFFDU, 1, false};
        }
        codepoint = (codepoint << 6) | (value & 0x3FU);
    }
    if (codepoint < minimum || codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        return {0xFFFDU, bytes, false};
    }
    return {codepoint, bytes, true};
}

String encodeUtf8(uint32_t codepoint)
{
    char bytes[5] = {};
    if (codepoint <= 0x7FU) {
        bytes[0] = static_cast<char>(codepoint);
    } else if (codepoint <= 0x7FFU) {
        bytes[0] = static_cast<char>(0xC0U | (codepoint >> 6));
        bytes[1] = static_cast<char>(0x80U | (codepoint & 0x3FU));
    } else if (codepoint <= 0xFFFFU) {
        bytes[0] = static_cast<char>(0xE0U | (codepoint >> 12));
        bytes[1] = static_cast<char>(
            0x80U | ((codepoint >> 6) & 0x3FU));
        bytes[2] = static_cast<char>(0x80U | (codepoint & 0x3FU));
    } else {
        bytes[0] = static_cast<char>(0xF0U | (codepoint >> 18));
        bytes[1] = static_cast<char>(
            0x80U | ((codepoint >> 12) & 0x3FU));
        bytes[2] = static_cast<char>(
            0x80U | ((codepoint >> 6) & 0x3FU));
        bytes[3] = static_cast<char>(0x80U | (codepoint & 0x3FU));
    }
    return String(bytes);
}

const lgfx::IFont *replyFontFor(uint32_t codepoint)
{
    if (codepoint > 0xFFFFU) {
        return nullptr;
    }
    for (const lgfx::IFont *font : kReplyFonts) {
        lgfx::FontMetrics metrics = {};
        font->getDefaultMetric(&metrics);
        if (font->updateFontMetric(
                &metrics, static_cast<uint16_t>(codepoint))) {
            return font;
        }
    }
    return nullptr;
}

bool isIgnorableCodepoint(uint32_t codepoint)
{
    return codepoint == 0x00ADU || codepoint == 0x200BU ||
           codepoint == 0x200CU || codepoint == 0x200DU ||
           codepoint == 0x2060U || codepoint == 0xFEFFU ||
           (codepoint >= 0x0300U && codepoint <= 0x036FU) ||
           (codepoint >= 0xFE00U && codepoint <= 0xFE0FU) ||
           (codepoint >= 0xE0100U && codepoint <= 0xE01EFU);
}

String normalizeCodepoint(uint32_t codepoint, bool &substituted)
{
    substituted = false;
    if (isIgnorableCodepoint(codepoint)) {
        return String();
    }
    if (codepoint == '\t' || codepoint == 0x00A0U ||
        (codepoint >= 0x2000U && codepoint <= 0x200AU) ||
        codepoint == 0x202FU || codepoint == 0x205FU ||
        codepoint == 0x3000U) {
        substituted = codepoint != ' ';
        return " ";
    }

    substituted = true;
    switch (codepoint) {
    case 0x2018U:
    case 0x2019U:
    case 0x201AU:
    case 0x201BU:
    case 0x2032U: return "'";
    case 0x201CU:
    case 0x201DU:
    case 0x201EU:
    case 0x201FU:
    case 0x2033U: return "\"";
    case 0x2010U:
    case 0x2011U:
    case 0x2012U:
    case 0x2013U:
    case 0x2014U:
    case 0x2015U:
    case 0x2212U: return "-";
    case 0x2022U:
    case 0x25CFU:
    case 0x25E6U: return "*";
    case 0x2026U: return "...";
    case 0x2190U: return "<-";
    case 0x2192U: return "->";
    case 0x00D7U: return "x";
    case 0x00F7U: return "/";
    case 0xFFFDU: return "?";
    default: break;
    }

    if (replyFontFor(codepoint) != nullptr) {
        substituted = false;
        return encodeUtf8(codepoint);
    }
    return "?";
}

size_t lastUtf8Offset(const String &text)
{
    if (text.length() == 0) {
        return 0;
    }
    size_t offset = text.length() - 1U;
    while (offset > 0 &&
           isUtf8Continuation(static_cast<uint8_t>(text[offset]))) {
        --offset;
    }
    return offset;
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

bool ReplyRenderer::begin(lgfx::LovyanGFX &surface, const char *text,
                          bool input_truncated)
{
    clear();
    if (text == nullptr || text[0] == '\0') {
        text = "...";
    }

    surface.setFont(&fonts::efontCN_24);
    surface.setTextSize(1.0f);
    surface.setTextWrap(false);
    for (const lgfx::IFont *font : kReplyFonts) {
        font_height_ = static_cast<int16_t>(
            std::max<int>(font_height_, surface.fontHeight(font)));
    }
    line_height_ = static_cast<int16_t>(font_height_ + 12);
    const int max_width = surface.width() - kMarginX * 2;
    const int max_lines = std::max(1, (surface.height() - kMarginY * 2) /
                                          line_height_);

    line_count_ = 1;
    const String input(text);
    size_t substitutions = 0;
    size_t font_fallbacks = 0;
    bool layout_truncated = false;

    auto appendGlyph = [&](String glyph, uint32_t codepoint) {
        const lgfx::IFont *font = replyFontFor(codepoint);
        if (font == nullptr) {
            glyph = "?";
            codepoint = '?';
            font = replyFontFor(codepoint);
            ++substitutions;
        }
        int glyph_width = surface.textWidth(glyph, font);
        if (glyph_width <= 0) {
            glyph = "?";
            font = &fonts::efontCN_24;
            glyph_width = surface.textWidth(glyph, font);
            ++substitutions;
        }
        size_t line = line_count_ - 1U;
        if (line_width_[line] > 0 &&
            line_width_[line] + glyph_width > max_width) {
            if (line_count_ >= kMaxLines ||
                static_cast<int>(line_count_) >= max_lines) {
                return false;
            }
            line = line_count_++;
            if (glyph == " ") {
                return true;
            }
        }
        lines_[line] += glyph;
        line_width_[line] = static_cast<int16_t>(
            line_width_[line] + glyph_width);
        text_bytes_ += glyph.length();
        if (font != &fonts::efontCN_24) {
            ++font_fallbacks;
        }
        return true;
    };

    for (size_t offset = 0; offset < input.length();) {
        const DecodedCodepoint decoded = decodeUtf8(input, offset);
        offset += decoded.bytes;
        if (decoded.value == '\r') {
            continue;
        }
        if (decoded.value == '\n') {
            if (line_count_ >= kMaxLines ||
                static_cast<int>(line_count_) >= max_lines) {
                layout_truncated = true;
                break;
            }
            ++line_count_;
            continue;
        }

        bool substituted = false;
        const String display = normalizeCodepoint(decoded.value, substituted);
        if (substituted || !decoded.valid) {
            ++substitutions;
        }
        for (size_t display_offset = 0;
             display_offset < display.length();) {
            const DecodedCodepoint visible =
                decodeUtf8(display, display_offset);
            const String glyph = display.substring(
                display_offset, display_offset + visible.bytes);
            display_offset += visible.bytes;
            if (!appendGlyph(glyph, visible.value)) {
                layout_truncated = true;
                break;
            }
        }
        if (layout_truncated) {
            break;
        }
    }

    if (input_truncated || layout_truncated) {
        const String marker = "...";
        const int marker_width =
            surface.textWidth(marker, &fonts::efontCN_24);
        const size_t line = line_count_ - 1U;

        auto removeLastGlyph = [&]() {
            const size_t last = lastUtf8Offset(lines_[line]);
            const DecodedCodepoint removed = decodeUtf8(lines_[line], last);
            const String glyph = lines_[line].substring(last);
            const lgfx::IFont *font = replyFontFor(removed.value);
            const int width = surface.textWidth(
                glyph, font != nullptr ? font : &fonts::efontCN_24);
            lines_[line].remove(last);
            line_width_[line] = static_cast<int16_t>(
                std::max(0, static_cast<int>(line_width_[line]) - width));
            text_bytes_ = text_bytes_ >= glyph.length()
                              ? text_bytes_ - glyph.length()
                              : 0;
        };

        while (lines_[line].length() != 0) {
            const size_t last = lastUtf8Offset(lines_[line]);
            const DecodedCodepoint trailing =
                decodeUtf8(lines_[line], last);
            if (trailing.value != '.' && trailing.value != ' ') {
                break;
            }
            removeLastGlyph();
        }
        while (lines_[line].length() != 0 &&
               line_width_[line] + marker_width > max_width) {
            removeLastGlyph();
        }
        if (line_width_[line] + marker_width <= max_width) {
            lines_[line] += marker;
            line_width_[line] = static_cast<int16_t>(
                line_width_[line] + marker_width);
            text_bytes_ += marker.length();
        }
    }

    if (substitutions != 0 || font_fallbacks != 0 || input_truncated ||
        layout_truncated) {
        Serial.printf(
            "[riddle/reply] glyphs: %u substituted, %u fallback, "
            "input_truncated=%s, layout_truncated=%s\n",
            static_cast<unsigned>(substitutions),
            static_cast<unsigned>(font_fallbacks),
            input_truncated ? "yes" : "no",
            layout_truncated ? "yes" : "no");
    }

    const int total_height = static_cast<int>(line_count_) * line_height_;
    const int start_y = std::max(kMarginY,
        std::min(surface.height() - kMarginY - total_height,
                  (surface.height() - total_height) / 3));
    for (size_t i = 0; i < line_count_; ++i) {
        const int width = line_width_[i];
        line_x_[i] = static_cast<int16_t>(
            std::max(kMarginX, (surface.width() - width) / 2));
        line_y_[i] = static_cast<int16_t>(start_y + i * line_height_);
        region_ = includeRect(region_,
                              {line_x_[i], line_y_[i], width,
                               font_height_ + 3});
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
            drawn_width_ = 0;
            continue;
        }

        const DecodedCodepoint decoded = decodeUtf8(line, byte_index_);
        String glyph = line.substring(
            byte_index_, byte_index_ + decoded.bytes);
        byte_index_ += decoded.bytes;
        const lgfx::IFont *font = replyFontFor(decoded.value);
        if (!decoded.valid || font == nullptr) {
            glyph = "?";
            font = &fonts::efontCN_24;
        }
        const int width = surface.textWidth(glyph, font);

        const int x = line_x_[line_index_] + drawn_width_;
        const int y = line_y_[line_index_];
        surface.drawString(glyph, x, y, font);
        dirty = includeRect(dirty, {x - 1, y - 1, width + 3,
                                    font_height_ + 4});
        drawn_width_ = static_cast<int16_t>(drawn_width_ + width);
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
    for (size_t index = 0; index < kMaxLines; ++index) {
        lines_[index] = String();
        line_x_[index] = 0;
        line_y_[index] = 0;
        line_width_[index] = 0;
    }
    line_count_ = 0;
    line_index_ = 0;
    byte_index_ = 0;
    text_bytes_ = 0;
    drawn_width_ = 0;
    font_height_ = 0;
    line_height_ = 0;
    region_ = {};
}

} // namespace riddle
