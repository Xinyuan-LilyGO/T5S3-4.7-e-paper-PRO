// SPDX-License-Identifier: GPL-2.0-only

#include <Arduino.h>
#include <M5GFX.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>

#include "GoodixGT6972P.h"
#include "T5EpaperDisplay.h"

namespace {

constexpr int kTouchSda = 39;
constexpr int kTouchScl = 40;
constexpr int kTouchInterrupt = 3;
constexpr int kTouchReset = 9;
constexpr int kBootPin = 0;

// The GT6972P sensor reports portrait coordinates independently of the EPD
// framebuffer size. Coordinates are inclusive in the mapping below.
constexpr uint16_t kTouchWidth = 684;
constexpr uint16_t kTouchHeight = 1216;

constexpr int kHeaderHeight = 156;
constexpr int kCanvasMargin = 8;
constexpr int kCanvasGap = 8;
constexpr int kCursorRadius = 14;
constexpr int kCursorPatchSize = kCursorRadius * 2 + 5;
constexpr uint16_t kPenMaxPressure = 4096;
constexpr int kMinBrushRadius = 1;
constexpr int kMaxBrushRadius = 8;
constexpr int kEraserRadius = 12;

constexpr uint32_t kActiveRefreshIntervalMs = 0;
constexpr uint32_t kIdleRefreshIntervalMs = 140;
constexpr uint32_t kStatusRenderIntervalMs = 300;
constexpr uint32_t kIdleQualityDelayMs = 900;
constexpr uint32_t kFallbackPollIntervalMs = 10;
constexpr uint32_t kTouchRetryIntervalMs = 5000;
constexpr uint32_t kBootDebounceMs = 30;
constexpr uint32_t kBootLongPressMs = 1000;
constexpr uint32_t kStrokeGapTimeoutMs = 160;
constexpr uint16_t kTouchQueueLength = 256;
constexpr uint32_t kTouchTaskStackSize = 4096;
constexpr UBaseType_t kTouchTaskPriority = 4;
constexpr BaseType_t kTouchTaskCore = 0;
constexpr uint32_t kPenUiTaskStackSize = 8192;
constexpr UBaseType_t kPenUiTaskPriority = 2;
constexpr BaseType_t kPenUiTaskCore = 0;

// Adjust these values only if the fitted sensor orientation differs.
constexpr bool kSwapAxes = false;
constexpr bool kInvertX = false;
constexpr bool kInvertY = false;

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    Rect() = default;
    Rect(int x_value, int y_value, int width, int height)
        : x(x_value), y(y_value), w(width), h(height)
    {
    }
};

struct DirtyRect {
    bool valid = false;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    void include(const Rect &rect)
    {
        if (rect.w <= 0 || rect.h <= 0) {
            return;
        }
        if (!valid) {
            valid = true;
            left = rect.x;
            top = rect.y;
            right = rect.x + rect.w - 1;
            bottom = rect.y + rect.h - 1;
            return;
        }
        left = std::min(left, rect.x);
        top = std::min(top, rect.y);
        right = std::max(right, rect.x + rect.w - 1);
        bottom = std::max(bottom, rect.y + rect.h - 1);
    }

    Rect rectangle() const
    {
        return valid ? Rect{left, top, right - left + 1, bottom - top + 1}
                     : Rect{};
    }

    void clear() { valid = false; }
};

struct PenTelemetry {
    bool present = false;
    bool hover = false;
    bool button1 = false;
    bool button2 = false;
    bool eraser = false;
    uint8_t finger_count = 0;
    uint16_t raw_x = 0;
    uint16_t raw_y = 0;
    uint16_t pixel_x = 0;
    uint16_t pixel_y = 0;
    uint16_t pressure = 0;
    int8_t tilt_x = 0;
    int8_t tilt_y = 0;
    int8_t battery = -1;
};

struct QueuedTouchSample {
    uint32_t captured_ms = 0;
    GoodixGT6972P::ReadResult result =
        GoodixGT6972P::ReadResult::kNoData;
    const char *error = "none";
    bool has_touch = false;
    uint8_t finger_count = 0;
    GoodixGT6972P::PenPoint pen = {};
};

struct CursorOverlay {
    bool valid = false;
    Rect rect = {};
    uint16_t background[kCursorPatchSize * kCursorPatchSize] = {};
};

GoodixGT6972P touch;
Rect canvas;
DirtyRect dirty;
PenTelemetry pen;
CursorOverlay cursor;

bool display_ready = false;
volatile bool touch_ready = false;
bool status_dirty = true;
bool cleanup_pending = false;
bool force_text_refresh = false;
bool force_quality_refresh = false;

bool stroke_valid = false;
bool previous_stroke_eraser = false;
int previous_stroke_x = 0;
int previous_stroke_y = 0;
int previous_stroke_radius = 1;
uint32_t previous_stroke_ms = 0;

bool boot_raw_pressed = false;
bool boot_stable_pressed = false;
bool boot_long_handled = false;
uint32_t boot_raw_change_ms = 0;
uint32_t boot_press_ms = 0;

uint32_t total_samples = 0;
uint32_t total_errors = 0;
uint32_t consecutive_errors = 0;
uint32_t rate_window_start_ms = 0;
uint32_t rate_window_samples = 0;
uint32_t rate_window_vsync = 0;
uint16_t sample_rate_hz = 0;
uint16_t display_rate_fps = 0;
uint32_t last_contact_ms = 0;
uint32_t last_display_ms = 0;
uint32_t last_status_render_ms = 0;
uint32_t last_touch_poll_ms = 0;
uint32_t last_touch_retry_ms = 0;
const char *last_error = "none";

QueueHandle_t touch_queue = nullptr;
SemaphoreHandle_t touch_driver_mutex = nullptr;
TaskHandle_t touch_task_handle = nullptr;
TaskHandle_t pen_ui_task_handle = nullptr;
volatile uint32_t dropped_touch_samples = 0;

lgfx::LovyanGFX &screen()
{
    return t5epd::display();
}

uint32_t grayColor(uint8_t gray)
{
    return screen().color888(gray, gray, gray);
}

Rect clipToScreen(const Rect &rect)
{
    const int left = std::max(0, rect.x);
    const int top = std::max(0, rect.y);
    const int right = std::min(screen().width(), rect.x + rect.w);
    const int bottom = std::min(screen().height(), rect.y + rect.h);
    return {left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

void markDirty(const Rect &rect)
{
    dirty.include(clipToScreen(rect));
}

void present(const Rect &region)
{
    const Rect clipped = clipToScreen(region);
    if (clipped.w <= 0 || clipped.h <= 0) {
        return;
    }
    t5epd::present(clipped.x, clipped.y, clipped.w, clipped.h);
}

const char *penStateLabel()
{
    if (!touch_ready) {
        return "OFFLINE";
    }
    if (consecutive_errors != 0) {
        return "ERROR";
    }
    if (pen.present && pen.eraser && !pen.hover) {
        return "ERASER";
    }
    if (pen.present && pen.hover) {
        return "HOVER";
    }
    if (pen.present) {
        return "CONTACT";
    }
    if (pen.finger_count != 0) {
        return "FINGER";
    }
    return "IDLE";
}

void drawIndicator(int x, int y, int width, const char *label, bool active)
{
    const uint32_t background = active ? TFT_BLACK : TFT_WHITE;
    const uint32_t foreground = active ? TFT_WHITE : TFT_BLACK;
    screen().fillRect(x, y, width, 26, background);
    screen().drawRect(x, y, width, 26, TFT_BLACK);
    screen().setFont(&fonts::Font2);
    screen().setTextDatum(textdatum_t::middle_center);
    screen().setTextColor(foreground, background);
    screen().drawString(label, x + width / 2, y + 13);
}

void drawTiltIndicator(int center_x, int center_y)
{
    constexpr int radius = 14;
    screen().drawCircle(center_x, center_y, radius, TFT_BLACK);
    screen().drawFastHLine(center_x - radius + 3, center_y,
                           radius * 2 - 5, grayColor(180));
    screen().drawFastVLine(center_x, center_y - radius + 3,
                           radius * 2 - 5, grayColor(180));

    const int tilt_x = std::max(-90, std::min(90, static_cast<int>(pen.tilt_x)));
    const int tilt_y = std::max(-90, std::min(90, static_cast<int>(pen.tilt_y)));
    const int tip_x = center_x + tilt_x * (radius - 3) / 90;
    const int tip_y = center_y + tilt_y * (radius - 3) / 90;
    screen().drawLine(center_x, center_y, tip_x, tip_y, TFT_BLACK);
    screen().fillCircle(tip_x, tip_y, 2, TFT_BLACK);
}

void drawStatusBar()
{
    char text[96];
    const int display_width = screen().width();
    const Rect header = {0, 0, screen().width(), kHeaderHeight};
    screen().fillRect(header.x, header.y, header.w, header.h, TFT_WHITE);
    screen().drawFastHLine(0, kHeaderHeight - 1, screen().width(), TFT_BLACK);

    screen().setTextDatum(textdatum_t::top_left);
    screen().setTextColor(TFT_BLACK, TFT_WHITE);
    screen().setFont(&fonts::Font4);
    screen().drawString("GT6972P PEN", 12, 8);

    const char *state = penStateLabel();
    const bool strong_state = strcmp(state, "CONTACT") == 0 ||
                              strcmp(state, "ERASER") == 0 ||
                              strcmp(state, "OFFLINE") == 0 ||
                              strcmp(state, "ERROR") == 0;
    const uint32_t state_background =
        strong_state ? TFT_BLACK
                     : (pen.present ? grayColor(205) : TFT_WHITE);
    const uint32_t state_foreground = strong_state ? TFT_WHITE : TFT_BLACK;
    constexpr int state_width = 126;
    const int state_x = display_width - state_width - 12;
    screen().fillRect(state_x, 8, state_width, 30, state_background);
    screen().drawRect(state_x, 8, state_width, 30, TFT_BLACK);
    screen().setTextDatum(textdatum_t::middle_center);
    screen().setTextColor(state_foreground, state_background);
    screen().drawString(state, state_x + state_width / 2, 23);

    screen().setFont(&fonts::Font2);
    screen().setTextDatum(textdatum_t::top_left);
    screen().setTextColor(TFT_BLACK, TFT_WHITE);
    if (touch_ready) {
        const GoodixGT6972P::DeviceInfo &info = touch.deviceInfo();
        snprintf(text, sizeof(text),
                 "PID %s  I2C 0x%02X  TOUCH %ux%u  LCD %dx%d",
                 info.patch_pid, info.address, kTouchWidth, kTouchHeight,
                 screen().width(), screen().height());
    } else {
        snprintf(text, sizeof(text), "PID --   I2C --   %s", last_error);
    }
    screen().drawString(text, 12, 43);

    screen().setTextDatum(textdatum_t::top_left);
    screen().setTextColor(TFT_BLACK, TFT_WHITE);
    snprintf(text, sizeof(text), "RAW %3u,%4u", pen.raw_x, pen.raw_y);
    screen().drawString(text, 12, 68);
    snprintf(text, sizeof(text), "PIX %3u,%3u", pen.pixel_x, pen.pixel_y);
    screen().drawString(text, 154, 68);
    snprintf(text, sizeof(text), "P %4u", pen.pressure);
    screen().drawString(text, 282, 68);

    constexpr int pressure_bar_x = 338;
    constexpr int pressure_bar_y = 71;
    const int pressure_bar_width = display_width - pressure_bar_x - 12;
    screen().drawRect(pressure_bar_x, pressure_bar_y, pressure_bar_width, 14,
                      TFT_BLACK);
    const int pressure_width =
        std::min<uint32_t>(pen.pressure, kPenMaxPressure) *
        (pressure_bar_width - 4) / kPenMaxPressure;
    if (pressure_width > 0) {
        screen().fillRect(pressure_bar_x + 2, pressure_bar_y + 2,
                          pressure_width, 10, TFT_BLACK);
    }

    snprintf(text, sizeof(text), "TILT %+3d,%+3d", pen.tilt_x, pen.tilt_y);
    screen().drawString(text, 12, 94);
    drawTiltIndicator(140, 103);

    snprintf(text, sizeof(text), "BAT %s", pen.battery >= 0 ? "" : "--");
    screen().drawString(text, 162, 94);
    if (pen.battery >= 0) {
        snprintf(text, sizeof(text), "%d%%", pen.battery);
        screen().drawString(text, 198, 94);
    }
    snprintf(text, sizeof(text), "PEN %3u EPD %2u", sample_rate_hz,
             display_rate_fps);
    screen().drawString(text, 240, 94);
    snprintf(text, sizeof(text), "SAMPLES %lu",
             static_cast<unsigned long>(total_samples));
    screen().drawString(text, 364, 94);

    drawIndicator(12, 122, 58, "B1", pen.button1);
    drawIndicator(76, 122, 58, "B2", pen.button2);
    drawIndicator(140, 122, 88, "ERASER", pen.eraser);
    drawIndicator(234, 122, 70, "BOOT", boot_stable_pressed);

    screen().setTextDatum(textdatum_t::top_left);
    screen().setTextColor(TFT_BLACK, TFT_WHITE);
    screen().setFont(&fonts::Font2);
    snprintf(text, sizeof(text), "FINGER %u", pen.finger_count);
    screen().drawString(text, 316, 128);
    snprintf(text, sizeof(text), "ERR %lu Q %lu",
             static_cast<unsigned long>(total_errors),
             static_cast<unsigned long>(dropped_touch_samples));
    screen().drawString(text, 410, 128);

}

void drawCanvasBase()
{
    screen().fillRect(canvas.x, canvas.y, canvas.w, canvas.h, TFT_WHITE);
    screen().drawRect(canvas.x, canvas.y, canvas.w, canvas.h, TFT_BLACK);

    const uint32_t guide = grayColor(190);
    const int center_x = canvas.x + canvas.w / 2;
    const int center_y = canvas.y + canvas.h / 2;
    screen().drawFastHLine(center_x - 16, center_y, 33, guide);
    screen().drawFastVLine(center_x, center_y - 16, 33, guide);

    if (!touch_ready) {
        screen().setTextDatum(textdatum_t::middle_center);
        screen().setTextColor(TFT_BLACK, TFT_WHITE);
        screen().setFont(&fonts::Font4);
        screen().drawString("TOUCH INITIALIZATION FAILED",
                            center_x, center_y - 18);
        screen().setFont(&fonts::Font2);
        screen().drawString(last_error, center_x, center_y + 20);
    }
    markDirty(canvas);
}

void restoreCursor()
{
    if (!cursor.valid) {
        return;
    }
    screen().pushImage(cursor.rect.x, cursor.rect.y, cursor.rect.w,
                       cursor.rect.h, cursor.background);
    markDirty(cursor.rect);
    cursor.valid = false;
}

void showHoverCursor(int x, int y)
{
    screen().startWrite();
    restoreCursor();

    const Rect requested = {x - kCursorRadius - 2, y - kCursorRadius - 2,
                            kCursorPatchSize, kCursorPatchSize};
    cursor.rect = clipToScreen(requested);
    screen().readRect(cursor.rect.x, cursor.rect.y, cursor.rect.w,
                      cursor.rect.h, cursor.background);
    cursor.valid = true;

    screen().setClipRect(canvas.x + 1, canvas.y + 1,
                         canvas.w - 2, canvas.h - 2);
    screen().drawCircle(x, y, kCursorRadius, TFT_BLACK);
    screen().drawFastHLine(x - kCursorRadius - 4, y,
                           kCursorRadius * 2 + 9, TFT_BLACK);
    screen().drawFastVLine(x, y - kCursorRadius - 4,
                           kCursorRadius * 2 + 9, TFT_BLACK);
    screen().clearClipRect();
    screen().endWrite();
    markDirty(cursor.rect);
}

int pressureRadius(uint16_t pressure)
{
    const uint32_t limited = std::min<uint32_t>(pressure, kPenMaxPressure);
    return kMinBrushRadius +
           limited * (kMaxBrushRadius - kMinBrushRadius) / kPenMaxPressure;
}

void drawBrushSegment(int x0, int y0, int radius0, int x1, int y1,
                      int radius1, uint32_t color)
{
    const int dx = x1 - x0;
    const int dy = y1 - y0;
    const int steps = std::max(abs(dx), abs(dy));
    if (steps == 0) {
        screen().fillCircle(x1, y1, radius1, color);
        return;
    }

    for (int step = 0; step <= steps; ++step) {
        const int x = x0 + dx * step / steps;
        const int y = y0 + dy * step / steps;
        const int radius = radius0 + (radius1 - radius0) * step / steps;
        screen().fillCircle(x, y, radius, color);
    }
}

void drawPenStroke(int x, int y, uint16_t pressure, bool eraser,
                   uint32_t now)
{
    const int radius = eraser ? kEraserRadius : pressureRadius(pressure);
    const uint32_t color = eraser ? TFT_WHITE : TFT_BLACK;
    const bool can_join = stroke_valid && previous_stroke_eraser == eraser &&
                          now - previous_stroke_ms <= kStrokeGapTimeoutMs;

    screen().startWrite();
    restoreCursor();
    screen().setClipRect(canvas.x + 1, canvas.y + 1,
                         canvas.w - 2, canvas.h - 2);
    if (can_join) {
        drawBrushSegment(previous_stroke_x, previous_stroke_y,
                         previous_stroke_radius, x, y, radius, color);
    } else {
        screen().fillCircle(x, y, radius, color);
    }
    screen().clearClipRect();
    screen().endWrite();

    const int left = can_join ? std::min(previous_stroke_x, x) : x;
    const int top = can_join ? std::min(previous_stroke_y, y) : y;
    const int right = can_join ? std::max(previous_stroke_x, x) : x;
    const int bottom = can_join ? std::max(previous_stroke_y, y) : y;
    const int margin = std::max(radius, previous_stroke_radius) + 3;
    markDirty({left - margin, top - margin,
               right - left + margin * 2 + 1,
               bottom - top + margin * 2 + 1});

    stroke_valid = true;
    previous_stroke_eraser = eraser;
    previous_stroke_x = x;
    previous_stroke_y = y;
    previous_stroke_radius = radius;
    previous_stroke_ms = now;
}

void mapPenCoordinates(uint16_t raw_x, uint16_t raw_y, int &pixel_x,
                       int &pixel_y)
{
    uint32_t x = raw_x;
    uint32_t y = raw_y;
    uint32_t max_x = kTouchWidth - 1;
    uint32_t max_y = kTouchHeight - 1;

    if (kSwapAxes) {
        std::swap(x, y);
        std::swap(max_x, max_y);
    }
    x = std::min(x, max_x);
    y = std::min(y, max_y);
    if (kInvertX) {
        x = max_x - x;
    }
    if (kInvertY) {
        y = max_y - y;
    }

    const int display_width = screen().width();
    const int display_height = screen().height();
    pixel_x = static_cast<int>(x * (display_width - 1) / max_x);
    pixel_y = static_cast<int>(y * (display_height - 1) / max_y);
}

void releasePen(uint32_t now)
{
    const bool was_contact = pen.present && !pen.hover;
    if (cursor.valid) {
        screen().startWrite();
        restoreCursor();
        screen().endWrite();
    }
    if (was_contact) {
        cleanup_pending = true;
        last_contact_ms = now;
    }
    pen.present = false;
    pen.hover = false;
    pen.button1 = false;
    pen.button2 = false;
    pen.eraser = false;
    pen.pressure = 0;
    stroke_valid = false;
}

void handleTouchSample(const QueuedTouchSample &sample)
{
    if (!sample.has_touch) {
        return;
    }

    const uint32_t now = sample.captured_ms;
    const bool was_contact = pen.present && !pen.hover;
    pen.finger_count = sample.finger_count;
    if (!sample.pen.present) {
        releasePen(now);
        status_dirty = true;
        return;
    }

    pen.present = true;
    pen.hover = sample.pen.hover;
    pen.button1 = sample.pen.button1;
    pen.button2 = sample.pen.button2;
    pen.eraser = sample.pen.eraser;
    pen.raw_x = sample.pen.x;
    pen.raw_y = sample.pen.y;
    pen.pressure = sample.pen.pressure;
    pen.tilt_x = sample.pen.tilt_x;
    pen.tilt_y = sample.pen.tilt_y;
    pen.battery = sample.pen.battery;

    int pixel_x = 0;
    int pixel_y = 0;
    mapPenCoordinates(pen.raw_x, pen.raw_y, pixel_x, pixel_y);
    pen.pixel_x = static_cast<uint16_t>(pixel_x);
    pen.pixel_y = static_cast<uint16_t>(pixel_y);

    ++total_samples;
    ++rate_window_samples;

    if (pen.hover) {
        if (was_contact) {
            cleanup_pending = true;
            last_contact_ms = now;
        }
        stroke_valid = false;
        showHoverCursor(pixel_x, pixel_y);
    } else {
        cleanup_pending = false;
        last_contact_ms = now;
        drawPenStroke(pixel_x, pixel_y, pen.pressure, pen.eraser, now);
    }
    status_dirty = true;
}

const char *readResultName(GoodixGT6972P::ReadResult result)
{
    switch (result) {
    case GoodixGT6972P::ReadResult::kBusError:
        return "bus error";
    case GoodixGT6972P::ReadResult::kChecksumError:
        return "checksum error";
    case GoodixGT6972P::ReadResult::kFormatError:
        return "format error";
    case GoodixGT6972P::ReadResult::kNoData:
        return "no data";
    case GoodixGT6972P::ReadResult::kEvent:
        return "event";
    }
    return "unknown error";
}

void handleReadError(GoodixGT6972P::ReadResult result, const char *detail)
{
    ++total_errors;
    ++consecutive_errors;
    last_error = detail;
    status_dirty = true;

    if (consecutive_errors == 1 || consecutive_errors % 20 == 0) {
        Serial.printf("[GT6972P_PEN] %s: %s\n", readResultName(result),
                      last_error);
    }
    if (consecutive_errors >= 20) {
        touch_ready = false;
        releasePen(millis());
        last_touch_retry_ms = millis();
    }
}

bool initializeTouch()
{
    touch_ready = false;
    if (touch_driver_mutex != nullptr) {
        xSemaphoreTake(touch_driver_mutex, portMAX_DELAY);
    }

    const bool initialized = touch.begin(Wire, kTouchSda, kTouchScl,
                                         kTouchReset, kTouchInterrupt);
    if (touch_queue != nullptr) {
        xQueueReset(touch_queue);
    }
    touch_ready = initialized;
    if (touch_driver_mutex != nullptr) {
        xSemaphoreGive(touch_driver_mutex);
    }
    consecutive_errors = 0;
    if (!initialized) {
        last_error = touch.lastError();
        Serial.printf("[GT6972P_PEN] touch initialization failed: %s\n",
                      last_error);
        return false;
    }

    last_error = "none";
    const GoodixGT6972P::DeviceInfo &info = touch.deviceInfo();
    Serial.printf(
        "[GT6972P_PEN] ready PID=%s address=0x%02X "
        "IC-range=%ux%u touch=%ux%u display=%dx%d\n",
        info.patch_pid, info.address, info.max_x, info.max_y,
        kTouchWidth, kTouchHeight, screen().width(), screen().height());
    return true;
}

void touchSamplerTask(void *unused)
{
    (void)unused;
    for (;;) {
        if (!touch_ready || touch_queue == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        const uint32_t now = millis();
        if (!touch.interruptAsserted() &&
            now - last_touch_poll_ms < kFallbackPollIntervalMs) {
            vTaskDelay(1);
            continue;
        }
        last_touch_poll_ms = now;

        xSemaphoreTake(touch_driver_mutex, portMAX_DELAY);
        if (!touch_ready) {
            xSemaphoreGive(touch_driver_mutex);
            continue;
        }
        GoodixGT6972P::Event event;
        const GoodixGT6972P::ReadResult result = touch.readEvent(event);
        const char *detail = touch.lastError();

        if (result == GoodixGT6972P::ReadResult::kNoData) {
            xSemaphoreGive(touch_driver_mutex);
            vTaskDelay(1);
            continue;
        }

        QueuedTouchSample sample;
        sample.captured_ms = now;
        sample.result = result;
        sample.error = detail;
        if (result == GoodixGT6972P::ReadResult::kEvent) {
            sample.has_touch = event.has_touch;
            sample.finger_count = event.finger_count;
            sample.pen = event.pen;
        }
        if (xQueueSend(touch_queue, &sample, 0) != pdTRUE) {
            ++dropped_touch_samples;
        }
        xSemaphoreGive(touch_driver_mutex);
    }
}

bool startTouchSampler()
{
    touch_queue = xQueueCreate(kTouchQueueLength,
                               sizeof(QueuedTouchSample));
    touch_driver_mutex = xSemaphoreCreateMutex();
    if (touch_queue == nullptr || touch_driver_mutex == nullptr) {
        last_error = "touch sampler allocation failed";
        return false;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(
        touchSamplerTask, "gt6972p", kTouchTaskStackSize, nullptr,
        kTouchTaskPriority, &touch_task_handle, kTouchTaskCore);
    if (created != pdPASS) {
        touch_task_handle = nullptr;
        last_error = "touch sampler task failed";
        return false;
    }
    Serial.printf("[GT6972P_PEN] sampler started: queue=%u core=%d "
                  "priority=%u\n",
                  kTouchQueueLength, static_cast<int>(kTouchTaskCore),
                  static_cast<unsigned>(kTouchTaskPriority));
    return true;
}

void pollTouchSynchronously()
{
    const uint32_t now = millis();
    if (!touch.interruptAsserted() &&
        now - last_touch_poll_ms < kFallbackPollIntervalMs) {
        return;
    }
    last_touch_poll_ms = now;

    GoodixGT6972P::Event event;
    const GoodixGT6972P::ReadResult result = touch.readEvent(event);
    if (result == GoodixGT6972P::ReadResult::kNoData) {
        return;
    }

    QueuedTouchSample sample;
    sample.captured_ms = now;
    sample.result = result;
    sample.error = touch.lastError();
    if (result == GoodixGT6972P::ReadResult::kEvent) {
        sample.has_touch = event.has_touch;
        sample.finger_count = event.finger_count;
        sample.pen = event.pen;
        consecutive_errors = 0;
        handleTouchSample(sample);
    } else {
        handleReadError(result, sample.error);
    }
}

void pollTouch()
{
    if (touch_task_handle == nullptr) {
        pollTouchSynchronously();
        return;
    }

    QueuedTouchSample sample;
    while (xQueueReceive(touch_queue, &sample, 0) == pdTRUE) {
        if (sample.result == GoodixGT6972P::ReadResult::kEvent) {
            consecutive_errors = 0;
            handleTouchSample(sample);
        } else {
            handleReadError(sample.result, sample.error);
        }
    }
}

void clearCanvas()
{
    screen().startWrite();
    restoreCursor();
    drawCanvasBase();
    screen().endWrite();
    stroke_valid = false;
    cleanup_pending = false;
    force_text_refresh = true;
}

void pollBootButton()
{
    const uint32_t now = millis();
    const bool raw_pressed = digitalRead(kBootPin) == LOW;
    if (raw_pressed != boot_raw_pressed) {
        boot_raw_pressed = raw_pressed;
        boot_raw_change_ms = now;
    }

    if (now - boot_raw_change_ms >= kBootDebounceMs &&
        boot_stable_pressed != boot_raw_pressed) {
        boot_stable_pressed = boot_raw_pressed;
        status_dirty = true;
        if (boot_stable_pressed) {
            boot_press_ms = now;
            boot_long_handled = false;
        } else if (!boot_long_handled) {
            clearCanvas();
        }
    }

    if (boot_stable_pressed && !boot_long_handled &&
        now - boot_press_ms >= kBootLongPressMs) {
        boot_long_handled = true;
        force_quality_refresh = true;
        status_dirty = true;
    }
}

void updateSampleRate()
{
    const uint32_t now = millis();
    if (rate_window_start_ms == 0) {
        rate_window_start_ms = now;
        rate_window_vsync = t5epd::vsyncCount();
        return;
    }
    const uint32_t elapsed = now - rate_window_start_ms;
    if (elapsed < 1000) {
        return;
    }

    sample_rate_hz = static_cast<uint16_t>(rate_window_samples * 1000 / elapsed);
    const uint32_t vsync = t5epd::vsyncCount();
    display_rate_fps = static_cast<uint16_t>(
        (vsync - rate_window_vsync) * 1000 / elapsed);
    rate_window_samples = 0;
    rate_window_vsync = vsync;
    rate_window_start_ms = now;
    status_dirty = true;
}

void retryTouchIfNeeded()
{
    if (touch_ready || millis() - last_touch_retry_ms < kTouchRetryIntervalMs) {
        return;
    }
    last_touch_retry_ms = millis();
    if (!initializeTouch()) {
        status_dirty = true;
        return;
    }

    pen = {};
    screen().startWrite();
    drawCanvasBase();
    drawStatusBar();
    screen().endWrite();
    force_quality_refresh = true;
    status_dirty = false;
}

void serviceDisplay()
{
    if (!display_ready) {
        return;
    }
    const uint32_t now = millis();

    if (force_quality_refresh) {
        if (status_dirty) {
            screen().startWrite();
            drawStatusBar();
            screen().endWrite();
            status_dirty = false;
            last_status_render_ms = now;
        }
        present({0, 0, screen().width(), screen().height()});
        dirty.clear();
        force_quality_refresh = false;
        force_text_refresh = false;
        cleanup_pending = false;
        last_display_ms = millis();
        return;
    }

    if (cleanup_pending && (!pen.present || pen.hover) &&
        now - last_contact_ms >= kIdleQualityDelayMs) {
        present(canvas);
        dirty.clear();
        cleanup_pending = false;
        force_text_refresh = false;
        last_display_ms = millis();
        return;
    }

    if (dirty.valid) {
        const uint32_t refresh_interval =
            pen.present ? kActiveRefreshIntervalMs : kIdleRefreshIntervalMs;
        if (now - last_display_ms < refresh_interval) {
            return;
        }

        const Rect update_region = dirty.rectangle();
        present(update_region);
        dirty.clear();
        force_text_refresh = false;
        last_display_ms = millis();
        return;
    }

    // Pen drawing takes priority. Refresh telemetry only between strokes so a
    // small pen dirty rectangle is never expanded to include the header.
    if (status_dirty && !pen.present &&
        now - last_status_render_ms >= kStatusRenderIntervalMs) {
        screen().startWrite();
        drawStatusBar();
        screen().endWrite();
        present({0, 0, screen().width(), kHeaderHeight});
        status_dirty = false;
        last_status_render_ms = millis();
    }
}

void servicePenUiOnce()
{
    pollBootButton();
    if (touch_ready) {
        pollTouch();
    } else {
        retryTouchIfNeeded();
    }
    updateSampleRate();
    serviceDisplay();
}

void penUiTask(void *unused)
{
    (void)unused;
    Serial.printf("[GT6972P_PEN] UI task started: core=%d priority=%u\n",
                  xPortGetCoreID(),
                  static_cast<unsigned>(kPenUiTaskPriority));
    for (;;) {
        servicePenUiOnce();
        vTaskDelay(1);
    }
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("[GT6972P_PEN] active pen display test start");

    pinMode(kBootPin, INPUT_PULLUP);
    if (!t5epd::begin()) {
        Serial.println("[GT6972P_PEN] display initialization failed");
        while (true) {
            delay(1000);
        }
    }
    display_ready = true;

    screen().setTextWrap(false);
    canvas = {kCanvasMargin, kHeaderHeight + kCanvasGap,
              screen().width() - kCanvasMargin * 2,
              screen().height() - kHeaderHeight - kCanvasGap - kCanvasMargin};

    initializeTouch();
    if (!startTouchSampler()) {
        Serial.printf("[GT6972P_PEN] %s; using synchronous polling\n",
                      last_error);
    }
    screen().startWrite();
    screen().fillScreen(TFT_WHITE);
    drawStatusBar();
    drawCanvasBase();
    screen().endWrite();
    present({0, 0, screen().width(), screen().height()});
    dirty.clear();
    status_dirty = false;
    last_display_ms = millis();
    last_status_render_ms = millis();
    last_touch_retry_ms = millis();

    const BaseType_t ui_created = xTaskCreatePinnedToCore(
        penUiTask, "pen_ui", kPenUiTaskStackSize, nullptr,
        kPenUiTaskPriority, &pen_ui_task_handle, kPenUiTaskCore);
    if (ui_created != pdPASS) {
        pen_ui_task_handle = nullptr;
        Serial.println("[GT6972P_PEN] UI task creation failed; "
                       "using Arduino loop");
    }
}

void loop()
{
    if (pen_ui_task_handle == nullptr) {
        servicePenUiOnce();
        delay(1);
        return;
    }
    delay(1000);
}
