#include <Arduino.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <Wire.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "driver/gpio.h"
#include "lgfx/v1/platforms/esp32/Bus_EPD.h"
#include "lgfx/v1/platforms/esp32/Panel_EPD.hpp"

using lgfx::epd_mode_t;

namespace {

constexpr char kTag[] = "m5gfx_wifi_scan";

constexpr TargetCredential kTargets[] = {
    {"xinyuandianzi", "AA15994823428"},
    {"LilyGo-AABB", "xinyuandianzi"},
};

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
constexpr gpio_num_t kPinTouchRst = GPIO_NUM_9;
constexpr gpio_num_t kPinBacklight = GPIO_NUM_11;
constexpr gpio_num_t kPinEpdD3 = GPIO_NUM_15;
constexpr gpio_num_t kPinEpdD4 = GPIO_NUM_16;
constexpr gpio_num_t kPinEpdD5 = GPIO_NUM_17;
constexpr gpio_num_t kPinEpdD6 = GPIO_NUM_18;
// GPIO1 drives LoRa reset on this board. The example never powers the LoRa rail,
// so it is a safe placeholder for Bus_EPD's required dummy slots.
constexpr gpio_num_t kPinDummyBus = GPIO_NUM_1;
constexpr gpio_num_t kPinEpdSth = GPIO_NUM_41;
constexpr gpio_num_t kPinEpdLe = GPIO_NUM_42;
constexpr gpio_num_t kPinEpdStv = GPIO_NUM_45;
constexpr gpio_num_t kPinEpdCkv = GPIO_NUM_48;

constexpr uint8_t kPanelOffsetRotation = 3;
constexpr int kPowerGoodTimeoutMs = 400;

constexpr uint32_t kRescanIntervalMs = 10000;
constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint8_t kQualityRefreshEvery = 4;

constexpr int kMargin = 18;
constexpr int kGap = 14;
constexpr int kHeaderHeight = 92;
constexpr int kStatusHeight = 168;
constexpr int kTargetsHeight = 110;
constexpr int kCardRadius = 16;
constexpr size_t kMaxVisibleNetworks = 14;

struct TargetCredential {
    const char *ssid;
    const char *password;
};

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

struct ScanEntry {
    String ssid;
    int32_t rssi;
    bool preferred;
};

enum class AppState {
    kBoot,
    kScanning,
    kWaiting,
    kConnecting,
    kConnected,
    kConnectFailed,
};

uint8_t g_pca_output[2] = {0xFF, 0x00};
std::vector<ScanEntry> g_scan_results;
AppState g_app_state = AppState::kBoot;
String g_status_line1 = "Power on";
String g_status_line2 = "Preparing display";
String g_status_line3;
String g_active_ssid;
int32_t g_active_rssi = 0;
IPAddress g_active_ip;
uint32_t g_next_scan_ms = 0;
uint8_t g_text_refresh_count = 0;
bool g_has_scanned_once = false;
bool g_last_scan_failed = false;

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
            Serial.println("[M5GFX_WIFI] PCA9535 init failed");
            return false;
        }

        const bool ok = lgfx::Bus_EPD::init();
        if (!ok) {
            Serial.println("[M5GFX_WIFI] Bus_EPD init failed");
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
            Serial.printf("[M5GFX_WIFI] power %s failed\n", power_on ? "on" : "off");
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

String ellipsize(const String &text, size_t max_chars)
{
    if (text.length() <= max_chars) {
        return text;
    }
    if (max_chars <= 3) {
        return text.substring(0, max_chars);
    }
    return text.substring(0, max_chars - 3) + "...";
}

bool isPreferredSsid(const String &ssid)
{
    for (const auto &target : kTargets) {
        if (ssid == target.ssid) {
            return true;
        }
    }
    return false;
}

const TargetCredential *findTargetCredential(const String &ssid)
{
    for (const auto &target : kTargets) {
        if (ssid == target.ssid) {
            return &target;
        }
    }
    return nullptr;
}

const char *stateTitle(AppState state)
{
    switch (state) {
    case AppState::kBoot:
        return "BOOT";
    case AppState::kScanning:
        return "SCANNING";
    case AppState::kWaiting:
        return "WAITING";
    case AppState::kConnecting:
        return "CONNECTING";
    case AppState::kConnected:
        return "CONNECTED";
    case AppState::kConnectFailed:
        return "FAILED";
    }
    return "UNKNOWN";
}

void setStatus(AppState state, const String &line1, const String &line2 = String(), const String &line3 = String())
{
    g_app_state = state;
    g_status_line1 = line1;
    g_status_line2 = line2;
    g_status_line3 = line3;
}

Rect makeRect(int x, int y, int w, int h)
{
    return {x, y, w, h};
}

void drawCard(const Rect &rect, const char *title, uint8_t fill_gray)
{
    const uint32_t fill = grayColor(fill_gray);

    display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, kCardRadius, fill);
    display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, kCardRadius, TFT_BLACK);
    display.drawFastHLine(rect.x + 14, rect.y + 32, rect.w - 28, TFT_BLACK);

    display.setFont(&fonts::Font2);
    display.setTextColor(TFT_BLACK, fill);
    display.setTextDatum(textdatum_t::top_left);
    display.drawString(title, rect.x + 14, rect.y + 8);
}

void drawHeader(const Rect &rect)
{
    const uint32_t fill = grayColor(236);
    display.fillRoundRect(rect.x, rect.y, rect.w, rect.h, kCardRadius, fill);
    display.drawRoundRect(rect.x, rect.y, rect.w, rect.h, kCardRadius, TFT_BLACK);
    display.drawRoundRect(rect.x + 8, rect.y + 8, rect.w - 16, rect.h - 16, 14, grayColor(184));

    display.setTextColor(TFT_BLACK, fill);
    display.setTextDatum(textdatum_t::top_left);

    display.setFont(&fonts::Font4);
    display.drawString("WiFi Scan + Auto Connect", rect.x + 16, rect.y + 14);

    display.setFont(&fonts::Font2);
    display.drawString("Scan on boot. Connect strongest preferred SSID automatically.", rect.x + 16, rect.y + 56);
}

void drawStatusCard(const Rect &rect)
{
    drawCard(rect, "Status", 244);

    display.setTextDatum(textdatum_t::top_left);
    display.setTextColor(TFT_BLACK, grayColor(244));

    display.setFont(&fonts::Font4);
    display.drawString(stateTitle(g_app_state), rect.x + 16, rect.y + 48);

    display.setFont(&fonts::Font2);
    display.drawString(ellipsize(g_status_line1, 48), rect.x + 16, rect.y + 92);
    display.drawString(ellipsize(g_status_line2, 48), rect.x + 16, rect.y + 118);
    display.drawString(ellipsize(g_status_line3, 48), rect.x + 16, rect.y + 144);
}

void drawTargetsCard(const Rect &rect)
{
    drawCard(rect, "Preferred SSIDs", 248);

    display.setFont(&fonts::Font2);
    display.setTextColor(TFT_BLACK, grayColor(248));
    display.setTextDatum(textdatum_t::top_left);

    display.drawString("1. xinyuandianzi", rect.x + 16, rect.y + 46);
    display.drawString("2. LilyGo-AABB", rect.x + 16, rect.y + 72);
}

void drawNetworksCard(const Rect &rect)
{
    drawCard(rect, "Nearby Networks", 250);

    const int start_y = rect.y + 44;
    const int line_height = 30;

    display.setFont(&fonts::Font2);
    display.setTextDatum(textdatum_t::top_left);
    display.setTextColor(TFT_BLACK, grayColor(250));

    display.drawString("Mark", rect.x + 16, start_y);
    display.drawString("RSSI", rect.x + 72, start_y);
    display.drawString("SSID", rect.x + 164, start_y);

    if (g_scan_results.empty()) {
        const char *message = "No scan result yet.";
        if (g_last_scan_failed) {
            message = "Last WiFi scan failed.";
        } else if (g_has_scanned_once) {
            message = "No WiFi found in last scan.";
        }
        display.drawString(message, rect.x + 16, start_y + 34);
        return;
    }

    const size_t visible_count = std::min(g_scan_results.size(), kMaxVisibleNetworks);

    for (size_t i = 0; i < visible_count; ++i) {
        const auto &entry = g_scan_results[i];
        const int row_y = start_y + 32 + static_cast<int>(i) * line_height;
        const uint32_t row_fill = entry.preferred ? grayColor(232) : grayColor(250);
        const String shown_ssid = entry.ssid.isEmpty() ? String("<hidden>") : ellipsize(entry.ssid, 30);
        char rssi_buffer[20];

        snprintf(rssi_buffer, sizeof(rssi_buffer), "%ld dBm", static_cast<long>(entry.rssi));

        if (entry.preferred) {
            display.fillRoundRect(rect.x + 10, row_y - 2, rect.w - 20, 24, 8, row_fill);
        }

        display.setTextColor(TFT_BLACK, row_fill);
        display.drawString(entry.preferred ? "*" : "-", rect.x + 24, row_y);
        display.drawString(rssi_buffer, rect.x + 72, row_y);
        display.drawString(shown_ssid, rect.x + 164, row_y);
    }

    if (g_scan_results.size() > visible_count) {
        const size_t hidden_count = g_scan_results.size() - visible_count;
        display.setTextColor(grayColor(90), grayColor(250));
        display.drawString(String("+ ") + hidden_count + " more networks", rect.x + 16, rect.y + rect.h - 28);
    }
}

void renderUi()
{
    const int width = display.width();
    const int height = display.height();
    const int card_width = width - (kMargin * 2);

    const Rect header = makeRect(kMargin, kMargin, card_width, kHeaderHeight);
    const Rect status = makeRect(kMargin, header.y + header.h + kGap, card_width, kStatusHeight);
    const Rect targets = makeRect(kMargin, status.y + status.h + kGap, card_width, kTargetsHeight);
    const Rect networks = makeRect(
        kMargin,
        targets.y + targets.h + kGap,
        card_width,
        height - targets.y - targets.h - kGap - kMargin);

    display.fillScreen(TFT_WHITE);
    drawHeader(header);
    drawStatusCard(status);
    drawTargetsCard(targets);
    drawNetworksCard(networks);
}

void presentScreen(epd_mode_t mode)
{
    display.powerSaveOff();
    display.setEpdMode(mode);
    display.display();
    display.waitDisplay();
    display.powerSaveOn();
}

epd_mode_t nextTextRefreshMode()
{
    // Prefer an occasional full refresh to keep text ghosting under control.
    ++g_text_refresh_count;
    if (g_text_refresh_count >= kQualityRefreshEvery) {
        g_text_refresh_count = 0;
        return epd_mode_t::epd_quality;
    }
    return epd_mode_t::epd_text;
}

void refreshScreen(epd_mode_t mode)
{
    display.setAutoDisplay(false);
    display.setColorDepth(4);
    display.setRotation(0);
    display.setTextWrap(false);

    display.startWrite();
    renderUi();
    display.endWrite();

    presentScreen(mode);
}

void collectScanResults(int16_t count)
{
    g_scan_results.clear();
    g_has_scanned_once = true;
    g_last_scan_failed = count < 0;
    if (count <= 0) {
        WiFi.scanDelete();
        return;
    }

    g_scan_results.reserve(static_cast<size_t>(count));

    for (int16_t i = 0; i < count; ++i) {
        const String ssid = WiFi.SSID(i);
        g_scan_results.push_back({
            ssid,
            WiFi.RSSI(i),
            isPreferredSsid(ssid),
        });
    }

    std::sort(g_scan_results.begin(), g_scan_results.end(), [](const ScanEntry &lhs, const ScanEntry &rhs) {
        if (lhs.rssi == rhs.rssi) {
            return lhs.ssid < rhs.ssid;
        }
        return lhs.rssi > rhs.rssi;
    });

    WiFi.scanDelete();
}

const TargetCredential *chooseBestTarget(int32_t &best_rssi)
{
    best_rssi = INT32_MIN;
    const TargetCredential *best_target = nullptr;

    for (const auto &entry : g_scan_results) {
        const TargetCredential *target = findTargetCredential(entry.ssid);
        if (target == nullptr) {
            continue;
        }

        if (best_target == nullptr || entry.rssi > best_rssi) {
            best_target = target;
            best_rssi = entry.rssi;
        }
    }

    return best_target;
}

void scheduleNextScan(uint32_t delay_ms)
{
    g_next_scan_ms = millis() + delay_ms;
}

void showConnectedStatus()
{
    g_active_ssid = WiFi.SSID();
    g_active_rssi = WiFi.RSSI();
    g_active_ip = WiFi.localIP();

    setStatus(
        AppState::kConnected,
        String("SSID: ") + g_active_ssid,
        String("RSSI: ") + g_active_rssi + " dBm",
        String("IP: ") + g_active_ip.toString());

    refreshScreen(epd_mode_t::epd_quality);
}

bool connectToTarget(const TargetCredential &target)
{
    Serial.printf("[%s] connect to %s\n", kTag, target.ssid);

    WiFi.disconnect();
    delay(100);
    WiFi.begin(target.ssid, target.password);

    const uint32_t start_ms = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start_ms) < kConnectTimeoutMs) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        showConnectedStatus();
        return true;
    }

    WiFi.disconnect();
    setStatus(
        AppState::kConnectFailed,
        String("SSID: ") + target.ssid,
        "Connect timeout, retry in 10 s",
        "Will scan and choose again");
    refreshScreen(epd_mode_t::epd_quality);
    return false;
}

void runScanCycle()
{
    Serial.printf("[%s] start scan\n", kTag);

    setStatus(
        AppState::kScanning,
        "Scanning nearby WiFi...",
        "Showing SSID and RSSI on screen",
        "Preferred targets will auto-connect");
    refreshScreen(nextTextRefreshMode());

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(80);

    const int16_t count = WiFi.scanNetworks(false, true);
    collectScanResults(count);

    int32_t best_rssi = INT32_MIN;
    const TargetCredential *target = chooseBestTarget(best_rssi);

    if (target != nullptr) {
        setStatus(
            AppState::kConnecting,
            String("Target found: ") + target->ssid,
            String("Best scan RSSI: ") + best_rssi + " dBm",
            "Connecting now...");
        refreshScreen(nextTextRefreshMode());

        if (connectToTarget(*target)) {
            return;
        }

        scheduleNextScan(kRescanIntervalMs);
        return;
    }

    if (count < 0) {
        setStatus(
            AppState::kWaiting,
            "WiFi scan failed",
            "Retry in 10 seconds",
            "No connection attempt");
    } else {
        setStatus(
            AppState::kWaiting,
            "Preferred SSIDs not found",
            "Rescan every 10 seconds",
            String("Nearby AP count: ") + g_scan_results.size());
    }

    refreshScreen(nextTextRefreshMode());
    scheduleNextScan(kRescanIntervalMs);
}

void handleConnectionLoss()
{
    Serial.printf("[%s] connection lost\n", kTag);

    WiFi.disconnect();
    setStatus(
        AppState::kWaiting,
        "WiFi disconnected",
        "Will rescan in 10 seconds",
        "Trying preferred SSIDs again");
    refreshScreen(epd_mode_t::epd_quality);
    scheduleNextScan(kRescanIntervalMs);
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("[M5GFX_WIFI] start");
    Serial.println("[M5GFX_WIFI] scan and auto-connect example");

    if (!display.init_without_reset(false)) {
        Serial.println("[M5GFX_WIFI] display init failed");
        while (true) {
            delay(1000);
        }
    }

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.disconnect();

    setStatus(
        AppState::kBoot,
        "Display ready",
        "WiFi STA mode ready",
        "Scanning on boot...");
    refreshScreen(epd_mode_t::epd_quality);

    runScanCycle();
}

void loop()
{
    if (WiFi.status() == WL_CONNECTED) {
        delay(250);
        return;
    }

    const uint32_t now = millis();
    if (g_app_state == AppState::kConnected) {
        handleConnectionLoss();
    } else if (now >= g_next_scan_ms) {
        runScanCycle();
    }

    delay(100);
}
