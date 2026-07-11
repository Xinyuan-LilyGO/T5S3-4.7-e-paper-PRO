#include <Arduino.h>
#include <M5GFX.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "TouchDrvGT911.hpp"
#include "driver/gpio.h"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"
#include "utilities.h"

using lgfx::epd_mode_t;

namespace {

constexpr int kPanelWidth = 960;
constexpr int kPanelHeight = 540;
constexpr int kEpdBusSpeedHz = 20000000;
constexpr int kVcomMillivolts = 1560;

constexpr int kI2cSda = BOARD_SDA;
constexpr int kI2cScl = BOARD_SCL;
constexpr uint8_t kPca9535Address = 0x20;
constexpr uint8_t kTps651851Address = 0x68;

constexpr uint8_t kTpsRegEnable = 0x01;
constexpr uint8_t kTpsRegVcom = 0x03;
constexpr uint8_t kTpsRegPowerGood = 0x0F;
constexpr uint8_t kTpsEnableAllRails = 0x3F;
constexpr uint8_t kTpsPowerGoodMask = 0xFA;
constexpr uint8_t kTpsPowerGoodExpected = 0xFA;

constexpr uint8_t kPcaInputPort0 = 0x00;
constexpr uint8_t kPcaOutputPort0 = 0x02;
constexpr uint8_t kPcaConfigPort0 = 0x06;

constexpr uint8_t kPcaPinLoraEnable = 0;
constexpr uint8_t kPcaPinEpdOe = 8;
constexpr uint8_t kPcaPinEpdMode = 9;
constexpr uint8_t kPcaPinTpsPwrUp = 11;
constexpr uint8_t kPcaPinVcomCtrl = 12;
constexpr uint8_t kPcaPinTpsWakeUp = 13;
constexpr uint8_t kPcaPinTpsPowerGood = 14;

constexpr gpio_num_t kPinTouchInt = GPIO_NUM_3;
constexpr gpio_num_t kPinEpdCkh = GPIO_NUM_4;
constexpr gpio_num_t kPinEpdD0 = GPIO_NUM_5;
constexpr gpio_num_t kPinEpdD1 = GPIO_NUM_6;
constexpr gpio_num_t kPinEpdD2 = GPIO_NUM_7;
constexpr gpio_num_t kPinEpdD7 = GPIO_NUM_8;
constexpr gpio_num_t kPinTouchRst = GPIO_NUM_9;
constexpr gpio_num_t kPinBacklight = GPIO_NUM_11;
constexpr gpio_num_t kPinEpdD3 = GPIO_NUM_15;
constexpr gpio_num_t kPinEpdD4 = GPIO_NUM_16;
constexpr gpio_num_t kPinEpdD5 = GPIO_NUM_17;
constexpr gpio_num_t kPinEpdD6 = GPIO_NUM_18;
// Bus_EPD requires valid GPIOs for the dummy DC / OE slots.
// GPIO43 only feeds GPS_RX on this board, so it is safe as a placeholder
// while LoRa is active. Avoid GPIO1 here because it is LoRa reset.
constexpr gpio_num_t kPinDummyBus = GPIO_NUM_43;
constexpr gpio_num_t kPinEpdSth = GPIO_NUM_41;
constexpr gpio_num_t kPinEpdLe = GPIO_NUM_42;
constexpr gpio_num_t kPinEpdStv = GPIO_NUM_45;
constexpr gpio_num_t kPinEpdCkv = GPIO_NUM_48;

constexpr uint8_t kPanelOffsetRotation = 3;
constexpr int kPowerGoodTimeoutMs = 400;

constexpr uint32_t kTapDebounceMs = 180;
constexpr uint32_t kAutoTxIntervalMs = 1000;
constexpr uint32_t kPassiveUiRefreshIntervalMs = 5000;
constexpr uint32_t kTouchI2cSpeedHz = 100000;
constexpr uint32_t kTouchResetHoldMs = 6;
constexpr uint32_t kTouchBootDelayMs = 55;
constexpr uint32_t kTouchRetryDelayMs = 80;
constexpr uint8_t kTouchInitAttempts = 4;
constexpr uint8_t kMaxTouchPoints = 2;

constexpr float kTcxoVoltage = 3.0f;
constexpr uint8_t kCurrentLimitMilliamp = 140;
constexpr int8_t kTxPowerStepDbm = 5;

constexpr float kFreqOptionsMhz[] = {868.0f, 915.0f};
constexpr float kBandwidthOptionsKhz[] = {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f};

enum class ParamId : uint8_t {
    Freq = 0,
    Bw,
    Sf,
    Cr,
    Sw,
    Tp,
    Pl,
    Crc,
    Iq,
    Cw,
    Count,
};

enum class ButtonId : uint8_t {
    Minus = 0,
    Plus,
    Rx,
    Tx,
    Count,
};

enum class RadioRunMode : uint8_t {
    Standby = 0,
    Receive,
    TxAuto,
};

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

struct UiLayout {
    Rect header;
    Rect detail;
    Rect param_list;
    Rect param_list_title;
    Rect param_rows[static_cast<int>(ParamId::Count)];
    Rect buttons[static_cast<int>(ButtonId::Count)];
};

struct TouchTapTracker {
    bool down = false;
    int16_t start_x = 0;
    int16_t start_y = 0;
    int16_t last_x = 0;
    int16_t last_y = 0;
    uint32_t down_ms = 0;
    uint32_t last_tap_ms = 0;
};

struct LoRaSettings {
    uint8_t freq_index = 1;
    uint8_t bw_index = 9;
    uint8_t spreading_factor = 5;
    uint8_t coding_rate = 6;
    uint8_t sync_word = 0xAB;
    int8_t tx_power_dbm = 7;
    uint16_t preamble_length = 15;
    bool crc_enabled = false;
    bool iq_inverted = false;
    bool cw_enabled = false;
};

uint8_t g_pca_output[2] = {0xFF, 0x00};

class LilyGoT5ProEpdBus : public lgfx::Bus_EPD {
public:
    bool init() override
    {
        Wire.begin(kI2cSda, kI2cScl);
        Wire.setClock(400000);

        pinMode(kPinBacklight, OUTPUT);
        digitalWrite(kPinBacklight, LOW);
        pinMode(kPinTouchRst, OUTPUT);
        digitalWrite(kPinTouchRst, HIGH);

        if (!pca9535Init()) {
            Serial.println("[LORA_UI] PCA9535 init failed");
            return false;
        }

        const bool ok = lgfx::Bus_EPD::init();
        if (!ok) {
            Serial.println("[LORA_UI] Bus_EPD init failed");
        }
        return ok;
    }

    bool powerControl(bool power_on) override
    {
        if (_pwr_on == power_on) {
            return true;
        }

        wait();
        const bool ok = power_on ? powerOnSequence() : powerOffSequence();
        if (!ok) {
            Serial.printf("[LORA_UI] display power %s failed\n", power_on ? "on" : "off");
            return false;
        }

        _pwr_on = power_on;
        return true;
    }

private:
    static bool i2cWriteBytes(uint8_t address, const uint8_t *data, size_t length)
    {
        Wire.beginTransmission(address);
        const size_t written = Wire.write(data, length);
        return written == length && Wire.endTransmission() == 0;
    }

    static bool i2cWriteRegister(uint8_t address, uint8_t reg, uint8_t value)
    {
        const uint8_t buffer[2] = {reg, value};
        return i2cWriteBytes(address, buffer, sizeof(buffer));
    }

    static bool i2cReadRegister(uint8_t address, uint8_t reg, uint8_t *buffer, size_t length)
    {
        Wire.beginTransmission(address);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) {
            return false;
        }

        const size_t received = Wire.requestFrom(static_cast<int>(address), static_cast<int>(length));
        if (received != length) {
            return false;
        }

        for (size_t i = 0; i < length; ++i) {
            buffer[i] = static_cast<uint8_t>(Wire.read());
        }
        return true;
    }

    static bool pca9535Init()
    {
        g_pca_output[0] = 0xFF;
        g_pca_output[1] = 0x00;

        if (!i2cWriteRegister(kPca9535Address, kPcaOutputPort0 + 0, g_pca_output[0])) {
            return false;
        }
        if (!i2cWriteRegister(kPca9535Address, kPcaOutputPort0 + 1, g_pca_output[1])) {
            return false;
        }
        if (!i2cWriteRegister(kPca9535Address, kPcaConfigPort0 + 0, 0x00)) {
            return false;
        }
        if (!i2cWriteRegister(kPca9535Address, kPcaConfigPort0 + 1, 0xC4)) {
            return false;
        }
        return true;
    }

    static bool pca9535SetLevel(uint8_t pin, bool level)
    {
        const uint8_t port = pin / 8;
        const uint8_t bit = pin & 0x07;

        if (level) {
            g_pca_output[port] |= static_cast<uint8_t>(1U << bit);
        } else {
            g_pca_output[port] &= static_cast<uint8_t>(~(1U << bit));
        }

        return i2cWriteRegister(kPca9535Address, kPcaOutputPort0 + port, g_pca_output[port]);
    }

    static bool pca9535GetLevel(uint8_t pin, bool &level)
    {
        const uint8_t port = pin / 8;
        const uint8_t bit = pin & 0x07;
        uint8_t value = 0;

        if (!i2cReadRegister(kPca9535Address, kPcaInputPort0 + port, &value, 1)) {
            return false;
        }

        level = (value & (1U << bit)) != 0;
        return true;
    }

    static bool tpsWriteRegister(uint8_t reg, const uint8_t *data, size_t length)
    {
        uint8_t buffer[4] = {reg, 0, 0, 0};
        if (length > sizeof(buffer) - 1) {
            return false;
        }

        for (size_t i = 0; i < length; ++i) {
            buffer[i + 1] = data[i];
        }
        return i2cWriteBytes(kTps651851Address, buffer, length + 1);
    }

    static bool tpsWriteRegisterU8(uint8_t reg, uint8_t value)
    {
        return tpsWriteRegister(reg, &value, 1);
    }

    static bool tpsReadRegisterU8(uint8_t reg, uint8_t &value)
    {
        return i2cReadRegister(kTps651851Address, reg, &value, 1);
    }

    bool waitPcaPowerGood()
    {
        for (int i = 0; i < kPowerGoodTimeoutMs; ++i) {
            bool level = false;
            if (!pca9535GetLevel(kPcaPinTpsPowerGood, level)) {
                return false;
            }
            if (level) {
                return true;
            }
            delay(1);
        }
        return false;
    }

    bool waitTpsPowerGood()
    {
        for (int i = 0; i < kPowerGoodTimeoutMs; ++i) {
            uint8_t value = 0;
            if (!tpsReadRegisterU8(kTpsRegPowerGood, value)) {
                return false;
            }
            if ((value & kTpsPowerGoodMask) == kTpsPowerGoodExpected) {
                return true;
            }
            delay(1);
        }
        return false;
    }

    bool powerOnSequence()
    {
        if (!pca9535SetLevel(kPcaPinEpdOe, true)) {
            return false;
        }
        if (!pca9535SetLevel(kPcaPinEpdMode, true)) {
            return false;
        }
        if (!pca9535SetLevel(kPcaPinTpsWakeUp, true)) {
            return false;
        }
        if (!pca9535SetLevel(kPcaPinTpsPwrUp, true)) {
            return false;
        }
        if (!pca9535SetLevel(kPcaPinVcomCtrl, true)) {
            return false;
        }
        delay(1);

        if (!waitPcaPowerGood()) {
            return false;
        }
        if (!tpsWriteRegisterU8(kTpsRegEnable, kTpsEnableAllRails)) {
            return false;
        }

        const uint16_t vcom = static_cast<uint16_t>(kVcomMillivolts / 10);
        const uint8_t vcom_data[2] = {
            static_cast<uint8_t>(vcom & 0xFF),
            static_cast<uint8_t>((vcom >> 8) & 0xFF),
        };
        if (!tpsWriteRegister(kTpsRegVcom, vcom_data, sizeof(vcom_data))) {
            return false;
        }

        return waitTpsPowerGood();
    }

    bool powerOffSequence()
    {
        if (!pca9535SetLevel(kPcaPinEpdOe, false)) {
            return false;
        }
        if (!pca9535SetLevel(kPcaPinEpdMode, false)) {
            return false;
        }
        if (!pca9535SetLevel(kPcaPinTpsPwrUp, false)) {
            return false;
        }
        if (!pca9535SetLevel(kPcaPinVcomCtrl, false)) {
            return false;
        }
        delay(1);
        return pca9535SetLevel(kPcaPinTpsWakeUp, false);
    }
};

class LilyGoT5ProM5GFX : public lgfx::LGFX_Device {
public:
    LilyGoT5ProM5GFX()
    {
        auto bus_cfg = bus_.config();
        bus_cfg.bus_speed = kEpdBusSpeedHz;
        bus_cfg.pin_data[0] = kPinEpdD0;
        bus_cfg.pin_data[1] = kPinEpdD1;
        bus_cfg.pin_data[2] = kPinEpdD2;
        bus_cfg.pin_data[3] = kPinEpdD3;
        bus_cfg.pin_data[4] = kPinEpdD4;
        bus_cfg.pin_data[5] = kPinEpdD5;
        bus_cfg.pin_data[6] = kPinEpdD6;
        bus_cfg.pin_data[7] = kPinEpdD7;
        bus_cfg.pin_pwr = kPinDummyBus;
        bus_cfg.pin_spv = kPinEpdStv;
        bus_cfg.pin_ckv = kPinEpdCkv;
        bus_cfg.pin_sph = kPinEpdSth;
        bus_cfg.pin_oe = kPinDummyBus;
        bus_cfg.pin_le = kPinEpdLe;
        bus_cfg.pin_cl = kPinEpdCkh;
        bus_cfg.bus_width = 8;
        bus_.config(bus_cfg);

        panel_.setBus(&bus_);

        auto panel_cfg = panel_.config();
        panel_cfg.memory_width = kPanelWidth;
        panel_cfg.memory_height = kPanelHeight;
        panel_cfg.panel_width = kPanelWidth;
        panel_cfg.panel_height = kPanelHeight;
        panel_cfg.offset_x = 0;
        panel_cfg.offset_y = 0;
        panel_cfg.offset_rotation = kPanelOffsetRotation;
        panel_cfg.bus_shared = false;
        panel_.config(panel_cfg);

        auto detail = panel_.config_detail();
        detail.line_padding = 0;
        detail.task_priority = 3;
        panel_.config_detail(detail);

        setPanel(&panel_);
    }

private:
    LilyGoT5ProEpdBus bus_;
    lgfx::Panel_EPD panel_;
};

LilyGoT5ProM5GFX display;
TouchDrvGT911 touch;
SX1262 radio = new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY);

UiLayout g_layout;
TouchTapTracker g_touch_tap;
LoRaSettings g_settings;

bool g_display_ready = false;
bool g_touch_ready = false;
bool g_radio_ready = false;
bool g_ui_dirty = true;
bool g_tx_in_progress = false;

ParamId g_selected_param = ParamId::Freq;
RadioRunMode g_run_mode = RadioRunMode::Standby;

uint32_t g_next_refresh_rank = 2;
uint32_t g_fast_refresh_count = 0;
uint32_t g_text_refresh_count = 0;
uint32_t g_next_auto_tx_ms = 0;
uint32_t g_last_passive_ui_ms = 0;
uint32_t g_tx_counter = 0;
uint32_t g_rx_counter = 0;

float g_last_rssi = 0.0f;
float g_last_snr = 0.0f;
float g_last_freq_error = 0.0f;

char g_status_line[96] = "Ready";
char g_last_payload[128] = "-";

constexpr uint8_t kTxPayload[] = {1, 2, 3, 4, 5};
constexpr char kTxPayloadLabel[] = "01 02 03 04 05";

volatile bool g_tx_done_flag = false;
volatile bool g_rx_done_flag = false;

void copyText(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src == nullptr ? "" : src);
}

void setStatus(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_status_line, sizeof(g_status_line), fmt, args);
    va_end(args);
}

int refreshRank(epd_mode_t mode)
{
    switch (mode) {
    case epd_mode_t::epd_fastest:
    case epd_mode_t::epd_fast:
        return 0;
    case epd_mode_t::epd_text:
        return 1;
    case epd_mode_t::epd_quality:
    default:
        return 2;
    }
}

void markUiDirty(epd_mode_t mode)
{
    g_ui_dirty = true;
    g_next_refresh_rank = std::max<uint32_t>(g_next_refresh_rank, refreshRank(mode));
}

void markPassiveUiDirty()
{
    const uint32_t now = millis();
    if ((now - g_last_passive_ui_ms) < kPassiveUiRefreshIntervalMs) {
        return;
    }

    g_last_passive_ui_ms = now;
    markUiDirty(epd_mode_t::epd_text);
}

epd_mode_t refreshModeFromRank(uint32_t rank)
{
    switch (rank) {
    case 0:
        return epd_mode_t::epd_fastest;
    case 1:
        return epd_mode_t::epd_text;
    case 2:
    default:
        return epd_mode_t::epd_quality;
    }
}

epd_mode_t consumeRefreshMode()
{
    const epd_mode_t mode = refreshModeFromRank(g_next_refresh_rank);
    g_next_refresh_rank = 0;
    g_fast_refresh_count = 0;
    g_text_refresh_count = 0;
    return mode;
}

bool i2cWriteBytes(uint8_t address, const uint8_t *data, size_t length)
{
    Wire.beginTransmission(address);
    const size_t written = Wire.write(data, length);
    return written == length && Wire.endTransmission() == 0;
}

bool i2cWriteRegister(uint8_t address, uint8_t reg, uint8_t value)
{
    const uint8_t buffer[2] = {reg, value};
    return i2cWriteBytes(address, buffer, sizeof(buffer));
}

bool pca9535SetLevel(uint8_t pin, bool level)
{
    const uint8_t port = pin / 8;
    const uint8_t bit = pin & 0x07;

    if (level) {
        g_pca_output[port] |= static_cast<uint8_t>(1U << bit);
    } else {
        g_pca_output[port] &= static_cast<uint8_t>(~(1U << bit));
    }

    return i2cWriteRegister(kPca9535Address, kPcaOutputPort0 + port, g_pca_output[port]);
}

float currentFrequencyMhz()
{
    return kFreqOptionsMhz[g_settings.freq_index];
}

float currentBandwidthKhz()
{
    return kBandwidthOptionsKhz[g_settings.bw_index];
}

bool pointInRect(int16_t x, int16_t y, const Rect &rect)
{
    return x >= rect.x && y >= rect.y && x < (rect.x + rect.w) && y < (rect.y + rect.h);
}

const char *runModeLabel(RadioRunMode mode)
{
    switch (mode) {
    case RadioRunMode::Receive:
        return "RX";
    case RadioRunMode::TxAuto:
        return "TX AUTO";
    case RadioRunMode::Standby:
    default:
        return "STANDBY";
    }
}

const char *paramLabel(ParamId param)
{
    switch (param) {
    case ParamId::Freq:
        return "Frequency";
    case ParamId::Bw:
        return "Bandwidth";
    case ParamId::Sf:
        return "Spreading Factor";
    case ParamId::Cr:
        return "Coding Rate";
    case ParamId::Sw:
        return "Sync Word";
    case ParamId::Tp:
        return "TX Power";
    case ParamId::Pl:
        return "Preamble";
    case ParamId::Crc:
        return "CRC";
    case ParamId::Iq:
        return "IQ Invert";
    case ParamId::Cw:
        return "Continuous Wave";
    case ParamId::Count:
    default:
        return "";
    }
}

bool isParamEditable(ParamId param)
{
    return param != ParamId::Sw && param != ParamId::Pl && param != ParamId::Cw;
}

void formatParamRange(ParamId param, char *buffer, size_t buffer_size)
{
    switch (param) {
    case ParamId::Freq:
        snprintf(buffer, buffer_size, "Options: 915.0 MHz and 868.0 MHz");
        break;
    case ParamId::Bw:
        snprintf(buffer, buffer_size, "7.8 / 10.4 / 15.6 / 20.8 / 31.25 / 41.7 / 62.5 / 125 / 250 / 500 kHz");
        break;
    case ParamId::Sf:
        snprintf(buffer, buffer_size, "Range: 5 to 12, step 1");
        break;
    case ParamId::Cr:
        snprintf(buffer, buffer_size, "Range: 5 to 8, step 1");
        break;
    case ParamId::Sw:
        snprintf(buffer, buffer_size, "Fixed: 0xAB");
        break;
    case ParamId::Tp:
        snprintf(buffer, buffer_size, "Range: -9 to 22 dBm, step 5");
        break;
    case ParamId::Pl:
        snprintf(buffer, buffer_size, "Fixed: 15");
        break;
    case ParamId::Crc:
    case ParamId::Iq:
        snprintf(buffer, buffer_size, "Tap +/- to toggle");
        break;
    case ParamId::Cw:
        snprintf(buffer, buffer_size, "Reserved here: TX button always sends packet payload");
        break;
    case ParamId::Count:
    default:
        buffer[0] = '\0';
        break;
    }
}

void formatParamValue(ParamId param, char *buffer, size_t buffer_size)
{
    switch (param) {
    case ParamId::Freq:
        snprintf(buffer, buffer_size, "%.1f MHz", currentFrequencyMhz());
        break;
    case ParamId::Bw:
        if (currentBandwidthKhz() == 31.25f) {
            snprintf(buffer, buffer_size, "%.2f kHz", currentBandwidthKhz());
        } else {
            snprintf(buffer, buffer_size, "%.1f kHz", currentBandwidthKhz());
        }
        break;
    case ParamId::Sf:
        snprintf(buffer, buffer_size, "%u", g_settings.spreading_factor);
        break;
    case ParamId::Cr:
        snprintf(buffer, buffer_size, "4/%u", g_settings.coding_rate);
        break;
    case ParamId::Sw:
        snprintf(buffer, buffer_size, "0x%02X", g_settings.sync_word);
        break;
    case ParamId::Tp:
        snprintf(buffer, buffer_size, "%d dBm", g_settings.tx_power_dbm);
        break;
    case ParamId::Pl:
        snprintf(buffer, buffer_size, "%u", g_settings.preamble_length);
        break;
    case ParamId::Crc:
        snprintf(buffer, buffer_size, "%s", g_settings.crc_enabled ? "ON" : "OFF");
        break;
    case ParamId::Iq:
        snprintf(buffer, buffer_size, "%s", g_settings.iq_inverted ? "INVERT" : "NORMAL");
        break;
    case ParamId::Cw:
        snprintf(buffer, buffer_size, "RESERVED");
        break;
    case ParamId::Count:
    default:
        buffer[0] = '\0';
        break;
    }
}

int wrapIndex(int value, int delta, int count)
{
    int next = value + delta;
    while (next < 0) {
        next += count;
    }
    while (next >= count) {
        next -= count;
    }
    return next;
}

int clampInt(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

uint16_t adjustPreambleLength(uint16_t value, int direction)
{
    uint16_t step = 1;
    if (value >= 100) {
        step = 10;
    } else if (value >= 20) {
        step = 5;
    }

    if (direction > 0) {
        return static_cast<uint16_t>(std::min<uint32_t>(65535U, static_cast<uint32_t>(value) + step));
    }

    if (value <= step) {
        return 1;
    }
    return static_cast<uint16_t>(value - step);
}

int8_t adjustTxPower(int8_t value, int direction)
{
    return static_cast<int8_t>(clampInt(value + (direction * kTxPowerStepDbm), -9, 22));
}

void computeLayout()
{
    const int margin = 12;
    const int gap = 10;
    const int width = display.width();
    const int height = display.height();
    const int card_width = width - (margin * 2);
    const int button_h = 104;
    const int detail_h = 190;

    g_layout.header = {0, 0, 0, 0};
    g_layout.detail = {margin, margin, card_width, detail_h};

    const int button_y = height - margin - button_h;
    g_layout.param_list = {margin,
                           g_layout.detail.y + g_layout.detail.h + gap,
                           card_width,
                           button_y - gap - (g_layout.detail.y + g_layout.detail.h + gap)};
    g_layout.param_list_title = {g_layout.param_list.x, g_layout.param_list.y, g_layout.param_list.w, 34};

    const int param_count = static_cast<int>(ParamId::Count);
    const int rows_y = g_layout.param_list.y + g_layout.param_list_title.h;
    const int rows_height = g_layout.param_list.h - g_layout.param_list_title.h - 8;
    const int row_height = rows_height / param_count;
    for (int i = 0; i < param_count; ++i) {
        const int y = rows_y + i * row_height;
        const int h = (i == param_count - 1) ? (rows_height - (row_height * i)) : row_height;
        g_layout.param_rows[i] = {g_layout.param_list.x + 8, y, g_layout.param_list.w - 16, h};
    }

    const int button_w = (card_width - (gap * 3)) / 4;
    for (int col = 0; col < 4; ++col) {
        g_layout.buttons[col] = {
            margin + col * (button_w + gap),
            button_y,
            button_w,
            button_h,
        };
    }
}

void drawCard(const Rect &rect, uint8_t gray = 245)
{
    (void)gray;
    display.fillRect(rect.x, rect.y, rect.w, rect.h, TFT_WHITE);
    display.drawRect(rect.x, rect.y, rect.w, rect.h, TFT_BLACK);
}

void drawHeader()
{
}

void drawDetail()
{
    char value[48];
    char range[168];
    char line[160];
    const ParamId selected = g_selected_param;
    const int inner_x = g_layout.detail.x + 12;
    const int inner_w = g_layout.detail.w - 24;

    drawCard(g_layout.detail, 255);

    display.setTextDatum(textdatum_t::top_left);
    display.setTextColor(TFT_BLACK);
    display.setFont(&fonts::Font4);
    display.drawString(paramLabel(selected), inner_x, g_layout.detail.y + 10);

    formatParamValue(selected, value, sizeof(value));
    display.setFont((strnlen(value, sizeof(value)) > 10) ? &fonts::Font4 : &fonts::Font6);
    display.drawString(value, inner_x, g_layout.detail.y + 42);

    display.setFont(&fonts::Font2);
    display.setTextColor(TFT_BLACK);
    formatParamRange(selected, range, sizeof(range));
    display.drawString(range, inner_x, g_layout.detail.y + 100);
    display.drawString(g_status_line, inner_x, g_layout.detail.y + 122);

    display.drawFastHLine(inner_x, g_layout.detail.y + 146, inner_w, TFT_BLACK);
    display.setTextColor(TFT_BLACK);
    snprintf(line,
             sizeof(line),
             "Mode %s   TX %lu   RX %lu",
             runModeLabel(g_run_mode),
             static_cast<unsigned long>(g_tx_counter),
             static_cast<unsigned long>(g_rx_counter));
    display.drawString(line, inner_x, g_layout.detail.y + 154);

    snprintf(line, sizeof(line), "RSSI %.1f dBm   SNR %.1f dB", g_last_rssi, g_last_snr);
    display.drawString(line, inner_x, g_layout.detail.y + 174);

    if (!g_touch_ready) {
        display.setTextColor(TFT_BLACK);
        display.drawString("Touch init failed. Parameters are view-only.", inner_x + 180, g_layout.detail.y + 154);
    }
}

void drawParamRows()
{
    drawCard(g_layout.param_list, 255);

    display.setTextDatum(textdatum_t::middle_left);
    display.setTextColor(TFT_BLACK);
    display.setFont(&fonts::Font4);
    display.drawString("Parameters", g_layout.param_list_title.x + 14, g_layout.param_list_title.y + 21);
    display.drawFastHLine(g_layout.param_list.x + 12,
                          g_layout.param_list.y + g_layout.param_list_title.h,
                          g_layout.param_list.w - 24,
                          TFT_BLACK);

    const int selected_index = static_cast<int>(g_selected_param);

    for (int i = 0; i < static_cast<int>(ParamId::Count); ++i) {
        const Rect &row = g_layout.param_rows[i];
        const bool selected = (i == selected_index);
        const uint32_t label_color = selected ? TFT_WHITE : TFT_BLACK;
        const uint32_t value_color = selected ? TFT_WHITE : TFT_BLACK;
        char value[48];

        if(selected) {
            display.fillRect(row.x, row.y, row.w, row.h - 2, TFT_BLACK);
        }else {
            display.fillRect(row.x, row.y, row.w, row.h - 2, TFT_WHITE);
        }
        display.drawRect(row.x, row.y, row.w, row.h - 2, TFT_BLACK);

        formatParamValue(static_cast<ParamId>(i), value, sizeof(value));

        display.setTextDatum(textdatum_t::middle_left);
        display.setTextColor(label_color);
        display.setFont(&fonts::Font4);
        display.drawString(paramLabel(static_cast<ParamId>(i)), row.x + 14, row.y + (row.h / 2));

        display.setTextDatum(textdatum_t::middle_right);
        display.setTextColor(value_color);
        display.setFont(&fonts::Font4);
        display.drawString(value, row.x + row.w - 14, row.y + (row.h / 2));
    }
}

void drawButton(const Rect &rect, const char *title, bool active)
{
    const uint32_t fill = active ? TFT_BLACK : TFT_WHITE;
    const uint32_t text_color = active ? TFT_WHITE : TFT_BLACK;

    display.fillRect(rect.x, rect.y, rect.w, rect.h, fill);
    display.drawRect(rect.x, rect.y, rect.w, rect.h, TFT_BLACK);

    display.setTextColor(text_color);
    display.setTextDatum(textdatum_t::middle_center);
    display.setFont(&fonts::Font4);
    display.drawString(title, rect.x + (rect.w / 2), rect.y + (rect.h / 2));
}

void drawButtons()
{
    drawButton(g_layout.buttons[static_cast<int>(ButtonId::Minus)], "-", false);
    drawButton(g_layout.buttons[static_cast<int>(ButtonId::Plus)], "+", false);
    drawButton(g_layout.buttons[static_cast<int>(ButtonId::Rx)], "RX", g_run_mode == RadioRunMode::Receive);
    drawButton(g_layout.buttons[static_cast<int>(ButtonId::Tx)], "TX", g_run_mode == RadioRunMode::TxAuto);
}

void renderUi(bool clear_background)
{
    (void)clear_background;
    display.fillScreen(TFT_WHITE);

    drawDetail();
    drawParamRows();
    drawButtons();
}

void presentScreen(epd_mode_t mode)
{
    display.powerSaveOff();
    display.setEpdMode(mode);
    display.display();
    display.waitDisplay();
    display.powerSaveOn();
}

void redrawUi(bool full_refresh)
{
    if (!g_display_ready) {
        return;
    }

    display.startWrite();
    renderUi(full_refresh);
    display.endWrite();

    presentScreen(full_refresh ? epd_mode_t::epd_quality : consumeRefreshMode());
}

void flushUi()
{
    if (!g_ui_dirty) {
        return;
    }
    if (g_touch_tap.down) {
        return;
    }

    g_ui_dirty = false;
    redrawUi(false);
}

bool initTouch()
{
    auto resetTouchBus = []() {
#if defined(ARDUINO_ARCH_ESP32)
        Wire.end();
        delay(2);
#endif
        Wire.begin(kI2cSda, kI2cScl);
        Wire.setClock(kTouchI2cSpeedHz);
    };

    auto prepareTouchReset = [](bool low_address) {
        pinMode(kPinTouchRst, OUTPUT);
        pinMode(kPinTouchInt, OUTPUT);
        digitalWrite(kPinTouchRst, LOW);
        digitalWrite(kPinTouchInt, low_address ? LOW : HIGH);
        delay(kTouchResetHoldMs);
        digitalWrite(kPinTouchRst, HIGH);
        delay(kTouchBootDelayMs);
        pinMode(kPinTouchInt, INPUT_PULLUP);
        delay(2);
    };

    auto tryTouchInit = [&](uint8_t address) {
        prepareTouchReset(address == GT911_SLAVE_ADDRESS_L);
        resetTouchBus();
        touch.setPins(kPinTouchRst, kPinTouchInt);
        const bool ok = touch.begin(Wire, address, kI2cSda, kI2cScl);
        Serial.printf("[LORA_UI] GT911 init %s at 0x%02X\n", ok ? "ok" : "fail", address);
        return ok;
    };

    for (uint8_t attempt = 0; attempt < kTouchInitAttempts; ++attempt) {
        if (tryTouchInit(GT911_SLAVE_ADDRESS_L) || tryTouchInit(GT911_SLAVE_ADDRESS_H)) {
            touch.setHomeButtonCallback([](void *user_data) {
                (void)user_data;
            }, nullptr);

            touch.setInterruptMode(LOW_LEVEL_QUERY);
            touch.setMaxCoordinates(display.width() - 1, display.height() - 1);
            return true;
        }

        Serial.printf("[LORA_UI] GT911 retry %u/%u\n",
                      static_cast<unsigned>(attempt + 1),
                      static_cast<unsigned>(kTouchInitAttempts));
        delay(kTouchRetryDelayMs);
    }

    Serial.println("[LORA_UI] GT911 init failed");
    return false;
}

bool radioCallOk(const char *what, int16_t state)
{
    if (state == RADIOLIB_ERR_NONE) {
        return true;
    }

    Serial.printf("[LORA_UI] %s failed, code %d\n", what, state);
    setStatus("%s failed (%d)", what, state);
    markUiDirty(epd_mode_t::epd_text);
    return false;
}

void stopRadioActivity()
{
    if (!g_radio_ready) {
        g_run_mode = RadioRunMode::Standby;
        g_tx_in_progress = false;
        return;
    }

    if (g_tx_in_progress || g_run_mode == RadioRunMode::TxAuto) {
        radio.finishTransmit();
    } else {
        radio.standby();
    }

    g_tx_done_flag = false;
    g_rx_done_flag = false;
    g_tx_in_progress = false;
    g_run_mode = RadioRunMode::Standby;
}

bool startReceiveMode();
bool startAutoTxMode();

bool restoreMode(RadioRunMode mode)
{
    switch (mode) {
    case RadioRunMode::Receive:
        return startReceiveMode();
    case RadioRunMode::TxAuto:
        return startAutoTxMode();
    case RadioRunMode::Standby:
    default:
        g_run_mode = RadioRunMode::Standby;
        setStatus("Parameters applied");
        markUiDirty(epd_mode_t::epd_text);
        return true;
    }
}

bool applyRadioConfig(bool restore_current_mode)
{
    if (!g_radio_ready) {
        return false;
    }

    RadioRunMode mode_to_restore = restore_current_mode ? g_run_mode : RadioRunMode::Standby;

    stopRadioActivity();

    if (!radioCallOk("setFrequency", radio.setFrequency(currentFrequencyMhz()))) {
        return false;
    }
    if (!radioCallOk("setBandwidth", radio.setBandwidth(currentBandwidthKhz()))) {
        return false;
    }
    if (!radioCallOk("setSpreadingFactor", radio.setSpreadingFactor(g_settings.spreading_factor))) {
        return false;
    }
    if (!radioCallOk("setCodingRate", radio.setCodingRate(g_settings.coding_rate))) {
        return false;
    }
    if (!radioCallOk("setSyncWord", radio.setSyncWord(g_settings.sync_word))) {
        return false;
    }
    if (!radioCallOk("setOutputPower", radio.setOutputPower(g_settings.tx_power_dbm))) {
        return false;
    }
    if (!radioCallOk("setCurrentLimit", radio.setCurrentLimit(kCurrentLimitMilliamp))) {
        return false;
    }
    if (!radioCallOk("setPreambleLength", radio.setPreambleLength(g_settings.preamble_length))) {
        return false;
    }
    if (!radioCallOk("setCRC", radio.setCRC(g_settings.crc_enabled ? 2 : 0))) {
        return false;
    }
    if (!radioCallOk("invertIQ", radio.invertIQ(g_settings.iq_inverted))) {
        return false;
    }

    return restoreMode(mode_to_restore);
}

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void onTxDone()
{
    g_tx_done_flag = true;
}

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void onRxDone()
{
    g_rx_done_flag = true;
}

bool sendCurrentPacket(bool auto_mode)
{
    const bool continuing_auto_tx = auto_mode && (g_run_mode == RadioRunMode::TxAuto);
    copyText(g_last_payload, sizeof(g_last_payload), kTxPayloadLabel);
    radio.setPacketSentAction(onTxDone);
    const int16_t state = radio.startTransmit(const_cast<uint8_t *>(kTxPayload), sizeof(kTxPayload));
    if (!radioCallOk("startTransmit", state)) {
        g_run_mode = RadioRunMode::Standby;
        g_tx_in_progress = false;
        return false;
    }

    g_run_mode = auto_mode ? RadioRunMode::TxAuto : RadioRunMode::Standby;
    g_tx_in_progress = true;
    if (!continuing_auto_tx) {
        setStatus(auto_mode ? "TX running: 01 02 03 04 05" : "Sending packet");
        g_last_passive_ui_ms = millis();
        markUiDirty(epd_mode_t::epd_text);
    }
    return true;
}

bool startReceiveMode()
{
    if (!g_radio_ready) {
        return false;
    }

    stopRadioActivity();
    radio.setPacketReceivedAction(onRxDone);
    if (!radioCallOk("startReceive", radio.startReceive())) {
        return false;
    }

    g_run_mode = RadioRunMode::Receive;
    setStatus("Listening on %.1f MHz", currentFrequencyMhz());
    markUiDirty(epd_mode_t::epd_text);
    return true;
}

bool startAutoTxMode()
{
    if (!g_radio_ready) {
        return false;
    }

    stopRadioActivity();
    g_next_auto_tx_ms = millis();
    return sendCurrentPacket(true);
}

void enterStandby()
{
    stopRadioActivity();
    setStatus("Radio standby");
    markUiDirty(epd_mode_t::epd_text);
}

void handleRadioEvents()
{
    if (!g_radio_ready) {
        return;
    }

    if (g_tx_done_flag) {
        g_tx_done_flag = false;
        g_tx_in_progress = false;
        radio.finishTransmit();
        ++g_tx_counter;

        if (g_run_mode == RadioRunMode::TxAuto) {
            g_next_auto_tx_ms = millis() + kAutoTxIntervalMs;
            setStatus("TX running: 01 02 03 04 05");
            markPassiveUiDirty();
        } else {
            g_run_mode = RadioRunMode::Standby;
            setStatus("Packet sent");
            markUiDirty(epd_mode_t::epd_text);
        }
    }

    if (g_run_mode == RadioRunMode::TxAuto && !g_tx_in_progress && millis() >= g_next_auto_tx_ms) {
        sendCurrentPacket(true);
    }

    if (g_rx_done_flag) {
        g_rx_done_flag = false;
        String incoming;
        const int16_t state = radio.readData(incoming);
        if (state == RADIOLIB_ERR_NONE) {
            ++g_rx_counter;
            g_last_rssi = radio.getRSSI();
            g_last_snr = radio.getSNR();
            g_last_freq_error = radio.getFrequencyError();
            copyText(g_last_payload, sizeof(g_last_payload), incoming.c_str());
            setStatus("Packet received (%lu)", static_cast<unsigned long>(g_rx_counter));
            markUiDirty(epd_mode_t::epd_text);
        } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
            setStatus("CRC mismatch");
            markUiDirty(epd_mode_t::epd_text);
        } else {
            radioCallOk("readData", state);
        }
    }
}

void adjustSelectedParam(int delta)
{
    const ParamId param = g_selected_param;

    if (!isParamEditable(param)) {
        if (param == ParamId::Sw) {
            setStatus("Sync Word is fixed at 0xAB");
        } else if (param == ParamId::Pl) {
            setStatus("Preamble length is fixed at 15");
        } else if (param == ParamId::Cw) {
            setStatus("CW is reserved in this demo");
        }
        markUiDirty(epd_mode_t::epd_text);
        return;
    }

    switch (param) {
    case ParamId::Freq:
        g_settings.freq_index = static_cast<uint8_t>(
            wrapIndex(g_settings.freq_index, delta, static_cast<int>(sizeof(kFreqOptionsMhz) / sizeof(kFreqOptionsMhz[0]))));
        break;
    case ParamId::Bw:
        g_settings.bw_index = static_cast<uint8_t>(
            wrapIndex(g_settings.bw_index, delta, static_cast<int>(sizeof(kBandwidthOptionsKhz) / sizeof(kBandwidthOptionsKhz[0]))));
        break;
    case ParamId::Sf:
        g_settings.spreading_factor = static_cast<uint8_t>(clampInt(g_settings.spreading_factor + delta, 5, 12));
        break;
    case ParamId::Cr:
        g_settings.coding_rate = static_cast<uint8_t>(clampInt(g_settings.coding_rate + delta, 5, 8));
        break;
    case ParamId::Sw:
        break;
    case ParamId::Tp:
        g_settings.tx_power_dbm = adjustTxPower(g_settings.tx_power_dbm, delta);
        break;
    case ParamId::Pl:
        break;
    case ParamId::Crc:
        g_settings.crc_enabled = !g_settings.crc_enabled;
        break;
    case ParamId::Iq:
        g_settings.iq_inverted = !g_settings.iq_inverted;
        break;
    case ParamId::Cw:
        g_settings.cw_enabled = !g_settings.cw_enabled;
        break;
    case ParamId::Count:
    default:
        return;
    }

    setStatus("%s updated", paramLabel(param));
    applyRadioConfig(true);
    markUiDirty(epd_mode_t::epd_text);
}

void selectParam(int delta)
{
    const int count = static_cast<int>(ParamId::Count);
    g_selected_param = static_cast<ParamId>(wrapIndex(static_cast<int>(g_selected_param), delta, count));
    markUiDirty(epd_mode_t::epd_text);
}

ButtonId buttonHitTest(int16_t x, int16_t y)
{
    for (int i = 0; i < static_cast<int>(ButtonId::Count); ++i) {
        if (pointInRect(x, y, g_layout.buttons[i])) {
            return static_cast<ButtonId>(i);
        }
    }
    return ButtonId::Count;
}

void handleButton(ButtonId button)
{
    switch (button) {
    case ButtonId::Minus:
        adjustSelectedParam(-1);
        break;
    case ButtonId::Plus:
        adjustSelectedParam(1);
        break;
    case ButtonId::Rx:
        if (g_run_mode == RadioRunMode::Receive) {
            enterStandby();
        } else {
            startReceiveMode();
        }
        break;
    case ButtonId::Tx:
        if (g_run_mode == RadioRunMode::TxAuto) {
            enterStandby();
        } else {
            startAutoTxMode();
        }
        break;
    case ButtonId::Count:
    default:
        break;
    }
}

void handleTap(int16_t x, int16_t y)
{
    const ButtonId button = buttonHitTest(x, y);
    if (button != ButtonId::Count) {
        handleButton(button);
        return;
    }

    for (int i = 0; i < static_cast<int>(ParamId::Count); ++i) {
        if (pointInRect(x, y, g_layout.param_rows[i])) {
            g_selected_param = static_cast<ParamId>(i);
            markUiDirty(epd_mode_t::epd_text);
            return;
        }
    }
}

void pollTouch()
{
    if (!g_touch_ready) {
        return;
    }

    int16_t x[kMaxTouchPoints] = {0};
    int16_t y[kMaxTouchPoints] = {0};

    if (touch.isPressed()) {
        const uint8_t count = touch.getPoint(x, y, kMaxTouchPoints);
        if (count == 0) {
            return;
        }

        if (!g_touch_tap.down) {
            g_touch_tap.down = true;
            g_touch_tap.start_x = x[0];
            g_touch_tap.start_y = y[0];
            g_touch_tap.down_ms = millis();
        }

        g_touch_tap.last_x = x[0];
        g_touch_tap.last_y = y[0];
        return;
    }

    if (!g_touch_tap.down) {
        return;
    }

    const uint32_t now = millis();
    const int dx = abs(g_touch_tap.last_x - g_touch_tap.start_x);
    const int dy = abs(g_touch_tap.last_y - g_touch_tap.start_y);
    const bool stable_tap = dx < 14 && dy < 14;
    const bool debounced = (now - g_touch_tap.last_tap_ms) > kTapDebounceMs;

    g_touch_tap.down = false;

    if (stable_tap && debounced) {
        g_touch_tap.last_tap_ms = now;
        handleTap(g_touch_tap.last_x, g_touch_tap.last_y);
    }
}

bool initRadio()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    if (!pca9535SetLevel(kPcaPinLoraEnable, true)) {
        Serial.println("[LORA_UI] failed to enable LoRa power");
        setStatus("LoRa power enable failed");
        return false;
    }

    delay(1500);
    SPI.begin(BOARD_SPI_SCLK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    if (!radioCallOk("radio.begin", radio.begin(currentFrequencyMhz()))) {
        return false;
    }
    if (!radioCallOk("setTCXO", radio.setTCXO(kTcxoVoltage))) {
        return false;
    }
    if (!radioCallOk("setDio2AsRfSwitch", radio.setDio2AsRfSwitch())) {
        return false;
    }

    g_radio_ready = true;
    return applyRadioConfig(false);
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(400);

    Serial.println();
    Serial.println("[LORA_UI] start");

    if (!display.init_without_reset(false)) {
        Serial.println("[LORA_UI] display init failed");
        while (true) {
            delay(1000);
        }
    }

    g_display_ready = true;
    display.setAutoDisplay(false);
    display.setColorDepth(1);
    display.setRotation(0);
    display.setTextWrap(false);
    computeLayout();

    g_touch_ready = initTouch();
    g_radio_ready = initRadio();

    if (g_radio_ready) {
        copyText(g_last_payload, sizeof(g_last_payload), kTxPayloadLabel);
        setStatus("Tap RX or TX to begin");
    }

    redrawUi(true);
    g_ui_dirty = false;
    g_next_refresh_rank = 0;

    Serial.printf("[LORA_UI] ready, display=%dx%d, touch=%s, radio=%s\n",
                  display.width(),
                  display.height(),
                  g_touch_ready ? "ok" : "fail",
                  g_radio_ready ? "ok" : "fail");
}

void loop()
{
    pollTouch();
    handleRadioEvents();
    flushUi();
    delay(20);
}
