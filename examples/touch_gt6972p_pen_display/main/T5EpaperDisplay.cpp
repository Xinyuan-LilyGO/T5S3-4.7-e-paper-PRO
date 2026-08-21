// SPDX-License-Identifier: MIT

#include "T5EpaperDisplay.h"

#include <Arduino.h>
#include <Wire.h>

#include <algorithm>
#include <cstring>

#include "../../epd_60fps_probe/main/epd_video.h"
#include "../../epd_60fps_probe/main/pca9535_min.h"
#include "../../epd_60fps_probe/main/t5s3_epd_pins.h"

namespace {

constexpr int kBacklightPin = 11;
constexpr uint8_t kSurfaceRotation = 3;
constexpr uint8_t kExpanderInitAttempts = 3;

Pca9535Min expander;
lgfx::LGFX_Sprite surface;
bool video_ready = false;

} // namespace

namespace t5epd {

bool begin()
{
    pinMode(kBacklightPin, OUTPUT);
    digitalWrite(kBacklightPin, LOW);

    bool expander_ready = false;
    for (uint8_t attempt = 0; attempt < kExpanderInitAttempts; ++attempt) {
        if (attempt != 0) {
            Wire.end();
            delay(10);
        }
        Wire.begin(t5s3_epd::kI2cSda, t5s3_epd::kI2cScl);
        Wire.setClock(400000);
        Wire.setTimeout(100);
        if (expander.begin(Wire, t5s3_epd::kPca9535Address) &&
            expander.configureProbeDefaults()) {
            expander_ready = true;
            break;
        }
        Serial.printf("[GT6972P_PEN] PCA9535 attempt %u/%u failed\n",
                      static_cast<unsigned>(attempt + 1),
                      static_cast<unsigned>(kExpanderInitAttempts));
    }
    if (!expander_ready) {
        Serial.println("[GT6972P_PEN] PCA9535 initialization failed");
        return false;
    }
    if (!epd_video_init(expander)) {
        Serial.println("[GT6972P_PEN] video scanner initialization failed");
        return false;
    }

    surface.setColorDepth(1);
    surface.setPsram(true);
    if (surface.createSprite(t5s3_epd::kActiveWidth,
                             t5s3_epd::kActiveHeight) == nullptr) {
        Serial.println("[GT6972P_PEN] drawing surface allocation failed");
        return false;
    }
    surface.setRotation(kSurfaceRotation);
    surface.setTextWrap(false);
    surface.fillScreen(TFT_WHITE);

    if (surface.bufferLength() != epd_video_get_backbuffer_size()) {
        Serial.printf("[GT6972P_PEN] framebuffer size mismatch: %lu/%lu\n",
                      static_cast<unsigned long>(surface.bufferLength()),
                      static_cast<unsigned long>(
                          epd_video_get_backbuffer_size()));
        return false;
    }
    if (!epd_video_power_on() || !epd_video_start()) {
        Serial.println("[GT6972P_PEN] video scanner start failed");
        return false;
    }

    video_ready = true;
    Serial.printf("[GT6972P_PEN] continuous EPD scan ready: %dx%d, "
                  "%d FPS\n",
                  surface.width(), surface.height(), TARGET_FPS);
    return true;
}

lgfx::LovyanGFX &display()
{
    return surface;
}

bool present(int x, int y, int width, int height)
{
    if (!video_ready || width <= 0 || height <= 0) {
        return false;
    }

    const int left = std::max(0, x);
    const int right = std::min(surface.width(), x + width);
    const int top = std::max(0, y);
    const int bottom = std::min(surface.height(), y + height);
    if (left >= right || top >= bottom) {
        return false;
    }

    uint8_t *backbuffer = epd_video_get_backbuffer();
    if (backbuffer == nullptr) {
        return false;
    }
    memcpy(backbuffer, surface.getBuffer(), epd_video_get_backbuffer_size());

    // Rotation 3 maps logical X to the native panel's reversed Y axis.
    const uint16_t dirty_y = static_cast<uint16_t>(
        t5s3_epd::kActiveHeight - right);
    const uint16_t dirty_height = static_cast<uint16_t>(right - left);
    epd_video_flip(dirty_y, dirty_height);
    return true;
}

uint32_t vsyncCount()
{
    return epd_video_get_vsync_count();
}

} // namespace t5epd
