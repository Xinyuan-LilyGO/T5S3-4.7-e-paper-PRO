#include <Arduino.h>
#include <M5GFX.h>
#include <Wire.h>

#include <algorithm>
#include <cstdint>

#include "driver/gpio.h"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"

using lgfx::epd_mode_t;

namespace {

constexpr char kTag[] = "m5gfx_display_test";

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
constexpr uint8_t kPcaPinTpsPwrUp = 11;
constexpr uint8_t kPcaPinVcomCtrl = 12;
constexpr uint8_t kPcaPinTpsWakeUp = 13;
constexpr uint8_t kPcaPinTpsPowerGood = 14;

constexpr gpio_num_t kPinEpdCkh = GPIO_NUM_4;
constexpr gpio_num_t kPinEpdD0 = GPIO_NUM_5;
constexpr gpio_num_t kPinEpdD1 = GPIO_NUM_6;
constexpr gpio_num_t kPinEpdD2 = GPIO_NUM_7;
constexpr gpio_num_t kPinEpdD7 = GPIO_NUM_8;
constexpr gpio_num_t kPinEpdTouchRst = GPIO_NUM_9;
constexpr gpio_num_t kPinBacklight = GPIO_NUM_11;
constexpr gpio_num_t kPinEpdD3 = GPIO_NUM_15;
constexpr gpio_num_t kPinEpdD4 = GPIO_NUM_16;
constexpr gpio_num_t kPinEpdD5 = GPIO_NUM_17;
constexpr gpio_num_t kPinEpdD6 = GPIO_NUM_18;
// Bus_EPD requires valid GPIOs for the dummy DC / OE slots.
// GPIO1 drives LoRa reset on this board, and the example never enables the LoRa power rail,
// so it is a safe placeholder that avoids invalid / unavailable ESP32-S3 GPIO22/23.
constexpr gpio_num_t kPinDummyBus = GPIO_NUM_1;
constexpr gpio_num_t kPinEpdSth = GPIO_NUM_41;
constexpr gpio_num_t kPinEpdLe = GPIO_NUM_42;
constexpr gpio_num_t kPinEpdStv = GPIO_NUM_45;
constexpr gpio_num_t kPinEpdCkv = GPIO_NUM_48;

constexpr uint8_t kPanelOffsetRotation = 3;
constexpr int kPowerGoodTimeoutMs = 400;

uint8_t g_pca_output[2] = {0xFF, 0x00};

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
    // Keep port 0 high, matching the stock firmware's idle state for the shared rail.
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
        pinMode(kPinEpdTouchRst, OUTPUT);
        digitalWrite(kPinEpdTouchRst, HIGH);

        if (!pca9535Init()) {
            Serial.println("[M5GFX_EPD_TEST] PCA9535 init failed");
            return false;
        }

        const bool ok = lgfx::Bus_EPD::init();
        if (!ok) {
            Serial.println("[M5GFX_EPD_TEST] Bus_EPD init failed");
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
            Serial.printf("[M5GFX_EPD_TEST] power %s failed\n", power_on ? "on" : "off");
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

void drawCornerLabels()
{
    display.setFont(&fonts::Font2);
    display.setTextColor(TFT_BLACK, TFT_WHITE);

    display.setTextDatum(textdatum_t::top_left);
    display.drawString("TL", 12, 10);
    display.setTextDatum(textdatum_t::top_right);
    display.drawString("TR", display.width() - 12, 10);
    display.setTextDatum(textdatum_t::bottom_left);
    display.drawString("BL", 12, display.height() - 13);
    display.setTextDatum(textdatum_t::bottom_right);
    display.drawString("BR", display.width() - 12, display.height() - 13);
}

uint32_t grayColor(uint8_t gray)
{
    return display.color888(gray, gray, gray);
}

void drawCardFrame(int x, int y, int w, int h, const char *title)
{
    display.fillRoundRect(x, y, w, h, 12, grayColor(246));
    display.drawRoundRect(x, y, w, h, 12, TFT_BLACK);
    display.drawFastHLine(x + 12, y + 30, w - 24, TFT_BLACK);

    display.setFont(&fonts::Font2);
    display.setTextColor(TFT_BLACK, grayColor(246));
    display.setTextDatum(textdatum_t::top_left);
    display.drawString(title, x + 12, y + 8);
}

void drawHeaderBlock(int x, int y, int w, int h)
{
    display.fillRoundRect(x, y, w, h, 16, grayColor(236));
    display.drawRoundRect(x, y, w, h, 16, TFT_BLACK);
    display.drawRoundRect(x + 8, y + 8, w - 16, h - 16, 14, grayColor(180));

    display.setTextColor(TFT_BLACK, grayColor(236));
    display.setTextDatum(textdatum_t::top_center);

    display.setFont(&fonts::Font4);
    display.drawString("T5 S3 Pro EPD Test", x + w / 2, y + 16);

    display.setFont(&fonts::Font2);
    display.drawString("M5GFX only", x + w / 2, y + 58);
    display.drawString("Portrait layout  |  offset_rotation = 3", x + w / 2, y + 82);

    display.setTextDatum(textdatum_t::top_left);
    display.drawString("Ported from T5_P4_E_Paper_V0.2/examples/epd_test", x + 16, y + 110);
}

void drawGraySteps(int x, int y, int w, int h)
{
    const int cols = 4;
    const int rows = 4;
    const int gap = 8;
    const int cell_width = (w - gap * (cols - 1)) / cols;
    const int cell_height = (h - gap * (rows - 1)) / rows;

    for (int i = 0; i < 16; ++i) {
        const int row = i / cols;
        const int col = i % cols;
        const int cell_x = x + col * (cell_width + gap);
        const int cell_y = y + row * (cell_height + gap);
        const uint8_t gray = static_cast<uint8_t>(i * 17);
        const uint32_t color = grayColor(gray);

        display.fillRoundRect(cell_x, cell_y, cell_width, cell_height, 8, color);
        display.drawRoundRect(cell_x, cell_y, cell_width, cell_height, 8, TFT_BLACK);
        display.setTextDatum(textdatum_t::middle_center);
        display.setTextColor(i < 8 ? TFT_WHITE : TFT_BLACK, color);
        display.drawString(String(i), cell_x + cell_width / 2, cell_y + cell_height / 2);
    }
}

void drawCheckerboard(int x, int y, int cell_size, int columns, int rows)
{
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < columns; ++col) {
            const bool dark = ((row + col) & 1) != 0;
            display.fillRect(x + col * cell_size,
                             y + row * cell_size,
                             cell_size,
                             cell_size,
                             dark ? TFT_BLACK : TFT_WHITE);
        }
    }
    display.drawRect(x, y, columns * cell_size, rows * cell_size, TFT_BLACK);
}

void drawAdaptiveCheckerboard(int x, int y, int w, int h)
{
    const int columns = 8;
    const int rows = 8;
    const int cell_size = std::max(8, std::min(w / columns, h / rows));
    const int board_w = cell_size * columns;
    const int board_h = cell_size * rows;
    const int board_x = x + (w - board_w) / 2;
    const int board_y = y + (h - board_h) / 2;
    drawCheckerboard(board_x, board_y, cell_size, columns, rows);
}

void drawShapeDemo(int x, int y, int w, int h)
{
    const int left = x + 14;
    const int right = x + w - 14;
    const int top = y + 18;
    const int bottom = y + h - 18;
    const int mid_x = x + w / 2;
    const int mid_y = y + h / 2 + 8;

    display.drawLine(left, top, right, top + 18, TFT_BLACK);
    display.drawLine(left, top + 36, right - 10, bottom - 18, grayColor(120));
    display.drawRect(left + 6, mid_y - 20, 56, 42, TFT_BLACK);
    display.fillRect(mid_x - 28, mid_y - 20, 56, 42, grayColor(96));
    display.drawRect(mid_x - 28, mid_y - 20, 56, 42, TFT_BLACK);
    display.drawCircle(right - 34, mid_y + 2, 26, TFT_BLACK);
    display.fillCircle(right - 34, mid_y + 2, 14, TFT_BLACK);
}

void drawGraySweep(int x, int y, int w, int h)
{
    const int lines = 12;
    for (int i = 0; i < lines; ++i) {
        const uint8_t gray = static_cast<uint8_t>(30 + i * 18);
        const int y0 = y + h - 18 - i * ((h - 32) / lines);
        const int y1 = y + 18 + i * ((h - 32) / lines);
        display.drawLine(x + 18, y0, x + w - 18, y1, grayColor(gray));
    }
}

void drawTestPattern()
{
    const int width = display.width();
    const int height = display.height();
    const int margin = 18;
    const int gap = 16;
    const int content_width = width - margin * 2;

    display.fillScreen(TFT_WHITE);

    display.setTextWrap(false);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(textdatum_t::top_left);

    int y = margin;

    drawHeaderBlock(margin, y, content_width, 148);
    y += 148 + gap;

    drawCardFrame(margin, y, content_width, 198, "Gray Scale 0 - 15");
    drawGraySteps(margin + 16, y + 42, content_width - 32, 140);
    y += 198 + gap;

    const int dual_width = (content_width - gap) / 2;
    const int dual_height = 214;

    drawCardFrame(margin, y, dual_width, dual_height, "Checkerboard");
    drawAdaptiveCheckerboard(margin + 12, y + 44, dual_width - 24, dual_height - 58);

    drawCardFrame(margin + dual_width + gap, y, dual_width, dual_height, "Shapes");
    drawShapeDemo(margin + dual_width + gap + 8, y + 38, dual_width - 16, dual_height - 50);
    y += dual_height + gap;

    drawCardFrame(margin, y, content_width, 206, "Diagonal Gray Sweep");
    drawGraySweep(margin + 10, y + 36, content_width - 20, 158);
    y += 206 + gap;

    const int footer_height = height - y - margin;
    drawCardFrame(margin, y, content_width, footer_height, "Info");

    display.setFont(&fonts::Font2);
    display.setTextColor(TFT_BLACK, grayColor(246));
    display.setTextDatum(textdatum_t::top_left);
    display.drawString(String("Panel: ") + width + "x" + height, margin + 16, y + 34);
    display.drawString(String("I80 bus: ") + (kEpdBusSpeedHz / 1000000) + " MHz", margin + 16, y + 58);
    display.drawString("Rotation: setRotation(0) + offset_rotation(3)", margin + 16, y + 80);
    display.drawString("If direction is still wrong, adjust kPanelOffsetRotation.", margin + 16, y + 116);

    drawCornerLabels();
}

void renderTestPattern()
{
    display.setAutoDisplay(false);
    display.setColorDepth(4);
    display.setRotation(0);
    display.setEpdMode(epd_mode_t::epd_quality);

    display.startWrite();
    drawTestPattern();
    display.endWrite();

    const uint32_t start = millis();
    display.display();
    display.waitDisplay();
    display.powerSaveOn();

    Serial.printf("[M5GFX_EPD_TEST] refresh finished in %lu ms\n",
                  static_cast<unsigned long>(millis() - start));
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("[M5GFX_EPD_TEST] start");
    Serial.println("[M5GFX_EPD_TEST] pure M5GFX Bus_EPD + Panel_EPD example");

    if (!display.init_without_reset(false)) {
        Serial.println("[M5GFX_EPD_TEST] display init failed");
        while (true) {
            delay(1000);
        }
    }

    Serial.printf("[M5GFX_EPD_TEST] ready, size=%dx%d, offset_rotation=%u\n",
                  display.width(),
                  display.height(),
                  static_cast<unsigned>(kPanelOffsetRotation));

    renderTestPattern();
}

void loop()
{
    delay(1000);
}
