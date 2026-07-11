/*
  LilyGo T5 E-Paper S3 Pro
  LoRa serial parameter example

  Serial commands:
    help
    show
    show params
    TX:<0|1>
    RX:<0|1>
    mode:tx|rx|standby
    send:<text>
    freq:<MHz>
    bw:<kHz>
    sf:<5-12>
    cr:<5-8>
    sw:<0x12|18>
    tp:<-9..22>
    pl:<1..65535>
    crc:<0|1>
    iq:<0|1>
    cw:<0|1>

  Notes:
    - Parameter changes are applied immediately.
    - TX:1 sends payload[] = {1, 2, 3, 4, 5} every 500 ms.
    - TX and RX cannot both be enabled at the same time.
    - cw:1 starts continuous wave output, cw:0 returns to standby.
*/

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>

#include <cstdlib>

#include "ExtensionIOXL9555.hpp"
#include "utilities.h"

namespace {

constexpr float kTcxoVoltage = 2.4f;
constexpr uint8_t kCurrentLimitMa = 140;
constexpr uint32_t kAutoTxIntervalMs = 500;

constexpr float kFskBitRate = 4.8f;
constexpr float kFskFreqDeviation = 5.0f;
constexpr float kFskRxBandwidth = 156.2f;
uint8_t kAutoTxPayload[] = {1, 2, 3, 4, 5};
constexpr char kAutoTxPayloadLabel[] = "01 02 03 04 05";

ExtensionIOXL9555 io;
SX1262 radio = new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY);

struct LoRaSettings {
    float freq = 868.0f;
    float bw = 125.0f;
    uint8_t sf = 10;
    uint8_t cr = 6;
    uint8_t sw = 0x12;
    int8_t tx_power = 22;
    uint16_t preamble_length = 15;
    bool crc = false;
    bool iq_inverted = false;
    bool continuous_wave = false;
};

enum class RadioMode : uint8_t {
    Standby = 0,
    TxAuto,
    TxSingle,
    Rx,
    ContinuousWave,
};

LoRaSettings g_settings;
RadioMode g_mode = RadioMode::Standby;

bool g_power_ready = false;
bool g_radio_ready = false;
bool g_tx_busy = false;

volatile bool g_tx_done = false;
volatile bool g_rx_done = false;

uint32_t g_next_tx_ms = 0;
uint32_t g_tx_count = 0;
uint32_t g_rx_count = 0;

float g_last_rssi = 0.0f;
float g_last_snr = 0.0f;
float g_last_freq_error = 0.0f;

String g_last_tx_payload;
String g_last_rx_payload;
String g_single_tx_payload;

bool isTxAutoEnabled()
{
    return g_mode == RadioMode::TxAuto;
}

bool isRxEnabled()
{
    return g_mode == RadioMode::Rx;
}

const char *modeName(RadioMode mode)
{
    switch (mode) {
        case RadioMode::TxAuto:
            return "TX_AUTO";
        case RadioMode::TxSingle:
            return "TX_SINGLE";
        case RadioMode::Rx:
            return "RX";
        case RadioMode::ContinuousWave:
            return "CW";
        case RadioMode::Standby:
        default:
            return "STANDBY";
    }
}

bool radioCallOk(const char *label, int16_t state)
{
    if (state == RADIOLIB_ERR_NONE) {
        return true;
    }
    Serial.printf("[LORA] %s failed, code %d\n", label, state);
    return false;
}

void printHelp()
{
    Serial.println(F("=========================================="));
    Serial.println(F("Available commands:"));
    Serial.println(F("  help"));
    Serial.println(F("  show"));
    Serial.println(F("  show params"));
    Serial.println(F("  TX:<0|1>"));
    Serial.println(F("  RX:<0|1>"));
    Serial.println(F("  mode:tx|rx|standby"));
    Serial.println(F("  send:<text>"));
    Serial.println(F("  freq:<MHz>"));
    Serial.println(F("  bw:<kHz>"));
    Serial.println(F("  sf:<5-12>"));
    Serial.println(F("  cr:<5-8>"));
    Serial.println(F("  sw:<0x12|18>"));
    Serial.println(F("  tp:<-9..22>"));
    Serial.println(F("  pl:<1..65535>"));
    Serial.println(F("  crc:<0|1>"));
    Serial.println(F("  iq:<0|1>"));
    Serial.println(F("  cw:<0|1>"));
    Serial.println(F("------------------------------------------"));
    Serial.println(F("Examples:"));
    Serial.println(F("  freq:470.0"));
    Serial.println(F("  bw:250.0"));
    Serial.println(F("  sf:7"));
    Serial.println(F("  TX:1"));
    Serial.println(F("  RX:1"));
    Serial.println(F("  send:hello lora"));
    Serial.println(F("=========================================="));
}

void printCurrentSettings()
{
    Serial.println(F("-------------- LORA PARAMS ---------------"));
    Serial.printf("Mode              : %s\n", modeName(g_mode));
    Serial.printf("Frequency         : %.2f MHz\n", g_settings.freq);
    Serial.printf("Bandwidth         : %.2f kHz\n", g_settings.bw);
    Serial.printf("Spreading Factor  : %u\n", g_settings.sf);
    Serial.printf("Coding Rate       : 4/%u\n", g_settings.cr);
    Serial.printf("Sync Word         : 0x%02X\n", g_settings.sw);
    Serial.printf("TX Power          : %d dBm\n", g_settings.tx_power);
    Serial.printf("Preamble Length   : %u\n", g_settings.preamble_length);
    Serial.printf("CRC               : %s\n", g_settings.crc ? "true" : "false");
    Serial.printf("Invert IQ         : %s\n", g_settings.iq_inverted ? "true" : "false");
    Serial.printf("Continuous Wave   : %s\n", g_settings.continuous_wave ? "true" : "false");
    Serial.printf("TX Auto           : %d\n", isTxAutoEnabled() ? 1 : 0);
    Serial.printf("RX Enable         : %d\n", isRxEnabled() ? 1 : 0);
    Serial.printf("TX Count          : %lu\n", static_cast<unsigned long>(g_tx_count));
    Serial.printf("RX Count          : %lu\n", static_cast<unsigned long>(g_rx_count));
    Serial.printf("Last TX Payload   : %s\n", g_last_tx_payload.length() ? g_last_tx_payload.c_str() : "-");
    Serial.printf("Last RX Payload   : %s\n", g_last_rx_payload.length() ? g_last_rx_payload.c_str() : "-");
    Serial.printf("Last RSSI / SNR   : %.1f dBm / %.1f dB\n", g_last_rssi, g_last_snr);
    Serial.printf("Last Freq Error   : %.0f Hz\n", g_last_freq_error);
    Serial.println(F("------------------------------------------"));
}

bool parseBoolValue(String value, bool &result)
{
    value.trim();
    value.toLowerCase();

    if (value == "1" || value == "on" || value == "true") {
        result = true;
        return true;
    }
    if (value == "0" || value == "off" || value == "false") {
        result = false;
        return true;
    }
    return false;
}

RadioMode effectiveModeForSettings(RadioMode currentMode, const LoRaSettings &settings)
{
    if (settings.continuous_wave) {
        return RadioMode::ContinuousWave;
    }
    if (currentMode == RadioMode::ContinuousWave) {
        return RadioMode::Standby;
    }
    return currentMode;
}

bool powerOnLoRa()
{
    if (!io.init(Wire, BOARD_SDA, BOARD_SCL, XL9555_SLAVE_ADDRESS0)) {
        Serial.println(F("[LORA] XL9555 init failed"));
        return false;
    }

    io.pinMode(ExtensionIOXL9555::IO0, OUTPUT);
    io.digitalWrite(ExtensionIOXL9555::IO0, HIGH);
    delay(1500);
    return true;
}

void clearRadioFlags()
{
    g_tx_done = false;
    g_rx_done = false;
    g_tx_busy = false;
}

void stopRadioActivity()
{
    clearRadioFlags();

    if (!g_radio_ready) {
        g_mode = RadioMode::Standby;
        return;
    }

    radio.finishTransmit();
    radio.standby();
    g_mode = RadioMode::Standby;
}

bool startAutoTransmit();
bool startSingleTransmit();
bool startReceiveMode();
bool startContinuousWaveMode();

bool applyLoRaSettings()
{
    if (!radioCallOk("setTCXO", radio.setTCXO(kTcxoVoltage))) {
        return false;
    }
    if (!radioCallOk("setDio2AsRfSwitch", radio.setDio2AsRfSwitch())) {
        return false;
    }
    if (!radioCallOk("setFrequency", radio.setFrequency(g_settings.freq))) {
        return false;
    }
    if (!radioCallOk("setBandwidth", radio.setBandwidth(g_settings.bw))) {
        return false;
    }
    if (!radioCallOk("setSpreadingFactor", radio.setSpreadingFactor(g_settings.sf))) {
        return false;
    }
    if (!radioCallOk("setCodingRate", radio.setCodingRate(g_settings.cr))) {
        return false;
    }
    if (!radioCallOk("setSyncWord", radio.setSyncWord(g_settings.sw))) {
        return false;
    }
    if (!radioCallOk("setOutputPower", radio.setOutputPower(g_settings.tx_power))) {
        return false;
    }
    if (!radioCallOk("setCurrentLimit", radio.setCurrentLimit(kCurrentLimitMa))) {
        return false;
    }
    if (!radioCallOk("setPreambleLength", radio.setPreambleLength(g_settings.preamble_length))) {
        return false;
    }
    if (!radioCallOk("setCRC", radio.setCRC(g_settings.crc))) {
        return false;
    }
    if (!radioCallOk("invertIQ", radio.invertIQ(g_settings.iq_inverted))) {
        return false;
    }

    return true;
}

bool configureRadio(RadioMode targetMode)
{
    if (!g_power_ready) {
        Serial.println(F("[LORA] power is not ready"));
        return false;
    }

    stopRadioActivity();
    g_radio_ready = false;
    delay(10);

    if (targetMode == RadioMode::ContinuousWave) {
        if (!radioCallOk("radio.beginFSK",
                         radio.beginFSK(g_settings.freq,
                                        kFskBitRate,
                                        kFskFreqDeviation,
                                        kFskRxBandwidth,
                                        g_settings.tx_power,
                                        g_settings.preamble_length,
                                        kTcxoVoltage))) {
            return false;
        }
        if (!radioCallOk("setDio2AsRfSwitch", radio.setDio2AsRfSwitch())) {
            return false;
        }
        if (!radioCallOk("setOutputPower", radio.setOutputPower(g_settings.tx_power))) {
            return false;
        }
        if (!radioCallOk("transmitDirect", radio.transmitDirect())) {
            return false;
        }

        g_radio_ready = true;
        g_mode = RadioMode::ContinuousWave;
        return true;
    }

    if (!radioCallOk("radio.begin", radio.begin(g_settings.freq))) {
        return false;
    }
    if (!applyLoRaSettings()) {
        return false;
    }

    g_radio_ready = true;

    switch (targetMode) {
        case RadioMode::TxAuto:
            return startAutoTransmit();
        case RadioMode::TxSingle:
            return startSingleTransmit();
        case RadioMode::Rx:
            return startReceiveMode();
        case RadioMode::Standby:
        default:
            radio.standby();
            g_mode = RadioMode::Standby;
            return true;
    }
}

bool commitSettingChange(const LoRaSettings &previousSettings, RadioMode previousMode)
{
    const RadioMode targetMode = effectiveModeForSettings(previousMode, g_settings);
    if (configureRadio(targetMode)) {
        return true;
    }

    Serial.println(F("[LORA] setting apply failed, restoring previous settings"));
    g_settings = previousSettings;

    const RadioMode restoreMode = effectiveModeForSettings(previousMode, g_settings);
    if (!configureRadio(restoreMode)) {
        Serial.println(F("[LORA] failed to restore previous radio state"));
    }
    return false;
}

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void onTxDone()
{
    g_tx_done = true;
}

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void onRxDone()
{
    g_rx_done = true;
}

bool startAutoTransmit()
{
    g_last_tx_payload = kAutoTxPayloadLabel;
    radio.setPacketSentAction(onTxDone);

    if (!radioCallOk("startTransmit",
                     radio.startTransmit(kAutoTxPayload, sizeof(kAutoTxPayload)))) {
        g_mode = RadioMode::Standby;
        return false;
    }

    g_tx_busy = true;
    g_mode = RadioMode::TxAuto;
    return true;
}

bool startSingleTransmit()
{
    g_last_tx_payload = g_single_tx_payload;
    radio.setPacketSentAction(onTxDone);

    if (!radioCallOk("startTransmit", radio.startTransmit(g_single_tx_payload))) {
        g_mode = RadioMode::Standby;
        return false;
    }

    g_tx_busy = true;
    g_mode = RadioMode::TxSingle;
    return true;
}

bool startReceiveMode()
{
    radio.setPacketReceivedAction(onRxDone);
    if (!radioCallOk("startReceive", radio.startReceive())) {
        g_mode = RadioMode::Standby;
        return false;
    }

    g_mode = RadioMode::Rx;
    return true;
}

bool startContinuousWaveMode()
{
    return configureRadio(RadioMode::ContinuousWave);
}

void handleTxDone()
{
    if (!g_tx_done) {
        return;
    }

    g_tx_done = false;
    g_tx_busy = false;
    radio.finishTransmit();
    ++g_tx_count;

    Serial.printf("[LORA] TX #%lu done: %s\n",
                  static_cast<unsigned long>(g_tx_count),
                  g_last_tx_payload.c_str());

    if (g_mode == RadioMode::TxAuto) {
        g_next_tx_ms = millis() + kAutoTxIntervalMs;
    } else {
        radio.standby();
        g_mode = RadioMode::Standby;
        Serial.println(F("[LORA] single TX finished, radio back to standby"));
    }
}

void handleRxDone()
{
    if (!g_rx_done) {
        return;
    }

    g_rx_done = false;

    uint8_t payload[256] = {0};
    const size_t length = radio.getPacketLength();
    const size_t readLength = (length <= sizeof(payload)) ? length : sizeof(payload);
    const int16_t state = radio.readData(payload, readLength);
    if (state == RADIOLIB_ERR_NONE) {
        ++g_rx_count;
        g_last_rssi = radio.getRSSI();
        g_last_snr = radio.getSNR();
        g_last_freq_error = radio.getFrequencyError();

        char hexBuffer[3 * sizeof(payload) + 1] = {0};
        size_t offset = 0;
        for (size_t i = 0; i < readLength && (offset + 4) < sizeof(hexBuffer); ++i) {
            offset += static_cast<size_t>(
                snprintf(&hexBuffer[offset],
                         sizeof(hexBuffer) - offset,
                         (i + 1 < readLength) ? "%02X " : "%02X",
                         payload[i]));
        }
        g_last_rx_payload = hexBuffer;

        Serial.printf("[LORA] RX #%lu (%u bytes): %s\n",
                      static_cast<unsigned long>(g_rx_count),
                      static_cast<unsigned>(readLength),
                      hexBuffer);
        Serial.printf("[LORA] RSSI=%.1f dBm, SNR=%.1f dB, FreqError=%.0f Hz\n",
                      g_last_rssi,
                      g_last_snr,
                      g_last_freq_error);
    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
        Serial.println(F("[LORA] CRC mismatch"));
    } else {
        Serial.printf("[LORA] readData failed, code %d\n", state);
    }

    if (g_mode == RadioMode::Rx) {
        if (!radioCallOk("startReceive", radio.startReceive())) {
            g_mode = RadioMode::Standby;
        }
    }
}

void handleAutoTransmit()
{
    if (g_mode != RadioMode::TxAuto || g_tx_busy) {
        return;
    }

    if (millis() < g_next_tx_ms) {
        return;
    }

    startAutoTransmit();
}

bool applyFreq(const String &value)
{
    char *end = nullptr;
    const float freq = strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') {
        Serial.println(F("[LORA] invalid frequency format"));
        return false;
    }

    const LoRaSettings previousSettings = g_settings;
    const RadioMode previousMode = g_mode;
    g_settings.freq = freq;

    if (!commitSettingChange(previousSettings, previousMode)) {
        return false;
    }

    Serial.printf("[LORA] frequency set to %.2f MHz\n", g_settings.freq);
    return true;
}

bool applyBw(const String &value)
{
    char *end = nullptr;
    const float bw = strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') {
        Serial.println(F("[LORA] invalid bandwidth format"));
        return false;
    }

    const LoRaSettings previousSettings = g_settings;
    const RadioMode previousMode = g_mode;
    g_settings.bw = bw;

    if (!commitSettingChange(previousSettings, previousMode)) {
        return false;
    }

    Serial.printf("[LORA] bandwidth set to %.2f kHz\n", g_settings.bw);
    return true;
}

bool applySf(const String &value)
{
    const int sf = value.toInt();
    if (sf < 5 || sf > 12) {
        Serial.println(F("[LORA] spreading factor must be 5..12"));
        return false;
    }

    const LoRaSettings previousSettings = g_settings;
    const RadioMode previousMode = g_mode;
    g_settings.sf = static_cast<uint8_t>(sf);

    if (!commitSettingChange(previousSettings, previousMode)) {
        return false;
    }

    Serial.printf("[LORA] spreading factor set to %u\n", g_settings.sf);
    return true;
}

bool applyCr(const String &value)
{
    const int cr = value.toInt();
    if (cr < 5 || cr > 8) {
        Serial.println(F("[LORA] coding rate must be 5..8"));
        return false;
    }

    const LoRaSettings previousSettings = g_settings;
    const RadioMode previousMode = g_mode;
    g_settings.cr = static_cast<uint8_t>(cr);

    if (!commitSettingChange(previousSettings, previousMode)) {
        return false;
    }

    Serial.printf("[LORA] coding rate set to 4/%u\n", g_settings.cr);
    return true;
}

bool applySw(const String &value)
{
    char *end = nullptr;
    const long sw = strtol(value.c_str(), &end, 0);
    if (end == value.c_str() || *end != '\0' || sw < 0 || sw > 0xFF) {
        Serial.println(F("[LORA] sync word must be 0..255, e.g. 0x12"));
        return false;
    }

    const LoRaSettings previousSettings = g_settings;
    const RadioMode previousMode = g_mode;
    g_settings.sw = static_cast<uint8_t>(sw);

    if (!commitSettingChange(previousSettings, previousMode)) {
        return false;
    }

    Serial.printf("[LORA] sync word set to 0x%02X\n", g_settings.sw);
    return true;
}

bool applyTp(const String &value)
{
    const int tp = value.toInt();
    if (tp < -9 || tp > 22) {
        Serial.println(F("[LORA] TX power must be -9..22 dBm"));
        return false;
    }

    const LoRaSettings previousSettings = g_settings;
    const RadioMode previousMode = g_mode;
    g_settings.tx_power = static_cast<int8_t>(tp);

    if (!commitSettingChange(previousSettings, previousMode)) {
        return false;
    }

    Serial.printf("[LORA] TX power set to %d dBm\n", g_settings.tx_power);
    return true;
}

bool applyPl(const String &value)
{
    const long pl = value.toInt();
    if (pl < 1 || pl > 65535) {
        Serial.println(F("[LORA] preamble length must be 1..65535"));
        return false;
    }

    const LoRaSettings previousSettings = g_settings;
    const RadioMode previousMode = g_mode;
    g_settings.preamble_length = static_cast<uint16_t>(pl);

    if (!commitSettingChange(previousSettings, previousMode)) {
        return false;
    }

    Serial.printf("[LORA] preamble length set to %u\n", g_settings.preamble_length);
    return true;
}

bool applyCrc(const String &value)
{
    bool enable = false;
    if (!parseBoolValue(value, enable)) {
        Serial.println(F("[LORA] crc must be 0 or 1"));
        return false;
    }

    const LoRaSettings previousSettings = g_settings;
    const RadioMode previousMode = g_mode;
    g_settings.crc = enable;

    if (!commitSettingChange(previousSettings, previousMode)) {
        return false;
    }

    Serial.printf("[LORA] CRC set to %s\n", g_settings.crc ? "true" : "false");
    return true;
}

bool applyIq(const String &value)
{
    bool enable = false;
    if (!parseBoolValue(value, enable)) {
        Serial.println(F("[LORA] iq must be 0 or 1"));
        return false;
    }

    const LoRaSettings previousSettings = g_settings;
    const RadioMode previousMode = g_mode;
    g_settings.iq_inverted = enable;

    if (!commitSettingChange(previousSettings, previousMode)) {
        return false;
    }

    Serial.printf("[LORA] invert IQ set to %s\n", g_settings.iq_inverted ? "true" : "false");
    return true;
}

bool applyCw(const String &value)
{
    bool enable = false;
    if (!parseBoolValue(value, enable)) {
        Serial.println(F("[LORA] cw must be 0 or 1"));
        return false;
    }

    const LoRaSettings previousSettings = g_settings;
    const RadioMode previousMode = g_mode;
    g_settings.continuous_wave = enable;

    if (!commitSettingChange(previousSettings, previousMode)) {
        return false;
    }

    if (g_settings.continuous_wave) {
        Serial.println(F("[LORA] continuous wave started"));
    } else {
        Serial.println(F("[LORA] continuous wave stopped, radio in standby"));
    }
    return true;
}

bool applyTxControl(const String &value)
{
    bool enable = false;
    if (!parseBoolValue(value, enable)) {
        Serial.println(F("[LORA] TX must be 0 or 1"));
        return false;
    }

    if (enable) {
        if (g_settings.continuous_wave) {
            Serial.println(F("[LORA] disable continuous wave first with cw:0"));
            return false;
        }
        if (!configureRadio(RadioMode::TxAuto)) {
            return false;
        }
        Serial.println(F("[LORA] TX auto enabled, RX disabled"));
        return true;
    }

    if (g_mode == RadioMode::TxAuto) {
        if (!configureRadio(RadioMode::Standby)) {
            return false;
        }
        Serial.println(F("[LORA] TX auto disabled"));
    } else {
        Serial.println(F("[LORA] TX auto is already disabled"));
    }
    return true;
}

bool applyRxControl(const String &value)
{
    bool enable = false;
    if (!parseBoolValue(value, enable)) {
        Serial.println(F("[LORA] RX must be 0 or 1"));
        return false;
    }

    if (enable) {
        if (g_settings.continuous_wave) {
            Serial.println(F("[LORA] disable continuous wave first with cw:0"));
            return false;
        }
        if (!configureRadio(RadioMode::Rx)) {
            return false;
        }
        Serial.println(F("[LORA] RX enabled, TX auto disabled"));
        return true;
    }

    if (g_mode == RadioMode::Rx) {
        if (!configureRadio(RadioMode::Standby)) {
            return false;
        }
        Serial.println(F("[LORA] RX disabled"));
    } else {
        Serial.println(F("[LORA] RX is already disabled"));
    }
    return true;
}

bool applyMode(String value)
{
    value.trim();
    value.toLowerCase();

    if (g_settings.continuous_wave) {
        Serial.println(F("[LORA] disable continuous wave first with cw:0"));
        return false;
    }

    RadioMode targetMode = RadioMode::Standby;
    if (value == "tx") {
        return applyTxControl("1");
    } else if (value == "rx") {
        return applyRxControl("1");
    } else if (value == "standby" || value == "stop" || value == "idle") {
        targetMode = RadioMode::Standby;
    } else {
        Serial.println(F("[LORA] mode must be tx, rx or standby"));
        return false;
    }

    if (!configureRadio(targetMode)) {
        return false;
    }

    Serial.printf("[LORA] mode set to %s\n", modeName(g_mode));
    return true;
}

bool applySend(const String &value)
{
    if (value.length() == 0) {
        Serial.println(F("[LORA] send payload cannot be empty"));
        return false;
    }
    if (g_settings.continuous_wave) {
        Serial.println(F("[LORA] disable continuous wave first with cw:0"));
        return false;
    }
    if (isTxAutoEnabled() || isRxEnabled()) {
        Serial.println(F("[LORA] set TX:0 and RX:0 before single send"));
        return false;
    }

    g_single_tx_payload = value;
    if (!configureRadio(RadioMode::TxSingle)) {
        return false;
    }

    Serial.printf("[LORA] single TX queued: %s\n", g_single_tx_payload.c_str());
    return true;
}

void handleSerialCommand()
{
    if (!Serial.available()) {
        return;
    }

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) {
        return;
    }

    if (cmd == "help") {
        printHelp();
        return;
    }

    if (cmd == "show" || cmd == "show params") {
        printCurrentSettings();
        return;
    }

    const int colon = cmd.indexOf(':');
    if (colon < 0) {
        Serial.println(F("[LORA] unknown command, type help"));
        return;
    }

    String key = cmd.substring(0, colon);
    String value = cmd.substring(colon + 1);
    key.trim();
    value.trim();
    key.toLowerCase();

    if (value.length() == 0) {
        Serial.println(F("[LORA] missing value"));
        return;
    }

    bool handled = true;

    if (key == "freq") {
        applyFreq(value);
    } else if (key == "bw") {
        applyBw(value);
    } else if (key == "tx") {
        applyTxControl(value);
    } else if (key == "rx") {
        applyRxControl(value);
    } else if (key == "sf") {
        applySf(value);
    } else if (key == "cr") {
        applyCr(value);
    } else if (key == "sw") {
        applySw(value);
    } else if (key == "tp") {
        applyTp(value);
    } else if (key == "pl") {
        applyPl(value);
    } else if (key == "crc") {
        applyCrc(value);
    } else if (key == "iq") {
        applyIq(value);
    } else if (key == "cw") {
        applyCw(value);
    } else if (key == "mode") {
        applyMode(value);
    } else if (key == "send") {
        applySend(value);
    } else {
        handled = false;
    }

    if (!handled) {
        Serial.println(F("[LORA] unknown command, type help"));
    }
}

}  // namespace

void setup()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    Serial.begin(115200);
    Serial.setTimeout(50);
    delay(400);

    Serial.println();
    Serial.println(F("[LORA] serial parameter example start"));

    g_power_ready = powerOnLoRa();
    if (!g_power_ready) {
        Serial.println(F("[LORA] LoRa power-on failed"));
        while (true) {
            delay(1000);
        }
    }

    SPI.begin(BOARD_SPI_SCLK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    if (!configureRadio(RadioMode::Standby)) {
        Serial.println(F("[LORA] radio init failed"));
        while (true) {
            delay(1000);
        }
    }

    Serial.println(F("[LORA] ready"));
    printHelp();
    printCurrentSettings();
}

void loop()
{
    handleSerialCommand();
    handleTxDone();
    handleRxDone();
    handleAutoTransmit();
    delay(10);
}
