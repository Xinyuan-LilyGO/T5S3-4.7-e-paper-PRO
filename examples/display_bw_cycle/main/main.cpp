#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include <string.h>

#include "epd_highlevel.h"
#include "epdiy.h"
#include "sdkconfig.h"

#define WAVEFORM EPD_BUILTIN_WAVEFORM

#ifdef CONFIG_IDF_TARGET_ESP32S3
#define DEMO_BOARD epd_board_v7
#endif

namespace
{
constexpr uint8_t kWhite = 0xFF;
constexpr uint8_t kBlack = 0x00;
constexpr int kVcomMv = 1560;
constexpr int kRefreshTemperatureC = 25;
constexpr uint32_t kFrameIntervalMs = 3000;
constexpr uint32_t kFullRefreshEvery = 6;

EpdiyHighlevelState hl;
uint8_t *framebuffer = nullptr;
bool next_frame_black = false;
uint32_t frame_counter = 0;

const char *modeName(EpdDrawMode mode)
{
    switch (mode) {
    case MODE_DU:
        return "MODE_DU";
    case MODE_GC16:
        return "MODE_GC16";
    default:
        return "UNKNOWN";
    }
}

size_t framebufferSize()
{
    return static_cast<size_t>(epd_width() / 2) * static_cast<size_t>(epd_height());
}

void fillFramebuffer(uint8_t color)
{
    memset(framebuffer, color, framebufferSize());
}

void updateScreen(EpdDrawMode mode)
{
    epd_poweron();
    const EpdDrawError err = epd_hl_update_screen(&hl, mode, kRefreshTemperatureC);
    epd_poweroff();

    if (err != EPD_DRAW_SUCCESS) {
        Serial.printf("[display_bw_cycle] refresh failed: 0x%X\n", err);
    }
}

void showFrame(bool black_frame, EpdDrawMode mode, uint32_t frame_index)
{
    fillFramebuffer(black_frame ? kBlack : kWhite);

    Serial.printf("[display_bw_cycle] frame=%lu color=%s mode=%s\n",
                  static_cast<unsigned long>(frame_index),
                  black_frame ? "black" : "white",
                  modeName(mode));

    updateScreen(mode);
}

void renderNextFrame()
{
    const uint32_t next_index = frame_counter + 1;
    const EpdDrawMode mode = (next_index % kFullRefreshEvery == 0) ? MODE_GC16 : MODE_DU;

    showFrame(next_frame_black, mode, next_index);
    next_frame_black = !next_frame_black;
    frame_counter = next_index;
}
} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("[display_bw_cycle] start");

    Wire.begin(39, 40);

    epd_init(&DEMO_BOARD, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(kVcomMv);
    hl = epd_hl_init(WAVEFORM);
    framebuffer = epd_hl_get_framebuffer(&hl);

    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);

    Serial.printf("[display_bw_cycle] size=%dx%d\n",
                  epd_rotated_display_width(),
                  epd_rotated_display_height());

    epd_poweron();
    epd_clear();
    epd_poweroff();

    showFrame(true, MODE_GC16, 1);
    next_frame_black = false;
    frame_counter = 1;
}

void loop()
{
    renderNextFrame();
    delay(kFrameIntervalMs);
}
