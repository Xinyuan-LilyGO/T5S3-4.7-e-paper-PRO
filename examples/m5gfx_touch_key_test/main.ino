#include <Arduino.h>
#include <M5GFX.h>
#include <Wire.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "TouchDrvGT911.hpp"
#include "driver/gpio.h"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"

using lgfx::epd_mode_t;

namespace {

constexpr int kPanelWidth = 960;
constexpr int kPanelHeight = 540;
constexpr int kEpdBusSpeedHz = 20000000;
constexpr int kVcomMillivolts = 1560;

constexpr int kI2cSda = 39;
constexpr int kI2cScl = 40;
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

constexpr uint8_t kPcaPinEpdOe = 8;
constexpr uint8_t kPcaPinEpdMode = 9;
constexpr uint8_t kPcaPinButton = 10;
constexpr uint8_t kPcaPinTpsPwrUp = 11;
constexpr uint8_t kPcaPinVcomCtrl = 12;
constexpr uint8_t kPcaPinTpsWakeUp = 13;
constexpr uint8_t kPcaPinTpsPowerGood = 14;

constexpr gpio_num_t kPinBoot = GPIO_NUM_0;
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
constexpr gpio_num_t kPinDummyBus = GPIO_NUM_1;
constexpr gpio_num_t kPinEpdSth = GPIO_NUM_41;
constexpr gpio_num_t kPinEpdLe = GPIO_NUM_42;
constexpr gpio_num_t kPinEpdStv = GPIO_NUM_45;
constexpr gpio_num_t kPinEpdCkv = GPIO_NUM_48;

constexpr uint8_t kPanelOffsetRotation = 3;
constexpr int kPowerGoodTimeoutMs = 400;
constexpr int kMargin = 16;
constexpr int kGap = 12;
constexpr int kTitleHeight = 82;
constexpr int kTouchStatusHeight = 120;
constexpr int kTouchInnerMargin = 14;
constexpr int kTouchMarkerRadius = 14;
constexpr int kTouchMarkerCrossRadius = 22;
constexpr int kKeyCornerRadius = 20;
constexpr int kKeyStateHeight = 40;
constexpr int kKeyCountHeight = 24;
constexpr uint32_t kHomeDebounceMs = 250;
constexpr uint32_t kHomeHighlightMs = 180;
constexpr uint32_t kTouchRefreshIntervalMs = 180;
constexpr uint32_t kTouchIdleCleanupDelayMs = 900;
constexpr int kTouchMoveThreshold = 6;
constexpr int kPollPeriodMs = 40;
constexpr uint8_t kFastTouchRefreshesBeforeCleanup = 8;
constexpr uint8_t kTextCleanupsBeforeQuality = 4;
constexpr uint8_t kMaxTouchPoints = 5;
constexpr uint8_t kVisibleTouchPoints = 2;

enum DirtyFlags : uint32_t {
    kDirtyNone = 0,
    kDirtyTouch = 1U << 0,
    kDirtyHome = 1U << 1,
    kDirtyBoot = 1U << 2,
    kDirtyPcaButton = 1U << 3,
    kDirtyAll = kDirtyTouch | kDirtyHome | kDirtyBoot | kDirtyPcaButton,
};

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

struct KeyState {
    bool pressed = false;
    bool seen = false;
    uint32_t press_count = 0;
    uint32_t last_change_ms = 0;
};

struct HomeState {
    bool seen = false;
    bool highlighted = false;
    uint32_t press_count = 0;
    uint32_t last_event_ms = 0;
};

struct TouchState {
    bool active = false;
    bool seen = false;
    uint8_t active_points = 0;
    uint8_t last_points = 0;
    int16_t x[kMaxTouchPoints] = {0};
    int16_t y[kMaxTouchPoints] = {0};
    uint32_t last_event_ms = 0;
};

struct UiLayout {
    Rect title;
    Rect touch;
    Rect keys[3];
};

uint8_t g_pca_output[2] = {0xFF, 0x00};
uint32_t g_dirty = kDirtyAll;
uint32_t g_last_touch_redraw_ms = 0;
uint8_t g_fast_touch_refreshes = 0;
uint8_t g_text_cleanup_count = 0;
int16_t g_touch_max_x = 0;
int16_t g_touch_max_y = 0;
bool g_touch_cleanup_pending = false;
uint32_t g_touch_release_ms = 0;

TouchDrvGT911 touch;
UiLayout layout;
KeyState boot_button;
KeyState pca_button;
HomeState home_button;
TouchState touch_state;

bool g_display_ready = false;
bool g_touch_ready = false;

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

bool i2cReadRegister(uint8_t address, uint8_t reg, uint8_t *buffer, size_t length)
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

bool pca9535Init()
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

bool pca9535GetLevel(uint8_t pin, bool &level)
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

bool tpsWriteRegister(uint8_t reg, const uint8_t *data, size_t length)
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

bool tpsWriteRegisterU8(uint8_t reg, uint8_t value)
{
    return tpsWriteRegister(reg, &value, 1);
}

bool tpsReadRegisterU8(uint8_t reg, uint8_t &value)
{
    return i2cReadRegister(kTps651851Address, reg, &value, 1);
}

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
            Serial.println("[M5GFX_TOUCH_KEY] PCA9535 init failed");
            return false;
        }

        const bool ok = lgfx::Bus_EPD::init();
        if (!ok) {
            Serial.println("[M5GFX_TOUCH_KEY] Bus_EPD init failed");
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
            Serial.printf("[M5GFX_TOUCH_KEY] power %s failed\n", power_on ? "on" : "off");
            return false;
        }

        _pwr_on = power_on;
        return true;
    }

private:
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

uint32_t grayColor(uint8_t gray)
{
    return display.color888(gray, gray, gray);
}

void markDirty(uint32_t flag)
{
    g_dirty |= flag;
}

void drawPanelFrame(const Rect &rect, uint8_t gray = 246)
{
    const uint32_t fill = grayColor(gray);
    display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 16, fill);
    display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 16, TFT_BLACK);
}

void computeLayout()
{
    const int width = display.width();
    const int height = display.height();
    const int content_width = width - (kMargin * 2);
    const int key_height = 190;
    const int key_width = (content_width - (kGap * 2)) / 3;
    int y = kMargin;

    layout.title = {kMargin, y, content_width, kTitleHeight};
    y += kTitleHeight + kGap;

    layout.touch = {kMargin, y, content_width, height - y - key_height - kGap - kMargin};
    y = layout.touch.y + layout.touch.h + kGap;

    for (int i = 0; i < 3; ++i) {
        layout.keys[i] = {
            kMargin + (i * (key_width + kGap)),
            y,
            key_width,
            key_height,
        };
    }
}

Rect touchCanvasRect()
{
    return {
        layout.touch.x + kTouchInnerMargin,
        layout.touch.y + kTouchStatusHeight,
        layout.touch.w - (kTouchInnerMargin * 2),
        layout.touch.h - kTouchStatusHeight - kTouchInnerMargin,
    };
}

void drawTitlePanel()
{
    drawPanelFrame(layout.title, 238);

    display.setTextColor(TFT_BLACK, grayColor(238));
    display.setTextDatum(textdatum_t::top_left);

    display.setFont(&fonts::Font4);
    display.drawString("Touch Test", layout.title.x + 16, layout.title.y + 14);

    display.setFont(&fonts::Font2);
    display.drawString("GT911 realtime view   |   HOME / BOOT / PCA9535_IO12_BUTTON", layout.title.x + 16, layout.title.y + 52);
}

void drawTouchPointSummary(int x, int y)
{
    char buffer[64];
    display.setFont(&fonts::Font2);
    display.setTextColor(TFT_BLACK, grayColor(244));
    display.setTextDatum(textdatum_t::top_left);

    if (touch_state.seen && touch_state.last_points > 0) {
        for (uint8_t i = 0; i < std::min<uint8_t>(touch_state.last_points, kVisibleTouchPoints); ++i) {
            snprintf(buffer, sizeof(buffer), "P%u  X:%3d  Y:%3d", i, touch_state.x[i], touch_state.y[i]);
            display.drawString(buffer, x, y + (i * 24));
        }
    } else {
        display.drawString("P0  X:---  Y:---", x, y);
        display.drawString("P1  X:---  Y:---", x, y + 24);
    }
}

void drawTouchMarkers(const Rect &canvas)
{
    const int max_x = std::max(1, display.width() - 1);
    const int max_y = std::max(1, display.height() - 1);
    const int plot_w = std::max(1, canvas.w - 2);
    const int plot_h = std::max(1, canvas.h - 2);

    for (uint8_t i = 0; i < std::min<uint8_t>(touch_state.last_points, kVisibleTouchPoints); ++i) {
        const int px = canvas.x + 1 + ((static_cast<int32_t>(touch_state.x[i]) * plot_w) / max_x);
        const int py = canvas.y + 1 + ((static_cast<int32_t>(touch_state.y[i]) * plot_h) / max_y);
        const uint32_t accent = (i == 0) ? TFT_BLACK : grayColor(70);

        display.drawLine(px - kTouchMarkerCrossRadius, py, px + kTouchMarkerCrossRadius, py, accent);
        display.drawLine(px, py - kTouchMarkerCrossRadius, px, py + kTouchMarkerCrossRadius, accent);
        display.drawCircle(px, py, kTouchMarkerRadius, accent);
        display.fillCircle(px, py, 4, accent);
        display.setFont(&fonts::Font2);
        display.setTextColor(TFT_WHITE, accent);
        display.setTextDatum(textdatum_t::middle_center);
        display.drawString(i == 0 ? "P0" : "P1", px, py);
    }
}

void drawTouchPanel()
{
    char buffer[96];
    drawPanelFrame(layout.touch, 244);

    display.setTextDatum(textdatum_t::top_left);
    display.setFont(&fonts::Font4);
    display.setTextColor(TFT_BLACK, grayColor(244));

    if (g_touch_ready) {
        display.drawString(touch_state.active ? "ACTIVE" : "IDLE", layout.touch.x + 16, layout.touch.y + 16);

        display.setFont(&fonts::Font2);
        display.setTextColor(TFT_BLACK, grayColor(244));
        snprintf(buffer, sizeof(buffer), "POINTS  %u", touch_state.active_points);
        display.drawString(buffer, layout.touch.x + 16, layout.touch.y + 54);

        if (g_touch_max_x > 0 && g_touch_max_y > 0) {
            snprintf(buffer, sizeof(buffer), "MAP  %d x %d", g_touch_max_x + 1, g_touch_max_y + 1);
            display.drawString(buffer, layout.touch.x + layout.touch.w - 150, layout.touch.y + 20);
        }

        drawTouchPointSummary(layout.touch.x + 180, layout.touch.y + 24);

        const Rect canvas = touchCanvasRect();
        display.fillRoundRect(canvas.x, canvas.y, canvas.w, canvas.h, 14, TFT_WHITE);
        display.drawRoundRect(canvas.x, canvas.y, canvas.w, canvas.h, 14, TFT_BLACK);

        const int col_1 = canvas.x + (canvas.w / 3);
        const int col_2 = canvas.x + ((canvas.w * 2) / 3);
        const int row_1 = canvas.y + (canvas.h / 3);
        const int row_2 = canvas.y + ((canvas.h * 2) / 3);
        display.drawFastVLine(col_1, canvas.y + 8, canvas.h - 16, grayColor(205));
        display.drawFastVLine(col_2, canvas.y + 8, canvas.h - 16, grayColor(205));
        display.drawFastHLine(canvas.x + 8, row_1, canvas.w - 16, grayColor(205));
        display.drawFastHLine(canvas.x + 8, row_2, canvas.w - 16, grayColor(205));

        display.setFont(&fonts::Font2);
        display.setTextColor(grayColor(90), TFT_WHITE);
        display.setTextDatum(textdatum_t::top_left);
        display.drawString("0,0", canvas.x + 10, canvas.y + 10);
        display.setTextDatum(textdatum_t::bottom_right);
        display.drawString("MAX", canvas.x + canvas.w - 10, canvas.y + canvas.h - 10);

        if (touch_state.seen && touch_state.last_points > 0) {
            drawTouchMarkers(canvas);
        } else {
            display.setFont(&fonts::Font4);
            display.setTextColor(grayColor(110), TFT_WHITE);
            display.setTextDatum(textdatum_t::middle_center);
            display.drawString("TOUCH HERE", canvas.x + (canvas.w / 2), canvas.y + (canvas.h / 2));
        }
    } else {
        display.drawString("TOUCH ERROR", layout.touch.x + 16, layout.touch.y + 16);
        display.setFont(&fonts::Font2);
        display.setTextColor(TFT_BLACK, grayColor(244));
        display.drawString("GT911 init failed.", layout.touch.x + 16, layout.touch.y + 58);
        display.drawString("Check touch wiring or address 0x5D.", layout.touch.x + 16, layout.touch.y + 84);
    }
}

void drawVirtualKey(const Rect &rect,
                    const char *label,
                    bool active,
                    uint32_t count,
                    const char *idle_text,
                    const char *active_text)
{
    char buffer[96];
    const uint32_t fill = active ? grayColor(28) : grayColor(242);
    const uint32_t text = active ? TFT_WHITE : TFT_BLACK;
    const int inner_y = rect.y + 46;
    const int state_y = rect.y + 74;
    const int count_y = rect.y + rect.h - 34;

    display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, kKeyCornerRadius, fill);
    display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, kKeyCornerRadius, TFT_BLACK);
    display.drawFastHLine(rect.x + 14, inner_y, rect.w - 28, active ? TFT_WHITE : TFT_BLACK);

    display.setTextColor(text, fill);
    display.setTextDatum(textdatum_t::top_center);

    display.setFont(&fonts::Font4);
    display.drawString(label, rect.x + (rect.w / 2), rect.y + 16);

    display.setFont(&fonts::Font4);
    display.drawString(active ? active_text : idle_text, rect.x + (rect.w / 2), state_y);

    display.setFont(&fonts::Font4);
    snprintf(buffer, sizeof(buffer), "COUNT %lu", static_cast<unsigned long>(count));
    display.drawString(buffer, rect.x + (rect.w / 2), count_y);
}

void drawHomeKey()
{
    drawVirtualKey(layout.keys[0], "HOME", home_button.highlighted, home_button.press_count, "READY", "DOWN");
}

void drawBootKey()
{
    drawVirtualKey(layout.keys[1], "BOOT", boot_button.pressed, boot_button.press_count, "UP", "DOWN");
}

void drawPcaButtonKey()
{
    drawVirtualKey(layout.keys[2], "IO48", pca_button.pressed, pca_button.press_count, "UP", "DOWN");
}

void presentScreen(epd_mode_t mode)
{
    display.powerSaveOff();
    display.setEpdMode(mode);
    display.display();
    display.waitDisplay();
    display.powerSaveOn();
}

void renderUiFrame(bool clear_background)
{
    if (clear_background) {
        display.fillScreen(TFT_WHITE);
    }
    drawTitlePanel();
    drawTouchPanel();
    drawHomeKey();
    drawBootKey();
    drawPcaButtonKey();
}

void redrawFullScreen()
{
    computeLayout();

    display.setAutoDisplay(false);
    display.setColorDepth(4);
    display.setRotation(0);
    display.setTextWrap(false);
    display.setEpdMode(epd_mode_t::epd_quality);

    display.startWrite();
    renderUiFrame(true);
    display.endWrite();

    presentScreen(epd_mode_t::epd_quality);
}

bool initTouch()
{
    touch.setPins(kPinTouchRst, kPinTouchInt);
    if (!touch.begin(Wire, GT911_SLAVE_ADDRESS_L, kI2cSda, kI2cScl)) {
        Serial.println("[M5GFX_TOUCH_KEY] GT911 init failed");
        return false;
    }

    touch.setHomeButtonCallback([](void *user_data) {
        (void)user_data;
        const uint32_t now = millis();
        if ((now - home_button.last_event_ms) < kHomeDebounceMs) {
            return;
        }

        home_button.seen = true;
        home_button.highlighted = true;
        home_button.press_count++;
        home_button.last_event_ms = now;
        markDirty(kDirtyHome);
    }, nullptr);

    touch.setInterruptMode(LOW_LEVEL_QUERY);
    touch.setMaxCoordinates(display.width() - 1, display.height() - 1);
    g_touch_max_x = display.width() - 1;
    g_touch_max_y = display.height() - 1;
    return true;
}

void updateKeyState(KeyState &state, bool pressed, uint32_t key_flag)
{
    if (state.pressed == pressed) {
        return;
    }

    state.pressed = pressed;
    state.last_change_ms = millis();
    markDirty(key_flag);

    if (pressed) {
        state.seen = true;
        state.press_count++;
    }
}

bool touchPointsChanged(uint8_t count, const int16_t *x, const int16_t *y)
{
    if (!touch_state.active || touch_state.last_points != count) {
        return true;
    }

    for (uint8_t i = 0; i < count; ++i) {
        if (abs(touch_state.x[i] - x[i]) >= kTouchMoveThreshold ||
            abs(touch_state.y[i] - y[i]) >= kTouchMoveThreshold) {
            return true;
        }
    }
    return false;
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
        if (count > 0) {
            const uint32_t now = millis();
            const bool periodic = (now - g_last_touch_redraw_ms) >= kTouchRefreshIntervalMs;

            if (touchPointsChanged(count, x, y) || periodic) {
                touch_state.active = true;
                touch_state.seen = true;
                touch_state.active_points = count;
                touch_state.last_points = count;
                touch_state.last_event_ms = now;
                for (uint8_t i = 0; i < count; ++i) {
                    touch_state.x[i] = x[i];
                    touch_state.y[i] = y[i];
                }
                g_last_touch_redraw_ms = now;
                g_touch_cleanup_pending = false;
                markDirty(kDirtyTouch);
            } else {
                touch_state.active = true;
                touch_state.active_points = count;
                touch_state.last_event_ms = now;
            }
            return;
        }
    }

    if (touch_state.active) {
        touch_state.active = false;
        touch_state.active_points = 0;
        touch_state.last_event_ms = millis();
        g_touch_cleanup_pending = true;
        g_touch_release_ms = touch_state.last_event_ms;
        markDirty(kDirtyTouch);
    }
}

void pollKeys()
{
    updateKeyState(boot_button, digitalRead(kPinBoot) == LOW, kDirtyBoot);

    bool pca_level = true;
    if (pca9535GetLevel(kPcaPinButton, pca_level)) {
        updateKeyState(pca_button, !pca_level, kDirtyPcaButton);
    }
}

void refreshDirtyCards()
{
    const uint32_t now = millis();

    if (home_button.highlighted && (millis() - home_button.last_event_ms) >= kHomeHighlightMs) {
        home_button.highlighted = false;
        g_dirty |= kDirtyHome;
    }

    if (g_touch_cleanup_pending &&
        !touch_state.active &&
        (now - g_touch_release_ms) >= kTouchIdleCleanupDelayMs) {
        epd_mode_t cleanup_mode = epd_mode_t::epd_text;
        ++g_text_cleanup_count;
        if (g_text_cleanup_count >= kTextCleanupsBeforeQuality) {
            cleanup_mode = epd_mode_t::epd_quality;
            g_text_cleanup_count = 0;
        }

        display.waitDisplay();
        presentScreen(cleanup_mode);
        g_touch_cleanup_pending = false;
        return;
    }

    if (!g_display_ready || g_dirty == kDirtyNone) {
        return;
    }

    const uint32_t dirty = g_dirty;
    g_dirty = kDirtyNone;

    display.startWrite();
    renderUiFrame(false);
    display.endWrite();

    epd_mode_t mode = epd_mode_t::epd_text;

    if (dirty & kDirtyTouch) {
        if (touch_state.active) {
            mode = epd_mode_t::epd_fast;
            ++g_fast_touch_refreshes;
            if (g_fast_touch_refreshes >= kFastTouchRefreshesBeforeCleanup) {
                mode = epd_mode_t::epd_text;
                g_fast_touch_refreshes = 0;
            }
        } else {
            mode = epd_mode_t::epd_fast;
            g_fast_touch_refreshes = 0;
        }
    } else if (dirty & (kDirtyHome | kDirtyBoot | kDirtyPcaButton)) {
        mode = epd_mode_t::epd_fast;
    }

    display.waitDisplay();
    presentScreen(mode);
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("[M5GFX_TOUCH_KEY] start");
    Serial.println("[M5GFX_TOUCH_KEY] M5GFX-only EPD test page");

    pinMode(kPinBoot, INPUT_PULLUP);

    if (!display.init_without_reset(false)) {
        Serial.println("[M5GFX_TOUCH_KEY] display init failed");
        while (true) {
            delay(1000);
        }
    }

    g_display_ready = true;
    g_touch_ready = initTouch();

    redrawFullScreen();
    g_dirty = kDirtyNone;

    Serial.printf("[M5GFX_TOUCH_KEY] ready, size=%dx%d\n", display.width(), display.height());
}

void loop()
{
    pollTouch();
    pollKeys();
    refreshDirtyCards();
    delay(kPollPeriodMs);
}
