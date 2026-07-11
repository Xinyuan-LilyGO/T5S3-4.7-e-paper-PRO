#include "epd_video.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "pca9535_min.h"
#include "t5s3_epd_pins.h"

#if defined(__GNUC__)
#pragma GCC optimize("O3")
#endif

namespace {

constexpr char kTag[] = "epd_video";
constexpr size_t kSourceRowBytes = t5s3_epd::kActiveWidth / 8U;
constexpr size_t kStateRowBytes = t5s3_epd::kActiveWidth / 2U;
constexpr size_t kBackbufferBytes =
    static_cast<size_t>(t5s3_epd::kActiveHeight) * kSourceRowBytes;
constexpr size_t kStateBufferBytes =
    static_cast<size_t>(t5s3_epd::kActiveHeight) * kStateRowBytes;
constexpr size_t kPanelRowBytes = t5s3_epd::kPanelWidth / 4U;
constexpr size_t kActiveLeftPadBytes = t5s3_epd::kActiveX / 4U;
constexpr size_t kActiveRowBytes = t5s3_epd::kActiveWidth / 4U;
constexpr size_t kActiveRightPadBytes =
    kPanelRowBytes - kActiveLeftPadBytes - kActiveRowBytes;
constexpr size_t kDmaRowBytes = kPanelRowBytes;
constexpr uint8_t kResetCounterMask[4] = {0xFC, 0xE0, 0x1C, 0x00};

constexpr gpio_num_t kDummyDcGpio = GPIO_NUM_0;
constexpr gpio_num_t kWrGpio = GPIO_NUM_4;
constexpr gpio_num_t kCsGpio = GPIO_NUM_41;
constexpr gpio_num_t kLeGpio = GPIO_NUM_42;
constexpr gpio_num_t kStvGpio = GPIO_NUM_45;
constexpr gpio_num_t kCkvGpio = GPIO_NUM_48;
constexpr gpio_num_t kDataGpios[8] = {
    GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_15,
    GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_8,
};

constexpr uint8_t kTpsRegEnable = 0x01;
constexpr uint8_t kTpsRegVcom = 0x03;
constexpr uint8_t kTpsRegPowerGood = 0x0F;
constexpr uint8_t kTpsEnableAllRails = 0x3F;
constexpr uint8_t kTpsPowerGoodMask = 0xFA;

constexpr uint8_t kPanelPowerMask =
    (1U << t5s3_epd::kPcaBitEpdOe) |
    (1U << t5s3_epd::kPcaBitEpdMode) |
    (1U << t5s3_epd::kPcaBitTpsPwrup) |
    (1U << t5s3_epd::kPcaBitVcomCtrl) |
    (1U << t5s3_epd::kPcaBitTpsWakeup);

static_assert(t5s3_epd::kActiveWidth % 8U == 0U,
              "active width must be byte aligned");
static_assert(t5s3_epd::kPanelWidth % 4U == 0U,
              "panel width must be 2bpp packed");
static_assert(kActiveLeftPadBytes + kActiveRowBytes + kActiveRightPadBytes ==
                  kPanelRowBytes,
              "active area padding must cover one panel line");

Pca9535Min *g_expander = nullptr;
TaskHandle_t g_scan_task = nullptr;
TaskHandle_t g_flip_waiter = nullptr;
esp_lcd_i80_bus_handle_t g_i80_bus = nullptr;
esp_lcd_panel_io_handle_t g_panel_io = nullptr;
portMUX_TYPE g_buffer_lock = portMUX_INITIALIZER_UNLOCKED;

uint8_t *g_buffers[2] = {nullptr, nullptr};
uint8_t *g_state_buffer = nullptr;
uint8_t *g_dma_buf[2] = {nullptr, nullptr};
uint8_t *g_blank_row = nullptr;
uint8_t g_row_active[t5s3_epd::kActiveHeight] = {0};

volatile bool g_running = false;
volatile bool g_dma_done = true;
volatile bool g_flip_req = false;
volatile uint8_t g_front_index = 0;
volatile uint32_t g_vsync_count = 0;
uint16_t g_pending_dirty_start = 0;
uint16_t g_pending_dirty_end = t5s3_epd::kActiveHeight - 1;

bool dma_done_callback(esp_lcd_panel_io_handle_t panel_io,
                       esp_lcd_panel_io_event_data_t *edata,
                       void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    g_dma_done = true;
    return false;
}

void free_buffer(uint8_t *&buffer)
{
    if (buffer != nullptr) {
        heap_caps_free(buffer);
        buffer = nullptr;
    }
}

void release_allocations()
{
    free_buffer(g_buffers[0]);
    free_buffer(g_buffers[1]);
    free_buffer(g_state_buffer);
    free_buffer(g_dma_buf[0]);
    free_buffer(g_dma_buf[1]);
    free_buffer(g_blank_row);
}

bool alloc_video_buffers()
{
    for (uint8_t i = 0; i < 2; ++i) {
        g_buffers[i] = static_cast<uint8_t *>(heap_caps_malloc(
            kBackbufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (g_buffers[i] == nullptr) {
            g_buffers[i] = static_cast<uint8_t *>(
                heap_caps_malloc(kBackbufferBytes, MALLOC_CAP_8BIT));
        }
        if (g_buffers[i] == nullptr) {
            ESP_LOGE(kTag, "framebuffer allocation failed: %u", i);
            return false;
        }
        memset(g_buffers[i], 0xFF, kBackbufferBytes);
    }

    g_state_buffer = static_cast<uint8_t *>(heap_caps_malloc(
        kStateBufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (g_state_buffer == nullptr) {
        g_state_buffer = static_cast<uint8_t *>(
            heap_caps_malloc(kStateBufferBytes, MALLOC_CAP_8BIT));
    }
    if (g_state_buffer == nullptr) {
        ESP_LOGE(kTag, "state buffer allocation failed");
        return false;
    }
    memset(g_state_buffer, 0x00, kStateBufferBytes);

    for (uint8_t i = 0; i < 2; ++i) {
        g_dma_buf[i] = static_cast<uint8_t *>(heap_caps_malloc(
            kDmaRowBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
        if (g_dma_buf[i] == nullptr) {
            ESP_LOGE(kTag, "DMA row allocation failed: %u", i);
            return false;
        }
        memset(g_dma_buf[i], 0x00, kDmaRowBytes);
    }

    g_blank_row = static_cast<uint8_t *>(heap_caps_malloc(
        kDmaRowBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (g_blank_row == nullptr) {
        ESP_LOGE(kTag, "blank row allocation failed");
        return false;
    }
    memset(g_blank_row, 0x00, kDmaRowBytes);
    return true;
}

bool write_i2c_bytes(uint8_t address, const uint8_t *data, size_t size)
{
    Wire.beginTransmission(address);
    if (Wire.write(data, size) != size) {
        return false;
    }
    return Wire.endTransmission() == 0;
}

bool read_i2c_register(uint8_t address, uint8_t reg, uint8_t *data, size_t size)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 ||
        Wire.requestFrom(static_cast<int>(address), static_cast<int>(size)) !=
            static_cast<int>(size)) {
        return false;
    }
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>(Wire.read());
    }
    return true;
}

bool wait_panel_power_good(uint32_t timeout_ms)
{
    const uint32_t deadline = millis() + timeout_ms;
    while (true) {
        bool power_good = false;
        if (g_expander->readPowerGood(power_good) && power_good) {
            return true;
        }
        if (static_cast<int32_t>(millis() - deadline) >= 0) {
            return false;
        }
        delay(1);
    }
}

bool tps_enable_outputs()
{
    const uint8_t command[] = {kTpsRegEnable, kTpsEnableAllRails};
    return write_i2c_bytes(t5s3_epd::kTps65185Address, command,
                           sizeof(command));
}

bool tps_set_vcom_mv(int vcom_mv)
{
    if (vcom_mv >= 0) {
        return false;
    }
    const uint16_t raw = static_cast<uint16_t>((-vcom_mv) / 10);
    const uint8_t command[] = {
        kTpsRegVcom,
        static_cast<uint8_t>(raw & 0xFFU),
        static_cast<uint8_t>((raw >> 8) & 0xFFU),
    };
    return write_i2c_bytes(t5s3_epd::kTps65185Address, command,
                           sizeof(command));
}

bool wait_tps_power_good(uint32_t timeout_ms)
{
    const uint32_t deadline = millis() + timeout_ms;
    while (true) {
        uint8_t value = 0;
        if (read_i2c_register(t5s3_epd::kTps65185Address, kTpsRegPowerGood,
                              &value, 1) &&
            (value & kTpsPowerGoodMask) == kTpsPowerGoodMask) {
            return true;
        }
        if (static_cast<int32_t>(millis() - deadline) >= 0) {
            return false;
        }
        delay(1);
    }
}

void sanitize_dirty_region(uint16_t dirty_y, uint16_t dirty_height,
                           uint16_t &row_start, uint16_t &row_end)
{
    row_start = 0;
    row_end = t5s3_epd::kActiveHeight - 1;
    if (dirty_height == 0) {
        return;
    }

    row_start = dirty_y < t5s3_epd::kActiveHeight
                    ? dirty_y
                    : t5s3_epd::kActiveHeight - 1;
    const uint32_t last_row = static_cast<uint32_t>(dirty_y) + dirty_height - 1U;
    row_end = last_row >= t5s3_epd::kActiveHeight
                  ? t5s3_epd::kActiveHeight - 1
                  : static_cast<uint16_t>(last_row);
}

void mark_rows_active(uint16_t row_start, uint16_t row_end)
{
    for (uint16_t row = row_start; row <= row_end; ++row) {
        g_row_active[row] = 1U;
    }
}

void configure_idle_levels()
{
    gpio_set_level(kLeGpio, 0);
    gpio_set_level(kStvGpio, 1);
    gpio_set_level(kCkvGpio, 1);
    gpio_set_level(kCsGpio, 1);
}

bool init_control_gpios()
{
    gpio_config_t config = {};
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    config.pin_bit_mask = (1ULL << kCsGpio) | (1ULL << kLeGpio) |
                          (1ULL << kStvGpio) | (1ULL << kCkvGpio);
    const esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "control GPIO setup failed: %s", esp_err_to_name(err));
        return false;
    }
    configure_idle_levels();
    return true;
}

bool init_panel_bus()
{
    if (g_panel_io != nullptr) {
        return true;
    }
    if (!init_control_gpios()) {
        return false;
    }

    esp_lcd_i80_bus_config_t bus_config = {};
    bus_config.dc_gpio_num = static_cast<int>(kDummyDcGpio);
    bus_config.wr_gpio_num = static_cast<int>(kWrGpio);
    bus_config.clk_src = LCD_CLK_SRC_PLL160M;
    bus_config.bus_width = 8;
    bus_config.max_transfer_bytes = kDmaRowBytes;
    for (size_t i = 0; i < 8; ++i) {
        bus_config.data_gpio_nums[i] = kDataGpios[i];
    }

    esp_err_t err = esp_lcd_new_i80_bus(&bus_config, &g_i80_bus);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "I80 bus setup failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_lcd_panel_io_i80_config_t panel_config = {};
    panel_config.cs_gpio_num = static_cast<int>(kCsGpio);
    panel_config.pclk_hz = EPD_BUS_HZ;
    panel_config.trans_queue_depth = 4;
    panel_config.on_color_trans_done = dma_done_callback;
    panel_config.lcd_cmd_bits = 8;
    panel_config.lcd_param_bits = 8;
    panel_config.dc_levels.dc_idle_level = 0;
    panel_config.dc_levels.dc_cmd_level = 0;
    panel_config.dc_levels.dc_dummy_level = 0;
    panel_config.dc_levels.dc_data_level = 1;
    panel_config.flags.cs_active_high = 0;
    panel_config.flags.reverse_color_bits = 0;
    panel_config.flags.swap_color_bytes = 0;
    panel_config.flags.pclk_active_neg = 0;
    panel_config.flags.pclk_idle_low = 0;

    err = esp_lcd_new_panel_io_i80(g_i80_bus, &panel_config, &g_panel_io);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "I80 panel IO setup failed: %s", esp_err_to_name(err));
        return false;
    }
    g_dma_done = true;
    return true;
}

void row_control_start()
{
    gpio_set_level(kCkvGpio, 1);
    delayMicroseconds(7);
    gpio_set_level(kStvGpio, 0);
    delayMicroseconds(10);
    gpio_set_level(kCkvGpio, 0);
    gpio_set_level(kCkvGpio, 1);
    delayMicroseconds(8);
    gpio_set_level(kStvGpio, 1);
    delayMicroseconds(10);
    gpio_set_level(kCkvGpio, 0);
    for (uint8_t i = 0; i < 3; ++i) {
        gpio_set_level(kCkvGpio, 1);
        delayMicroseconds(18);
        gpio_set_level(kCkvGpio, 0);
    }
    gpio_set_level(kCkvGpio, 1);
}

void row_control_step()
{
    gpio_set_level(kCkvGpio, 0);
    gpio_set_level(kLeGpio, 1);
    gpio_set_level(kLeGpio, 0);
}

void wait_for_dma()
{
    while (!g_dma_done) {
        delayMicroseconds(1);
    }
}

bool send_row(uint8_t *data, bool first_row)
{
    wait_for_dma();
    if (!first_row) {
        row_control_step();
    }
    g_dma_done = false;
    gpio_set_level(kCkvGpio, 1);
    const esp_err_t err = esp_lcd_panel_io_tx_color(
        g_panel_io, -1, data, kDmaRowBytes);
    if (err != ESP_OK) {
        g_dma_done = true;
        ESP_LOGE(kTag, "row transmit failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

// Each state byte describes two source pixels. The low bits hold the desired
// direction and the upper bits are short per-pixel pulse counters.
bool build_active_row(const uint8_t *frame, uint16_t row, uint8_t *dst)
{
    memset(dst, 0x00, kDmaRowBytes);
    uint8_t *write_ptr = dst + kActiveLeftPadBytes;
    const uint8_t *read_ptr = frame + static_cast<size_t>(row) * kSourceRowBytes;
    uint8_t *state_ptr =
        g_state_buffer + static_cast<size_t>(row) * kStateRowBytes;
    bool needs_more_drive = false;

    for (size_t source_byte = 0; source_byte < kSourceRowBytes; ++source_byte) {
        uint8_t incoming_pixels = *read_ptr++;
        for (uint8_t output_byte = 0; output_byte < 2; ++output_byte) {
            uint8_t packed_drive = 0;
            for (uint8_t pair = 0; pair < 2; ++pair) {
                uint8_t state = *state_ptr;
                const uint8_t driving_dir = incoming_pixels >> 6;
                const uint8_t pixel_diff = static_cast<uint8_t>(
                    (state ^ driving_dir) & 0x03U);

                state &= kResetCounterMask[pixel_diff];
                state |= driving_dir;

                packed_drive <<= 4;
                packed_drive |= static_cast<uint8_t>(
                    (state & 0x80U) ? 0x00U
                                    : ((driving_dir & 0x02U) ? 0x04U : 0x08U));
                packed_drive |= static_cast<uint8_t>(
                    (state & 0x10U) ? 0x00U
                                    : ((driving_dir & 0x01U) ? 0x01U : 0x02U));

                const uint8_t counter_increment = static_cast<uint8_t>(
                    ((~state) >> 2) & 0x24U);
                state = static_cast<uint8_t>(state + counter_increment);
                needs_more_drive = needs_more_drive ||
                                    ((state & 0x90U) != 0x90U);
                *state_ptr++ = state;
                incoming_pixels <<= 2;
            }
            *write_ptr++ = packed_drive;
        }
    }

    g_row_active[row] = needs_more_drive ? 1U : 0U;
    return needs_more_drive;
}

uint8_t *prepare_scan_row(const uint8_t *frame, uint16_t scan_row,
                          uint8_t dma_index, uint32_t &processed_rows,
                          uint32_t &continuing_rows)
{
    if (scan_row < EPD_VIDEO_TOP_DUMMY_LINES) {
        return g_blank_row;
    }
    const uint16_t active_row =
        static_cast<uint16_t>(scan_row - EPD_VIDEO_TOP_DUMMY_LINES);
    if (active_row >= t5s3_epd::kActiveHeight ||
        g_row_active[active_row] == 0U) {
        return g_blank_row;
    }

    ++processed_rows;
    if (build_active_row(frame, active_row, g_dma_buf[dma_index])) {
        ++continuing_rows;
    }
    return g_dma_buf[dma_index];
}

void sleep_to_target_frame(int64_t frame_start_us)
{
    if (TARGET_FPS <= 0) {
        return;
    }
    const int64_t target_us = 1000000LL / TARGET_FPS;
    while (true) {
        const int64_t remaining_us =
            target_us - (esp_timer_get_time() - frame_start_us);
        if (remaining_us <= 0) {
            return;
        }
        if (remaining_us > 2000) {
            vTaskDelay(1);
        } else {
            delayMicroseconds(static_cast<unsigned int>(remaining_us));
            return;
        }
    }
}

void scan_task(void *unused)
{
    (void)unused;
    const uint16_t total_scan_rows = EPD_VIDEO_TOP_DUMMY_LINES +
                                     t5s3_epd::kActiveHeight +
                                     EPD_VIDEO_BOTTOM_DUMMY_LINES;
    ESP_LOGI(kTag, "scan task core=%d target=%d fps bus=%lu Hz",
             xPortGetCoreID(), TARGET_FPS,
             static_cast<unsigned long>(EPD_BUS_HZ));

    uint64_t log_window_start = esp_timer_get_time();
    uint64_t log_scan_us = 0;
    uint32_t log_frames = 0;
    while (g_running) {
        const int64_t frame_start_us = esp_timer_get_time();
        TaskHandle_t waiter_to_notify = nullptr;
        uint8_t front_index = 0;
        uint16_t dirty_start = 0;
        uint16_t dirty_end = 0;
        bool applied_flip = false;

        portENTER_CRITICAL(&g_buffer_lock);
        ++g_vsync_count;
        if (g_flip_req) {
            g_front_index ^= 1U;
            g_flip_req = false;
            dirty_start = g_pending_dirty_start;
            dirty_end = g_pending_dirty_end;
            applied_flip = true;
            waiter_to_notify = g_flip_waiter;
            g_flip_waiter = nullptr;
        }
        front_index = g_front_index;
        portEXIT_CRITICAL(&g_buffer_lock);

        if (applied_flip) {
            mark_rows_active(dirty_start, dirty_end);
            if (waiter_to_notify != nullptr) {
                xTaskNotifyGive(waiter_to_notify);
            }
        }

        const uint8_t *frame = g_buffers[front_index];
        uint32_t processed_rows = 0;
        uint32_t continuing_rows = 0;
        row_control_start();

        uint8_t dma_index = 0;
        uint8_t *row_ptr = prepare_scan_row(frame, 0, dma_index,
                                            processed_rows, continuing_rows);
        bool row_uses_dma = row_ptr != g_blank_row;
        if (!send_row(row_ptr, true)) {
            g_running = false;
            break;
        }

        for (uint16_t scan_row = 1; scan_row < total_scan_rows; ++scan_row) {
            const uint8_t next_dma_index = row_uses_dma
                                                ? static_cast<uint8_t>(dma_index ^ 1U)
                                                : dma_index;
            uint8_t *next_row = prepare_scan_row(
                frame, scan_row, next_dma_index, processed_rows, continuing_rows);
            const bool next_uses_dma = next_row != g_blank_row;
            if (!send_row(next_row, false)) {
                g_running = false;
                break;
            }
            dma_index = next_dma_index;
            row_uses_dma = next_uses_dma;
        }
        if (!g_running || !send_row(g_blank_row, false)) {
            g_running = false;
            break;
        }
        wait_for_dma();

        ++log_frames;
        log_scan_us += static_cast<uint64_t>(esp_timer_get_time() - frame_start_us);
        const uint64_t now = esp_timer_get_time();
        if (now - log_window_start >= 1000000ULL) {
            const uint32_t avg_scan_ms = log_frames == 0
                                              ? 0
                                              : static_cast<uint32_t>(
                                                    (log_scan_us / log_frames) / 1000ULL);
            ESP_LOGI(kTag,
                     "scan=%lu fps avg=%lu ms rows=%lu continuing=%lu vsync=%lu",
                     static_cast<unsigned long>(log_frames),
                     static_cast<unsigned long>(avg_scan_ms),
                     static_cast<unsigned long>(processed_rows),
                     static_cast<unsigned long>(continuing_rows),
                     static_cast<unsigned long>(g_vsync_count));
            log_frames = 0;
            log_scan_us = 0;
            log_window_start = now;
        }
        sleep_to_target_frame(frame_start_us);
    }

    g_scan_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

bool epd_video_init(Pca9535Min &expander)
{
    if (g_expander != nullptr) {
        return true;
    }
    g_expander = &expander;
    if (!alloc_video_buffers() || !init_panel_bus()) {
        release_allocations();
        return false;
    }
    return true;
}

bool epd_video_power_on()
{
    if (g_expander == nullptr) {
        return false;
    }
    bool before_power_good = false;
    if (g_expander->readPowerGood(before_power_good)) {
        ESP_LOGI(kTag, "TPS power good before enable: %s",
                 before_power_good ? "high" : "low");
    }
    if (!g_expander->setOutputMask(1, kPanelPowerMask, true) ||
        !wait_panel_power_good(400) || !tps_enable_outputs() ||
        !tps_set_vcom_mv(EPD_VCOM_MV) || !wait_tps_power_good(400)) {
        ESP_LOGE(kTag, "panel power-on sequence failed");
        return false;
    }

    memset(g_buffers[0], 0xFF, kBackbufferBytes);
    memset(g_buffers[1], 0xFF, kBackbufferBytes);
    memset(g_state_buffer, 0x00, kStateBufferBytes);
    memset(g_dma_buf[0], 0x00, kDmaRowBytes);
    memset(g_dma_buf[1], 0x00, kDmaRowBytes);
    memset(g_blank_row, 0x00, kDmaRowBytes);
    memset(g_row_active, 0x00, sizeof(g_row_active));
    configure_idle_levels();
    ESP_LOGI(kTag, "panel power on, VCOM=%d mV", EPD_VCOM_MV);
    return true;
}

bool epd_video_start()
{
    if (g_running) {
        return true;
    }
    portENTER_CRITICAL(&g_buffer_lock);
    g_front_index = 0;
    g_vsync_count = 0;
    g_flip_req = false;
    g_flip_waiter = nullptr;
    g_pending_dirty_start = 0;
    g_pending_dirty_end = t5s3_epd::kActiveHeight - 1;
    portEXIT_CRITICAL(&g_buffer_lock);

    g_dma_done = true;
    g_running = true;
    const BaseType_t result = xTaskCreatePinnedToCore(
        scan_task, "epd_scan", 8192, nullptr, 3, &g_scan_task, 1);
    if (result != pdPASS) {
        g_running = false;
        ESP_LOGE(kTag, "scan task creation failed");
        return false;
    }
    return true;
}

uint8_t *epd_video_get_backbuffer()
{
    uint8_t back_index = 0;
    portENTER_CRITICAL(&g_buffer_lock);
    back_index = g_front_index ^ 1U;
    portEXIT_CRITICAL(&g_buffer_lock);
    return g_buffers[back_index];
}

size_t epd_video_get_backbuffer_size()
{
    return kBackbufferBytes;
}

void epd_video_flip(uint16_t dirty_y, uint16_t dirty_height)
{
    if (!g_running) {
        return;
    }
    uint16_t row_start = 0;
    uint16_t row_end = 0;
    sanitize_dirty_region(dirty_y, dirty_height, row_start, row_end);
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    (void)ulTaskNotifyTake(pdTRUE, 0);

    portENTER_CRITICAL(&g_buffer_lock);
    g_pending_dirty_start = row_start;
    g_pending_dirty_end = row_end;
    g_flip_waiter = self;
    g_flip_req = true;
    portEXIT_CRITICAL(&g_buffer_lock);
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

uint32_t epd_video_get_vsync_count()
{
    return g_vsync_count;
}

void epd_video_shutdown()
{
    g_running = false;
    TaskHandle_t waiter_to_notify = nullptr;
    portENTER_CRITICAL(&g_buffer_lock);
    g_flip_req = false;
    waiter_to_notify = g_flip_waiter;
    g_flip_waiter = nullptr;
    portEXIT_CRITICAL(&g_buffer_lock);
    if (waiter_to_notify != nullptr) {
        xTaskNotifyGive(waiter_to_notify);
    }
    for (uint8_t i = 0; i < 20 && g_scan_task != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    wait_for_dma();
    configure_idle_levels();
    if (g_expander != nullptr) {
        g_expander->safeShutdownOutputs();
    }
}
