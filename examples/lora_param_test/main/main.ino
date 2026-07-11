/*
  LoRa send/receive test with on-screen parameter adjustment.
  Screen: M5GFX EPD (LilyGo T5 S3 Pro)
  Radio:  SX1262 via RadioLib
  UI:     Touch to select param, BOOT button to cycle value, HOME button to apply & restart.
*/

#include <Arduino.h>
#include <M5GFX.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "ExtensionIOXL9555.hpp"
#include "TouchDrvGT911.hpp"
#include "driver/gpio.h"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"

using lgfx::epd_mode_t;

// ---------------------------------------------------------------------------
// Board pin definitions
// ---------------------------------------------------------------------------
static constexpr int kI2cSda = 39;
static constexpr int kI2cScl = 40;

static constexpr gpio_num_t kPinBoot       = GPIO_NUM_0;
static constexpr gpio_num_t kPinTouchInt   = GPIO_NUM_3;
static constexpr gpio_num_t kPinEpdCkh    = GPIO_NUM_4;
static constexpr gpio_num_t kPinEpdD0     = GPIO_NUM_5;
static constexpr gpio_num_t kPinEpdD1     = GPIO_NUM_6;
static constexpr gpio_num_t kPinEpdD2     = GPIO_NUM_7;
static constexpr gpio_num_t kPinEpdD7     = GPIO_NUM_8;
static constexpr gpio_num_t kPinTouchRst  = GPIO_NUM_9;
static constexpr gpio_num_t kPinBacklight = GPIO_NUM_11;
static constexpr gpio_num_t kPinEpdD3     = GPIO_NUM_15;
static constexpr gpio_num_t kPinEpdD4     = GPIO_NUM_16;
static constexpr gpio_num_t kPinEpdD5     = GPIO_NUM_17;
static constexpr gpio_num_t kPinEpdD6     = GPIO_NUM_18;
// GPIO43 feeds GPS_RX only — safe placeholder for Bus_EPD dummy slots while LoRa is active.
// Avoid GPIO1 here: it is LoRa reset (kLoraRst).
static constexpr gpio_num_t kPinDummyBus  = GPIO_NUM_43;
static constexpr gpio_num_t kPinEpdSth    = GPIO_NUM_41;
static constexpr gpio_num_t kPinEpdLe     = GPIO_NUM_42;
static constexpr gpio_num_t kPinEpdStv    = GPIO_NUM_45;
static constexpr gpio_num_t kPinEpdCkv    = GPIO_NUM_48;

static constexpr int  kPanelWidth  = 960;
static constexpr int  kPanelHeight = 540;
static constexpr int  kEpdBusSpeedHz = 20000000;
static constexpr int  kVcomMillivolts = 1560;
static constexpr uint8_t kPanelOffsetRotation = 3;
static constexpr int  kPowerGoodTimeoutMs = 400;

// PCA9535 (I/O expander)
static constexpr uint8_t kPca9535Address   = 0x20;
static constexpr uint8_t kPcaInputPort0    = 0x00;
static constexpr uint8_t kPcaOutputPort0   = 0x02;
static constexpr uint8_t kPcaConfigPort0   = 0x06;
static constexpr uint8_t kPcaPinEpdOe      = 8;
static constexpr uint8_t kPcaPinEpdMode    = 9;
static constexpr uint8_t kPcaPinTpsPwrUp   = 11;
static constexpr uint8_t kPcaPinVcomCtrl   = 12;
static constexpr uint8_t kPcaPinTpsWakeUp  = 13;
static constexpr uint8_t kPcaPinTpsPowerGood = 14;

// TPS65185 (EPD power)
static constexpr uint8_t kTps651851Address   = 0x68;
static constexpr uint8_t kTpsRegEnable       = 0x01;
static constexpr uint8_t kTpsRegVcom         = 0x03;
static constexpr uint8_t kTpsRegPowerGood    = 0x0F;
static constexpr uint8_t kTpsEnableAllRails  = 0x3F;
static constexpr uint8_t kTpsPowerGoodMask   = 0xFA;
static constexpr uint8_t kTpsPowerGoodExpected = 0xFA;

// SX1262 SPI pins
static constexpr int kLoraSck  = 14;
static constexpr int kLoraMiso = 21;
static constexpr int kLoraMosi = 13;
static constexpr int kLoraCs   = 46;
static constexpr int kLoraIrq  = 10;
static constexpr int kLoraRst  = 1;
static constexpr int kLoraBusy = 47;
static constexpr int kSdCs     = 12;

// XL9555 LoRa power (same chip used in sx1262 examples)
static constexpr uint8_t kXl9555Address = 0x20;

// ---------------------------------------------------------------------------
// LoRa parameter tables
// ---------------------------------------------------------------------------
static const float kFreqOptions[]  = {433.0f, 470.0f, 868.0f, 915.0f};
static const float kBwOptions[]    = {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f};
static const uint8_t kSfOptions[]  = {5, 6, 7, 8, 9, 10, 11, 12};
static const uint8_t kCrOptions[]  = {5, 6, 7, 8};
static const uint8_t kSwOptions[]  = {0x12, 0x34, 0x6B, 0xAB, 0xCD};
static const int8_t  kTpOptions[]  = {-9, -3, 0, 3, 7, 10, 14, 17, 20, 22};
static const uint16_t kPlOptions[] = {6, 8, 12, 15, 20, 32};

static constexpr uint8_t kFreqCount = sizeof(kFreqOptions) / sizeof(kFreqOptions[0]);
static constexpr uint8_t kBwCount   = sizeof(kBwOptions)   / sizeof(kBwOptions[0]);
static constexpr uint8_t kSfCount   = sizeof(kSfOptions)   / sizeof(kSfOptions[0]);
static constexpr uint8_t kCrCount   = sizeof(kCrOptions)   / sizeof(kCrOptions[0]);
static constexpr uint8_t kSwCount   = sizeof(kSwOptions)   / sizeof(kSwOptions[0]);
static constexpr uint8_t kTpCount   = sizeof(kTpOptions)   / sizeof(kTpOptions[0]);
static constexpr uint8_t kPlCount   = sizeof(kPlOptions)   / sizeof(kPlOptions[0]);

struct LoRaParams {
    uint8_t freqIdx = 3;   // 915 MHz
    uint8_t bwIdx   = 9;   // 500 kHz
    uint8_t sfIdx   = 2;   // SF7
    uint8_t crIdx   = 1;   // CR6
    uint8_t swIdx   = 0;   // 0x12
    uint8_t tpIdx   = 4;   // 7 dBm
    uint8_t plIdx   = 1;   // 8
    bool    crc     = false;
    bool    iq      = false;
    bool    cw      = false;

    float    freq() const { return kFreqOptions[freqIdx]; }
    float    bw()   const { return kBwOptions[bwIdx]; }
    uint8_t  sf()   const { return kSfOptions[sfIdx]; }
    uint8_t  cr()   const { return kCrOptions[crIdx]; }
    uint8_t  sw()   const { return kSwOptions[swIdx]; }
    int8_t   tp()   const { return kTpOptions[tpIdx]; }
    uint16_t pl()   const { return kPlOptions[plIdx]; }
};

// ---------------------------------------------------------------------------
// Application mode
// ---------------------------------------------------------------------------
enum class Mode { TX, RX };

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static LoRaParams g_params;
static Mode       g_mode       = Mode::TX;
static int        g_selectedParam = 0;   // 0-9: freq,bw,sf,cr,sw,tp,pl,crc,iq,cw
static bool       g_radioReady = false;
static bool       g_needRedraw = true;
static bool       g_paramsChanged = false;
static uint32_t   g_txCount   = 0;
static uint32_t   g_rxCount   = 0;
static float      g_lastRssi  = 0.0f;
static float      g_lastSnr   = 0.0f;
static String     g_lastData;
static uint32_t   g_lastTxMs  = 0;
static constexpr uint32_t kTxIntervalMs = 2000;

static volatile bool g_txDone  = false;
static volatile bool g_rxDone  = false;
static int           g_txState = RADIOLIB_ERR_NONE;

// touch state
static int16_t  g_touchX = 0;
static int16_t  g_touchY = 0;
static bool     g_touched = false;
static uint32_t g_lastTouchMs = 0;
static constexpr uint32_t kTouchDebounceMs = 300;

// BOOT button state
static bool     g_bootWas    = false;
static uint32_t g_bootPressMs = 0;
static constexpr uint32_t kBootDebounceMs = 150;
static constexpr uint32_t kBootLongPressMs = 600;

// UI layout
static constexpr int kMargin = 16;
static constexpr int kGap    = 10;

// ---------------------------------------------------------------------------
// I2C helpers
// ---------------------------------------------------------------------------
static uint8_t g_pcaOutput[2] = {0xFF, 0x00};

static bool i2cWriteBytes(uint8_t addr, const uint8_t *data, size_t len)
{
    Wire.beginTransmission(addr);
    Wire.write(data, len);
    return Wire.endTransmission() == 0;
}

static bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = {reg, val};
    return i2cWriteBytes(addr, buf, 2);
}

static bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    return Wire.requestFrom((int)addr, (int)len) == (int)len &&
           [&]() { for (size_t i = 0; i < len; ++i) buf[i] = Wire.read(); return true; }();
}

// ---------------------------------------------------------------------------
// PCA9535 helpers
// ---------------------------------------------------------------------------
static bool pcaInit()
{
    g_pcaOutput[0] = 0xFF;
    g_pcaOutput[1] = 0x00;
    return i2cWriteReg(kPca9535Address, kPcaOutputPort0 + 0, g_pcaOutput[0]) &&
           i2cWriteReg(kPca9535Address, kPcaOutputPort0 + 1, g_pcaOutput[1]) &&
           i2cWriteReg(kPca9535Address, kPcaConfigPort0 + 0, 0x00) &&
           i2cWriteReg(kPca9535Address, kPcaConfigPort0 + 1, 0xC4);
}

static bool pcaSetLevel(uint8_t pin, bool level)
{
    const uint8_t port = pin / 8;
    const uint8_t bit  = pin & 0x07;
    if (level) g_pcaOutput[port] |=  (uint8_t)(1U << bit);
    else       g_pcaOutput[port] &= ~(uint8_t)(1U << bit);
    return i2cWriteReg(kPca9535Address, kPcaOutputPort0 + port, g_pcaOutput[port]);
}

static bool pcaGetLevel(uint8_t pin, bool &level)
{
    const uint8_t port = pin / 8;
    const uint8_t bit  = pin & 0x07;
    uint8_t val = 0;
    if (!i2cReadReg(kPca9535Address, kPcaInputPort0 + port, &val, 1)) return false;
    level = (val & (1U << bit)) != 0;
    return true;
}

// ---------------------------------------------------------------------------
// TPS65185 helpers
// ---------------------------------------------------------------------------
static bool tpsWriteReg(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[4] = {reg, 0, 0, 0};
    if (len > 3) return false;
    for (size_t i = 0; i < len; ++i) buf[i + 1] = data[i];
    return i2cWriteBytes(kTps651851Address, buf, len + 1);
}

static bool tpsWriteU8(uint8_t reg, uint8_t val) { return tpsWriteReg(reg, &val, 1); }

static bool tpsReadU8(uint8_t reg, uint8_t &val)
{
    return i2cReadReg(kTps651851Address, reg, &val, 1);
}

static bool waitPcaPowerGood()
{
    for (int i = 0; i < kPowerGoodTimeoutMs; ++i) {
        bool lv = false;
        if (pcaGetLevel(kPcaPinTpsPowerGood, lv) && lv) return true;
        delay(1);
    }
    return false;
}

static bool waitTpsPowerGood()
{
    for (int i = 0; i < kPowerGoodTimeoutMs; ++i) {
        uint8_t v = 0;
        if (tpsReadU8(kTpsRegPowerGood, v) && (v & kTpsPowerGoodMask) == kTpsPowerGoodExpected)
            return true;
        delay(1);
    }
    return false;
}

// ---------------------------------------------------------------------------
// EPD bus & panel
// ---------------------------------------------------------------------------
class LilyGoEpdBus : public lgfx::Bus_EPD {
public:
    bool init() override
    {
        Wire.begin(kI2cSda, kI2cScl);
        Wire.setClock(400000);
        pinMode(kPinBacklight, OUTPUT);
        digitalWrite(kPinBacklight, LOW);
        pinMode(kPinTouchRst, OUTPUT);
        digitalWrite(kPinTouchRst, HIGH);
        if (!pcaInit()) { Serial.println("[LORA_TEST] PCA9535 init failed"); return false; }
        return lgfx::Bus_EPD::init();
    }

    bool powerControl(bool on) override
    {
        if (_pwr_on == on) return true;
        wait();
        const bool ok = on ? powerOn() : powerOff();
        if (ok) _pwr_on = on;
        return ok;
    }

private:
    bool powerOn()
    {
        pcaSetLevel(kPcaPinEpdOe, true);
        pcaSetLevel(kPcaPinEpdMode, true);
        pcaSetLevel(kPcaPinTpsWakeUp, true);
        pcaSetLevel(kPcaPinTpsPwrUp, true);
        pcaSetLevel(kPcaPinVcomCtrl, true);
        delay(1);
        if (!waitPcaPowerGood()) return false;
        if (!tpsWriteU8(kTpsRegEnable, kTpsEnableAllRails)) return false;
        const uint16_t vcom = kVcomMillivolts / 10;
        const uint8_t vd[2] = {(uint8_t)(vcom & 0xFF), (uint8_t)(vcom >> 8)};
        if (!tpsWriteReg(kTpsRegVcom, vd, 2)) return false;
        return waitTpsPowerGood();
    }

    bool powerOff()
    {
        pcaSetLevel(kPcaPinEpdOe, false);
        pcaSetLevel(kPcaPinEpdMode, false);
        pcaSetLevel(kPcaPinTpsPwrUp, false);
        pcaSetLevel(kPcaPinVcomCtrl, false);
        delay(1);
        return pcaSetLevel(kPcaPinTpsWakeUp, false);
    }
};

class LilyGoDisplay : public lgfx::LGFX_Device {
public:
    LilyGoDisplay()
    {
        auto bc = bus_.config();
        bc.bus_speed = kEpdBusSpeedHz;
        bc.pin_data[0] = kPinEpdD0; bc.pin_data[1] = kPinEpdD1;
        bc.pin_data[2] = kPinEpdD2; bc.pin_data[3] = kPinEpdD3;
        bc.pin_data[4] = kPinEpdD4; bc.pin_data[5] = kPinEpdD5;
        bc.pin_data[6] = kPinEpdD6; bc.pin_data[7] = kPinEpdD7;
        bc.pin_pwr = kPinDummyBus;  bc.pin_spv = kPinEpdStv;
        bc.pin_ckv = kPinEpdCkv;    bc.pin_sph = kPinEpdSth;
        bc.pin_oe  = kPinDummyBus;  bc.pin_le  = kPinEpdLe;
        bc.pin_cl  = kPinEpdCkh;    bc.bus_width = 8;
        bus_.config(bc);
        panel_.setBus(&bus_);

        auto pc = panel_.config();
        pc.memory_width  = kPanelWidth;  pc.memory_height  = kPanelHeight;
        pc.panel_width   = kPanelWidth;  pc.panel_height   = kPanelHeight;
        pc.offset_x = 0; pc.offset_y = 0;
        pc.offset_rotation = kPanelOffsetRotation;
        pc.bus_shared = false;
        panel_.config(pc);

        auto det = panel_.config_detail();
        det.line_padding = 0; det.task_priority = 3;
        panel_.config_detail(det);
        setPanel(&panel_);
    }

private:
    LilyGoEpdBus   bus_;
    lgfx::Panel_EPD panel_;
};

static LilyGoDisplay display;
static TouchDrvGT911 touch;
static bool g_touchReady = false;

// ---------------------------------------------------------------------------
// SX1262 radio (RadioLib)
// ---------------------------------------------------------------------------
static SX1262 radio = new Module(kLoraCs, kLoraIrq, kLoraRst, kLoraBusy);

static void IRAM_ATTR onTxDone() { g_txDone = true; }
static void IRAM_ATTR onRxDone() { g_rxDone = true; }

// Power on LoRa via PCA9535 IO0 (same expander already init'd by EPD bus).
// XL9555 and PCA9535 share address 0x20; reuse the pcaSetLevel helper.
static void loraPowerOn()
{
    pcaSetLevel(0, true);   // IO0 = LoRa power enable
    delay(1500);
}

static bool applyLoRaParams()
{
    int err = RADIOLIB_ERR_NONE;
    const LoRaParams &p = g_params;

    if ((err = radio.setFrequency(p.freq())) == RADIOLIB_ERR_INVALID_FREQUENCY) {
        Serial.println("Bad frequency"); return false;
    }
    if ((err = radio.setBandwidth(p.bw())) == RADIOLIB_ERR_INVALID_BANDWIDTH) {
        Serial.println("Bad bandwidth"); return false;
    }
    if ((err = radio.setSpreadingFactor(p.sf())) == RADIOLIB_ERR_INVALID_SPREADING_FACTOR) {
        Serial.println("Bad SF"); return false;
    }
    if ((err = radio.setCodingRate(p.cr())) == RADIOLIB_ERR_INVALID_CODING_RATE) {
        Serial.println("Bad CR"); return false;
    }
    if (radio.setSyncWord(p.sw()) != RADIOLIB_ERR_NONE) {
        Serial.println("Bad sync word"); return false;
    }
    if ((err = radio.setOutputPower(p.tp())) == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
        Serial.println("Bad TX power"); return false;
    }
    if ((err = radio.setPreambleLength(p.pl())) == RADIOLIB_ERR_INVALID_PREAMBLE_LENGTH) {
        Serial.println("Bad preamble"); return false;
    }
    if ((err = radio.setCRC(p.crc)) == RADIOLIB_ERR_INVALID_CRC_CONFIGURATION) {
        Serial.println("Bad CRC"); return false;
    }
    if ((err = radio.invertIQ(p.iq)) != RADIOLIB_ERR_NONE) {
        Serial.println("Bad IQ"); return false;
    }
    if (radio.setTCXO(3.0f) == RADIOLIB_ERR_INVALID_TCXO_VOLTAGE) {
        Serial.println("Bad TCXO"); return false;
    }
    if (radio.setDio2AsRfSwitch() != RADIOLIB_ERR_NONE) {
        Serial.println("Bad DIO2"); return false;
    }
    return true;
}

static bool initRadio()
{
    pinMode(kLoraCs, OUTPUT); digitalWrite(kLoraCs, HIGH);
    pinMode(kSdCs,   OUTPUT); digitalWrite(kSdCs,   HIGH);
    SPI.begin(kLoraSck, kLoraMiso, kLoraMosi);

    loraPowerOn();

    int state = radio.begin(g_params.freq());
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[LORA_TEST] radio.begin failed: %d\n", state);
        return false;
    }
    if (!applyLoRaParams()) return false;

    if (g_params.cw) {
        radio.transmitDirect();
        return true;
    }

    if (g_mode == Mode::TX) {
        radio.setPacketSentAction(onTxDone);
    } else {
        radio.setPacketReceivedAction(onRxDone);
        state = radio.startReceive();
        if (state != RADIOLIB_ERR_NONE) {
            Serial.printf("[LORA_TEST] startReceive failed: %d\n", state);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// UI helpers
// ---------------------------------------------------------------------------
static uint32_t gray(uint8_t v) { return display.color888(v, v, v); }

static void presentEpd(epd_mode_t mode)
{
    display.powerSaveOff();
    display.setEpdMode(mode);
    display.display();
    display.waitDisplay();
    display.powerSaveOn();
}

// Parameter names and value strings
static const char *kParamNames[] = {
    "Freq(MHz)", "BW(kHz)", "SF", "CR", "SyncWord",
    "TXpwr(dBm)", "Preamble", "CRC", "IQ inv", "ContWave"
};
static constexpr int kParamCount = 10;

static void paramValueStr(int idx, char *buf, size_t sz)
{
    const LoRaParams &p = g_params;
    switch (idx) {
        case 0: snprintf(buf, sz, "%.1f", p.freq()); break;
        case 1: snprintf(buf, sz, "%.2f", p.bw());   break;
        case 2: snprintf(buf, sz, "%d",   (int)p.sf()); break;
        case 3: snprintf(buf, sz, "%d",   (int)p.cr()); break;
        case 4: snprintf(buf, sz, "0x%02X", p.sw());  break;
        case 5: snprintf(buf, sz, "%d",   (int)p.tp()); break;
        case 6: snprintf(buf, sz, "%d",   (int)p.pl()); break;
        case 7: snprintf(buf, sz, "%s",   p.crc ? "ON" : "OFF"); break;
        case 8: snprintf(buf, sz, "%s",   p.iq  ? "ON" : "OFF"); break;
        case 9: snprintf(buf, sz, "%s",   p.cw  ? "ON" : "OFF"); break;
        default: buf[0] = '\0';
    }
}

static void cycleParam(int idx)
{
    LoRaParams &p = g_params;
    switch (idx) {
        case 0: p.freqIdx = (p.freqIdx + 1) % kFreqCount; break;
        case 1: p.bwIdx   = (p.bwIdx   + 1) % kBwCount;   break;
        case 2: p.sfIdx   = (p.sfIdx   + 1) % kSfCount;   break;
        case 3: p.crIdx   = (p.crIdx   + 1) % kCrCount;   break;
        case 4: p.swIdx   = (p.swIdx   + 1) % kSwCount;   break;
        case 5: p.tpIdx   = (p.tpIdx   + 1) % kTpCount;   break;
        case 6: p.plIdx   = (p.plIdx   + 1) % kPlCount;   break;
        case 7: p.crc = !p.crc; break;
        case 8: p.iq  = !p.iq;  break;
        case 9: p.cw  = !p.cw;  break;
    }
}

// ---------------------------------------------------------------------------
// Screen layout (landscape 540x960 logical after offset_rotation=3)
// Display width()=540, height()=960 when rotation=0 + offset_rotation=3
// ---------------------------------------------------------------------------
static void drawScreen()
{
    const int W = display.width();   // 540
    const int H = display.height();  // 960

    display.fillScreen(TFT_WHITE);
    display.setTextWrap(false);

    // --- Title bar ---
    const int titleH = 56;
    display.fillRoundRect(kMargin, kMargin, W - kMargin * 2, titleH, 12, gray(230));
    display.drawRoundRect(kMargin, kMargin, W - kMargin * 2, titleH, 12, TFT_BLACK);
    display.setFont(&fonts::Font4);
    display.setTextColor(TFT_BLACK, gray(230));
    display.setTextDatum(textdatum_t::middle_left);
    display.drawString("LoRa Param Test", kMargin + 14, kMargin + titleH / 2);

    // Mode badge
    const char *modeTxt = (g_mode == Mode::TX) ? "TX" : "RX";
    const uint32_t modeBg = (g_mode == Mode::TX) ? TFT_BLACK : gray(60);
    const int badgeW = 64, badgeH = 34;
    const int badgeX = W - kMargin - badgeW - 4;
    const int badgeY = kMargin + (titleH - badgeH) / 2;
    display.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 8, modeBg);
    display.setFont(&fonts::Font4);
    display.setTextColor(TFT_WHITE, modeBg);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString(modeTxt, badgeX + badgeW / 2, badgeY + badgeH / 2);

    int y = kMargin + titleH + kGap;

    // --- Parameter grid ---
    // 2 columns
    const int colW = (W - kMargin * 2 - kGap) / 2;
    const int rowH = 58;
    const int cols = 2;

    for (int i = 0; i < kParamCount; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const int px = kMargin + col * (colW + kGap);
        const int py = y + row * (rowH + kGap / 2);
        const bool sel = (i == g_selectedParam);

        const uint32_t bg  = sel ? TFT_BLACK  : gray(242);
        const uint32_t fg  = sel ? TFT_WHITE  : TFT_BLACK;
        const uint32_t bdr = sel ? TFT_BLACK  : gray(160);

        display.fillRoundRect(px, py, colW, rowH, 10, bg);
        display.drawRoundRect(px, py, colW, rowH, 10, bdr);

        display.setFont(&fonts::Font2);
        display.setTextColor(sel ? gray(180) : gray(100), bg);
        display.setTextDatum(textdatum_t::top_left);
        display.drawString(kParamNames[i], px + 10, py + 6);

        char vbuf[16];
        paramValueStr(i, vbuf, sizeof(vbuf));
        display.setFont(&fonts::Font4);
        display.setTextColor(fg, bg);
        display.setTextDatum(textdatum_t::bottom_right);
        display.drawString(vbuf, px + colW - 10, py + rowH - 6);
    }

    const int gridRows = (kParamCount + cols - 1) / cols;
    y += gridRows * (rowH + kGap / 2) + kGap;

    // --- Stats panel ---
    const int statsH = 130;
    display.fillRoundRect(kMargin, y, W - kMargin * 2, statsH, 12, gray(246));
    display.drawRoundRect(kMargin, y, W - kMargin * 2, statsH, 12, TFT_BLACK);
    display.drawFastHLine(kMargin + 12, y + 32, W - kMargin * 2 - 24, TFT_BLACK);
    display.setFont(&fonts::Font2);
    display.setTextColor(TFT_BLACK, gray(246));
    display.setTextDatum(textdatum_t::top_left);
    display.drawString("Stats", kMargin + 12, y + 8);

    char sbuf[80];
    display.setFont(&fonts::Font2);
    if (g_mode == Mode::TX) {
        snprintf(sbuf, sizeof(sbuf), "TX count: %lu   Last TX: %lums ago",
                 (unsigned long)g_txCount,
                 (unsigned long)(millis() - g_lastTxMs));
        display.drawString(sbuf, kMargin + 12, y + 40);
    } else {
        snprintf(sbuf, sizeof(sbuf), "RX count: %lu", (unsigned long)g_rxCount);
        display.drawString(sbuf, kMargin + 12, y + 40);
        snprintf(sbuf, sizeof(sbuf), "RSSI: %.1f dBm   SNR: %.1f dB", g_lastRssi, g_lastSnr);
        display.drawString(sbuf, kMargin + 12, y + 62);
        if (g_lastData.length()) {
            snprintf(sbuf, sizeof(sbuf), "Last: %s", g_lastData.c_str());
            display.drawString(sbuf, kMargin + 12, y + 84);
        }
    }

    if (!g_radioReady) {
        display.setFont(&fonts::Font2);
        display.setTextColor(TFT_BLACK, gray(246));
        display.drawString("Radio: INIT FAILED", kMargin + 12, y + 106);
    }

    y += statsH + kGap;

    // --- Help bar ---
    const int helpH = 48;
    if (y + helpH + kMargin <= H) {
        display.fillRoundRect(kMargin, y, W - kMargin * 2, helpH, 10, gray(236));
        display.drawRoundRect(kMargin, y, W - kMargin * 2, helpH, 10, TFT_BLACK);
        display.setFont(&fonts::Font2);
        display.setTextColor(gray(60), gray(236));
        display.setTextDatum(textdatum_t::middle_left);
        display.drawString("Touch: select param  |  BOOT short: cycle value  |  BOOT long: apply+restart  |  HOME: TX<->RX",
                           kMargin + 12, y + helpH / 2);
    }
}

static void fullRefresh()
{
    display.setAutoDisplay(false);
    display.setColorDepth(4);
    display.setRotation(0);
    display.setEpdMode(epd_mode_t::epd_quality);
    display.startWrite();
    drawScreen();
    display.endWrite();
    presentEpd(epd_mode_t::epd_quality);
}

static void fastRefresh()
{
    display.setAutoDisplay(false);
    display.startWrite();
    drawScreen();
    display.endWrite();
    presentEpd(epd_mode_t::epd_text);
}

// ---------------------------------------------------------------------------
// Touch handling - map touch to parameter cell
// ---------------------------------------------------------------------------
static int touchToParamIdx(int16_t tx, int16_t ty)
{
    const int W = display.width();
    const int titleH = 56;
    const int rowH = 58;
    const int colW = (W - kMargin * 2 - kGap) / 2;
    const int cols = 2;
    const int gridY = kMargin + titleH + kGap;

    for (int i = 0; i < kParamCount; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const int px = kMargin + col * (colW + kGap);
        const int py = gridY + row * (rowH + kGap / 2);
        if (tx >= px && tx < px + colW && ty >= py && ty < py + rowH)
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Radio restart after param change
// ---------------------------------------------------------------------------
static void restartRadio()
{
    radio.reset();
    delay(100);
    g_txDone = false;
    g_rxDone = false;
    g_radioReady = initRadio();
    if (g_radioReady && g_mode == Mode::TX && !g_params.cw) {
        String msg = String("PKT#") + g_txCount;
        g_txState = radio.startTransmit(msg);
    }
    g_needRedraw = true;
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("[LORA_TEST] start");

    pinMode(kPinBoot, INPUT_PULLUP);

    if (!display.init_without_reset(false)) {
        Serial.println("[LORA_TEST] display init failed");
        while (true) delay(1000);
    }

    touch.setPins(kPinTouchRst, kPinTouchInt);
    g_touchReady = touch.begin(Wire, GT911_SLAVE_ADDRESS_L, kI2cSda, kI2cScl);
    if (g_touchReady) {
        touch.setHomeButtonCallback([](void *) {
            // HOME toggles TX/RX
            g_mode = (g_mode == Mode::TX) ? Mode::RX : Mode::TX;
            g_paramsChanged = true;
        }, nullptr);
        touch.setInterruptMode(LOW_LEVEL_QUERY);
        touch.setMaxCoordinates(display.width() - 1, display.height() - 1);
    }

    g_radioReady = initRadio();

    fullRefresh();
    g_needRedraw = false;

    Serial.printf("[LORA_TEST] ready %dx%d radio=%d\n",
                  display.width(), display.height(), (int)g_radioReady);
}

void loop()
{
    const uint32_t now = millis();

    // --- Touch poll ---
    if (g_touchReady && touch.isPressed()) {
        int16_t x[1], y[1];
        if (touch.getPoint(x, y, 1) > 0 && (now - g_lastTouchMs) > kTouchDebounceMs) {
            g_lastTouchMs = now;
            const int idx = touchToParamIdx(x[0], y[0]);
            if (idx >= 0 && idx != g_selectedParam) {
                g_selectedParam = idx;
                g_needRedraw = true;
            }
        }
    }

    // --- BOOT button ---
    const bool bootNow = (digitalRead(kPinBoot) == LOW);
    if (bootNow && !g_bootWas) {
        g_bootPressMs = now;
    } else if (!bootNow && g_bootWas) {
        const uint32_t held = now - g_bootPressMs;
        if (held >= kBootLongPressMs) {
            // Long press -> apply params & restart radio
            g_paramsChanged = true;
        } else if (held >= kBootDebounceMs) {
            // Short press -> cycle selected param value
            cycleParam(g_selectedParam);
            g_needRedraw = true;
        }
    }
    g_bootWas = bootNow;

    // --- Apply params ---
    if (g_paramsChanged) {
        g_paramsChanged = false;
        restartRadio();
        fullRefresh();
        g_needRedraw = false;
        return;
    }

    // --- TX logic ---
    if (g_radioReady && g_mode == Mode::TX && !g_params.cw) {
        if (g_txDone) {
            g_txDone = false;
            if (g_txState == RADIOLIB_ERR_NONE) {
                g_txCount++;
                Serial.printf("[LORA_TEST] TX #%lu ok\n", (unsigned long)g_txCount);
            } else {
                Serial.printf("[LORA_TEST] TX err %d\n", g_txState);
            }
            radio.finishTransmit();
            g_needRedraw = true;
        }
        if ((now - g_lastTxMs) >= kTxIntervalMs && !g_txDone) {
            g_lastTxMs = now;
            String msg = String("PKT#") + g_txCount;
            g_txState = radio.startTransmit(msg);
        }
    }

    // --- RX logic ---
    if (g_radioReady && g_mode == Mode::RX && !g_params.cw) {
        if (g_rxDone) {
            g_rxDone = false;
            String data;
            const int state = radio.readData(data);
            if (state == RADIOLIB_ERR_NONE) {
                g_rxCount++;
                g_lastRssi = radio.getRSSI();
                g_lastSnr  = radio.getSNR();
                g_lastData = data;
                Serial.printf("[LORA_TEST] RX #%lu: %s  RSSI=%.1f SNR=%.1f\n",
                              (unsigned long)g_rxCount,
                              data.c_str(), g_lastRssi, g_lastSnr);
            } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
                Serial.println("[LORA_TEST] CRC mismatch");
            } else {
                Serial.printf("[LORA_TEST] RX err %d\n", state);
            }
            radio.startReceive();
            g_needRedraw = true;
        }
    }

    // --- Periodic stats refresh ---
    static uint32_t lastStatMs = 0;
    if (g_mode == Mode::TX && (now - lastStatMs) >= 3000) {
        lastStatMs = now;
        g_needRedraw = true;
    }

    if (g_needRedraw) {
        g_needRedraw = false;
        fastRefresh();
    }

    delay(40);
}
