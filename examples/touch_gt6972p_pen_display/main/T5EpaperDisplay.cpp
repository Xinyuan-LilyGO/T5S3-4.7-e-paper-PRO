// SPDX-License-Identifier: MIT

#include "T5EpaperDisplay.h"

#include <Arduino.h>
#include <Wire.h>

#include <cstring>

#include "driver/gpio.h"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"

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
constexpr uint8_t kPcaPinTpsPwrUp = 11;
constexpr uint8_t kPcaPinVcomCtrl = 12;
constexpr uint8_t kPcaPinTpsWakeUp = 13;
constexpr uint8_t kPcaPinTpsPowerGood = 14;

constexpr gpio_num_t kPinTouchRst = GPIO_NUM_9;
constexpr gpio_num_t kPinEpdCkh = GPIO_NUM_4;
constexpr gpio_num_t kPinEpdD0 = GPIO_NUM_5;
constexpr gpio_num_t kPinEpdD1 = GPIO_NUM_6;
constexpr gpio_num_t kPinEpdD2 = GPIO_NUM_7;
constexpr gpio_num_t kPinEpdD7 = GPIO_NUM_8;
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

uint8_t pca_output[2] = {0xFF, 0x00};

bool i2cWriteBytes(uint8_t address, const uint8_t *data, size_t length)
{
    Wire.beginTransmission(address);
    const size_t written = Wire.write(data, length);
    return written == length && Wire.endTransmission() == 0;
}

bool i2cWriteRegister(uint8_t address, uint8_t reg, uint8_t value)
{
    const uint8_t data[2] = {reg, value};
    return i2cWriteBytes(address, data, sizeof(data));
}

bool i2cReadRegister(uint8_t address, uint8_t reg, uint8_t *data,
                     size_t length)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(static_cast<int>(address),
                         static_cast<int>(length)) != length) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        data[i] = static_cast<uint8_t>(Wire.read());
    }
    return true;
}

bool pca9535Init()
{
    pca_output[0] = 0xFF;
    pca_output[1] = 0x00;

    return i2cWriteRegister(kPca9535Address, kPcaOutputPort0, pca_output[0]) &&
           i2cWriteRegister(kPca9535Address, kPcaOutputPort0 + 1,
                            pca_output[1]) &&
           i2cWriteRegister(kPca9535Address, kPcaConfigPort0, 0x00) &&
           i2cWriteRegister(kPca9535Address, kPcaConfigPort0 + 1, 0xC4);
}

bool pca9535SetLevel(uint8_t pin, bool level)
{
    const uint8_t port = pin / 8;
    const uint8_t bit = pin & 0x07;
    if (level) {
        pca_output[port] |= static_cast<uint8_t>(1U << bit);
    } else {
        pca_output[port] &= static_cast<uint8_t>(~(1U << bit));
    }
    return i2cWriteRegister(kPca9535Address, kPcaOutputPort0 + port,
                            pca_output[port]);
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
    memcpy(buffer + 1, data, length);
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
            Serial.println("[GT6972P_PEN] PCA9535 initialization failed");
            return false;
        }
        return lgfx::Bus_EPD::init();
    }

    bool powerControl(bool power_on) override
    {
        if (_pwr_on == power_on) {
            return true;
        }
        wait();
        const bool ok = power_on ? powerOnSequence() : powerOffSequence();
        if (ok) {
            _pwr_on = power_on;
        } else {
            Serial.printf("[GT6972P_PEN] display power %s failed\n",
                          power_on ? "on" : "off");
        }
        return ok;
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
        if (!pca9535SetLevel(kPcaPinEpdOe, true) ||
            !pca9535SetLevel(kPcaPinEpdMode, true) ||
            !pca9535SetLevel(kPcaPinTpsWakeUp, true) ||
            !pca9535SetLevel(kPcaPinTpsPwrUp, true) ||
            !pca9535SetLevel(kPcaPinVcomCtrl, true)) {
            return false;
        }
        delay(1);
        if (!waitPcaPowerGood() ||
            !tpsWriteRegisterU8(kTpsRegEnable, kTpsEnableAllRails)) {
            return false;
        }

        const uint16_t vcom = static_cast<uint16_t>(kVcomMillivolts / 10);
        const uint8_t vcom_data[2] = {
            static_cast<uint8_t>(vcom),
            static_cast<uint8_t>(vcom >> 8),
        };
        return tpsWriteRegister(kTpsRegVcom, vcom_data, sizeof(vcom_data)) &&
               waitTpsPowerGood();
    }

    bool powerOffSequence()
    {
        if (!pca9535SetLevel(kPcaPinEpdOe, false) ||
            !pca9535SetLevel(kPcaPinEpdMode, false) ||
            !pca9535SetLevel(kPcaPinTpsPwrUp, false) ||
            !pca9535SetLevel(kPcaPinVcomCtrl, false)) {
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
        auto bus_config = bus_.config();
        bus_config.bus_speed = kEpdBusSpeedHz;
        bus_config.pin_data[0] = kPinEpdD0;
        bus_config.pin_data[1] = kPinEpdD1;
        bus_config.pin_data[2] = kPinEpdD2;
        bus_config.pin_data[3] = kPinEpdD3;
        bus_config.pin_data[4] = kPinEpdD4;
        bus_config.pin_data[5] = kPinEpdD5;
        bus_config.pin_data[6] = kPinEpdD6;
        bus_config.pin_data[7] = kPinEpdD7;
        bus_config.pin_pwr = kPinDummyBus;
        bus_config.pin_spv = kPinEpdStv;
        bus_config.pin_ckv = kPinEpdCkv;
        bus_config.pin_sph = kPinEpdSth;
        bus_config.pin_oe = kPinDummyBus;
        bus_config.pin_le = kPinEpdLe;
        bus_config.pin_cl = kPinEpdCkh;
        bus_config.bus_width = 8;
        bus_.config(bus_config);

        panel_.setBus(&bus_);
        auto panel_config = panel_.config();
        panel_config.memory_width = kPanelWidth;
        panel_config.memory_height = kPanelHeight;
        panel_config.panel_width = kPanelWidth;
        panel_config.panel_height = kPanelHeight;
        panel_config.offset_x = 0;
        panel_config.offset_y = 0;
        panel_config.offset_rotation = kPanelOffsetRotation;
        panel_config.bus_shared = false;
        panel_.config(panel_config);

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

LilyGoT5ProM5GFX epaper;

} // namespace

namespace t5epd {

bool begin()
{
    return epaper.init_without_reset(false);
}

lgfx::LGFX_Device &display()
{
    return epaper;
}

} // namespace t5epd
