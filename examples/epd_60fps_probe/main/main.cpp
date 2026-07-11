#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "epd_video.h"
#include "pca9535_min.h"
#include "t5s3_epd_pins.h"

namespace {

constexpr char kTag[] = "epd_probe";
constexpr uint16_t kRowPitch = t5s3_epd::kActiveWidth / 8U;
constexpr int kCheckerCell = 24;
constexpr int kProbeWidth = 96;
constexpr int kProbeHeight = 96;

Pca9535Min g_expander;
uint8_t *g_background = nullptr;

void put_pixel(uint8_t *buffer, int x, int y, bool white)
{
    if (x < 0 || y < 0 || x >= t5s3_epd::kActiveWidth ||
        y >= t5s3_epd::kActiveHeight) {
        return;
    }
    const size_t index = static_cast<size_t>(y) * kRowPitch + (x >> 3);
    const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7));
    if (white) {
        buffer[index] |= mask;
    } else {
        buffer[index] &= static_cast<uint8_t>(~mask);
    }
}

void fill_rect(uint8_t *buffer, int x, int y, int width, int height, bool white)
{
    for (int py = y; py < y + height; ++py) {
        for (int px = x; px < x + width; ++px) {
            put_pixel(buffer, px, py, white);
        }
    }
}

void frame_rect(uint8_t *buffer, int x, int y, int width, int height,
                int thickness, bool white)
{
    fill_rect(buffer, x, y, width, thickness, white);
    fill_rect(buffer, x, y + height - thickness, width, thickness, white);
    fill_rect(buffer, x, y, thickness, height, white);
    fill_rect(buffer, x + width - thickness, y, thickness, height, white);
}

void build_background(uint8_t *buffer)
{
    memset(buffer, 0xFF, epd_video_get_backbuffer_size());
    frame_rect(buffer, 0, 0, t5s3_epd::kActiveWidth,
               t5s3_epd::kActiveHeight, 3, false);

    const int panel_x = 18;
    const int panel_y = 18;
    const int panel_width = 142;
    const int panel_height = t5s3_epd::kActiveHeight - 36;
    frame_rect(buffer, panel_x, panel_y, panel_width, panel_height, 2, false);
    for (int y = panel_y + 10; y < panel_y + panel_height - 10;
         y += kCheckerCell) {
        for (int x = panel_x + 10; x < panel_x + panel_width - 10;
             x += kCheckerCell) {
            const bool black = (((x - panel_x) / kCheckerCell +
                                 (y - panel_y) / kCheckerCell) & 1) == 0;
            if (black) {
                fill_rect(buffer, x, y, kCheckerCell / 2, kCheckerCell / 2,
                          false);
            }
        }
    }

    const int guide_x = 188;
    const int guide_y = 32;
    const int guide_width = t5s3_epd::kActiveWidth - guide_x - 20;
    const int guide_height = t5s3_epd::kActiveHeight - 64;
    frame_rect(buffer, guide_x, guide_y, guide_width, guide_height, 2, false);
    for (int i = 1; i < 6; ++i) {
        const int y = guide_y + guide_height * i / 6;
        fill_rect(buffer, guide_x + 8, y, guide_width - 16, 1, false);
    }
    for (int i = 1; i < 4; ++i) {
        const int x = guide_x + guide_width * i / 4;
        fill_rect(buffer, x, guide_y + 8, 1, guide_height - 16, false);
    }
}

void draw_probe(uint8_t *buffer, int x, int y)
{
    frame_rect(buffer, x, y, kProbeWidth, kProbeHeight, 3, false);
    frame_rect(buffer, x + 12, y + 12, kProbeWidth - 24,
               kProbeHeight - 24, 2, true);
    frame_rect(buffer, x + 24, y + 24, kProbeWidth - 48,
               kProbeHeight - 48, 2, false);
    fill_rect(buffer, x + 8, y + kProbeHeight / 2,
              kProbeWidth - 16, 1, false);
    fill_rect(buffer, x + kProbeWidth / 2, y + 8, 1,
              kProbeHeight - 16, false);
}

void compute_probe_position(uint32_t frame, int &x, int &y)
{
    const int horizontal_span = t5s3_epd::kActiveWidth - kProbeWidth - 24;
    const int vertical_span = t5s3_epd::kActiveHeight - kProbeHeight - 24;
    const int horizontal_phase = frame % (horizontal_span * 2);
    const int vertical_phase = (frame / 2U) % (vertical_span * 2);
    x = 12 + (horizontal_phase <= horizontal_span
                   ? horizontal_phase
                   : horizontal_span * 2 - horizontal_phase);
    y = 12 + (vertical_phase <= vertical_span
                   ? vertical_phase
                   : vertical_span * 2 - vertical_phase);
}

void scan_i2c_bus()
{
    char found[96] = {0};
    size_t offset = 0;
    for (uint8_t address = 1; address < 0x7F; ++address) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0 && offset < sizeof(found)) {
            offset += static_cast<size_t>(snprintf(
                found + offset, sizeof(found) - offset, "%s0x%02X",
                offset == 0 ? "" : " ", address));
        }
    }
    ESP_LOGI(kTag, "I2C devices: %s", offset == 0 ? "none" : found);
}

void render_task(void *unused)
{
    (void)unused;
    const uint32_t frame_period_ms = TARGET_FPS > 0 ? 1000U / TARGET_FPS : 0;
    const TickType_t frame_ticks = pdMS_TO_TICKS(frame_period_ms);
    uint64_t log_window_start = esp_timer_get_time();
    uint32_t log_frames = 0;
    int previous_y = 12;

    ESP_LOGI(kTag, "render task core=%d target=%d fps", xPortGetCoreID(),
             TARGET_FPS);
    for (uint32_t frame = 0;; ++frame) {
        const TickType_t start_tick = xTaskGetTickCount();
        uint8_t *backbuffer = epd_video_get_backbuffer();
        memcpy(backbuffer, g_background, epd_video_get_backbuffer_size());

        int x = 0;
        int y = 0;
        compute_probe_position(frame, x, y);
        draw_probe(backbuffer, x, y);

        if (frame == 0) {
            epd_video_flip(0, t5s3_epd::kActiveHeight);
        } else {
            int dirty_top = previous_y < y ? previous_y : y;
            int dirty_bottom = previous_y + kProbeHeight - 1;
            if (y + kProbeHeight - 1 > dirty_bottom) {
                dirty_bottom = y + kProbeHeight - 1;
            }
            dirty_top -= EPD_DIRTY_PADDING;
            dirty_bottom += EPD_DIRTY_PADDING;
            if (dirty_top < 0) {
                dirty_top = 0;
            }
            if (dirty_bottom >= t5s3_epd::kActiveHeight) {
                dirty_bottom = t5s3_epd::kActiveHeight - 1;
            }
            epd_video_flip(static_cast<uint16_t>(dirty_top),
                           static_cast<uint16_t>(dirty_bottom - dirty_top + 1));
        }
        previous_y = y;
        ++log_frames;

        const uint64_t now = esp_timer_get_time();
        if (now - log_window_start >= 1000000ULL) {
            ESP_LOGI(kTag, "render=%lu fps vsync=%lu",
                     static_cast<unsigned long>(log_frames),
                     static_cast<unsigned long>(epd_video_get_vsync_count()));
            log_frames = 0;
            log_window_start = now;
        }

        if (frame_ticks > 0) {
            const TickType_t elapsed = xTaskGetTickCount() - start_tick;
            if (elapsed < frame_ticks) {
                vTaskDelay(frame_ticks - elapsed);
            }
        } else {
            vTaskDelay(1);
        }
    }
}

bool init_probe()
{
    Wire.begin(t5s3_epd::kI2cSda, t5s3_epd::kI2cScl);
    Wire.setClock(400000);
    Wire.setTimeout(100);
    scan_i2c_bus();

    if (!g_expander.begin(Wire, t5s3_epd::kPca9535Address) ||
        !g_expander.configureProbeDefaults()) {
        ESP_LOGE(kTag, "PCA9535 initialization failed at 0x%02X",
                 t5s3_epd::kPca9535Address);
        return false;
    }

    uint8_t config0 = 0;
    uint8_t config1 = 0;
    uint8_t output0 = 0;
    uint8_t output1 = 0;
    if (g_expander.readState(config0, config1, output0, output1)) {
        ESP_LOGI(kTag, "PCA9535 cfg=0x%02X/0x%02X out=0x%02X/0x%02X",
                 config0, config1, output0, output1);
    }

    if (!epd_video_init(g_expander)) {
        return false;
    }
    g_background = static_cast<uint8_t *>(heap_caps_malloc(
        epd_video_get_backbuffer_size(), MALLOC_CAP_8BIT));
    if (g_background == nullptr) {
        ESP_LOGE(kTag, "background allocation failed");
        return false;
    }
    build_background(g_background);

    return epd_video_power_on() && epd_video_start();
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(1200);
    ESP_LOGI(kTag, "startup board=%s psram=%s", t5s3_epd::kBoardName,
             psramFound() ? "yes" : "no");
    ESP_LOGI(kTag, "heap internal=%u spiram=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    if (!init_probe()) {
        ESP_LOGE(kTag, "probe initialization failed");
        return;
    }

    if (xTaskCreatePinnedToCore(render_task, "render_task", 8192, nullptr, 2,
                                nullptr, 0) != pdPASS) {
        ESP_LOGE(kTag, "render task creation failed");
        epd_video_shutdown();
        return;
    }
    ESP_LOGI(kTag, "running active area=%ux%u at (%u,%u)",
             t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight,
             t5s3_epd::kActiveX, t5s3_epd::kActiveY);
}

void loop()
{
    delay(1000);
}
