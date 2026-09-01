// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace riddle {

struct Rect {
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 0;
    int32_t h = 0;

    constexpr Rect() = default;
    constexpr Rect(int32_t left, int32_t top, int32_t width, int32_t height)
        : x(left), y(top), w(width), h(height)
    {
    }

    bool isEmpty() const
    {
        return w <= 0 || h <= 0;
    }

    int32_t right() const
    {
        if (isEmpty()) {
            return x;
        }
        const int64_t value = static_cast<int64_t>(x) + w;
        return static_cast<int32_t>(std::min<int64_t>(
            value, std::numeric_limits<int32_t>::max()));
    }

    int32_t bottom() const
    {
        if (isEmpty()) {
            return y;
        }
        const int64_t value = static_cast<int64_t>(y) + h;
        return static_cast<int32_t>(std::min<int64_t>(
            value, std::numeric_limits<int32_t>::max()));
    }

    Rect clipped(int32_t width, int32_t height) const
    {
        if (isEmpty() || width <= 0 || height <= 0) {
            return {};
        }
        const int64_t x0 = std::max<int64_t>(0, x);
        const int64_t y0 = std::max<int64_t>(0, y);
        const int64_t x1 = std::min<int64_t>(
            width, static_cast<int64_t>(x) + w);
        const int64_t y1 = std::min<int64_t>(
            height, static_cast<int64_t>(y) + h);
        if (x0 >= x1 || y0 >= y1) {
            return {};
        }
        return {static_cast<int32_t>(x0), static_cast<int32_t>(y0),
                static_cast<int32_t>(x1 - x0),
                static_cast<int32_t>(y1 - y0)};
    }

    Rect united(const Rect &other) const
    {
        if (isEmpty()) {
            return other;
        }
        if (other.isEmpty()) {
            return *this;
        }
        const int64_t x0 = std::min<int64_t>(x, other.x);
        const int64_t y0 = std::min<int64_t>(y, other.y);
        const int64_t x1 = std::max<int64_t>(
            static_cast<int64_t>(x) + w,
            static_cast<int64_t>(other.x) + other.w);
        const int64_t y1 = std::max<int64_t>(
            static_cast<int64_t>(y) + h,
            static_cast<int64_t>(other.y) + other.h);
        const int64_t max_value = std::numeric_limits<int32_t>::max();
        return {
            static_cast<int32_t>(std::max<int64_t>(
                std::numeric_limits<int32_t>::min(), x0)),
            static_cast<int32_t>(std::max<int64_t>(
                std::numeric_limits<int32_t>::min(), y0)),
            static_cast<int32_t>(std::min<int64_t>(max_value, x1 - x0)),
            static_cast<int32_t>(std::min<int64_t>(max_value, y1 - y0)),
        };
    }
};

// Inclusive bounds. The sentinel state is intentionally representable without
// a separate flag so copies remain cheap and trivially constructible.
struct BBox {
    int32_t x0 = std::numeric_limits<int32_t>::max();
    int32_t y0 = std::numeric_limits<int32_t>::max();
    int32_t x1 = std::numeric_limits<int32_t>::min();
    int32_t y1 = std::numeric_limits<int32_t>::min();

    bool isEmpty() const
    {
        return x0 > x1 || y0 > y1;
    }

    void clear()
    {
        x0 = std::numeric_limits<int32_t>::max();
        y0 = std::numeric_limits<int32_t>::max();
        x1 = std::numeric_limits<int32_t>::min();
        y1 = std::numeric_limits<int32_t>::min();
    }

    void add(int32_t x, int32_t y, int32_t margin = 0)
    {
        const int64_t radius = std::max<int32_t>(0, margin);
        const int64_t low = std::numeric_limits<int32_t>::min();
        const int64_t high = std::numeric_limits<int32_t>::max();
        const int32_t left = static_cast<int32_t>(
            std::max<int64_t>(low, static_cast<int64_t>(x) - radius));
        const int32_t top = static_cast<int32_t>(
            std::max<int64_t>(low, static_cast<int64_t>(y) - radius));
        const int32_t right = static_cast<int32_t>(
            std::min<int64_t>(high, static_cast<int64_t>(x) + radius));
        const int32_t bottom = static_cast<int32_t>(
            std::min<int64_t>(high, static_cast<int64_t>(y) + radius));
        x0 = std::min(x0, left);
        y0 = std::min(y0, top);
        x1 = std::max(x1, right);
        y1 = std::max(y1, bottom);
    }

    void include(const Rect &rect)
    {
        if (rect.isEmpty()) {
            return;
        }
        const int64_t right = static_cast<int64_t>(rect.x) + rect.w - 1;
        const int64_t bottom = static_cast<int64_t>(rect.y) + rect.h - 1;
        x0 = std::min(x0, rect.x);
        y0 = std::min(y0, rect.y);
        x1 = std::max<int64_t>(x1, std::min<int64_t>(
            right, std::numeric_limits<int32_t>::max()));
        y1 = std::max<int64_t>(y1, std::min<int64_t>(
            bottom, std::numeric_limits<int32_t>::max()));
    }

    void include(const BBox &other)
    {
        if (other.isEmpty()) {
            return;
        }
        x0 = std::min(x0, other.x0);
        y0 = std::min(y0, other.y0);
        x1 = std::max(x1, other.x1);
        y1 = std::max(y1, other.y1);
    }

    Rect toRect() const
    {
        if (isEmpty()) {
            return {};
        }
        const int64_t width = static_cast<int64_t>(x1) - x0 + 1;
        const int64_t height = static_cast<int64_t>(y1) - y0 + 1;
        const int64_t limit = std::numeric_limits<int32_t>::max();
        return {x0, y0,
                static_cast<int32_t>(std::min(width, limit)),
                static_cast<int32_t>(std::min(height, limit))};
    }
};

} // namespace riddle
