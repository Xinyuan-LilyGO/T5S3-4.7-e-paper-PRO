// SPDX-License-Identifier: GPL-2.0-only

#include <Arduino.h>
#include <Wire.h>

#include "GoodixGT6972P.h"

namespace {

constexpr int kTouchSda = 39;
constexpr int kTouchScl = 40;
constexpr int kTouchInterrupt = 3;
constexpr int kTouchReset = 9;

GoodixGT6972P touch;
bool previous_fingers[GoodixGT6972P::kMaxTouchPoints] = {};
bool previous_pen_present = false;
bool previous_pen_hover = false;
uint32_t read_error_count = 0;

const char *readResultName(GoodixGT6972P::ReadResult result)
{
    switch (result) {
    case GoodixGT6972P::ReadResult::kBusError:
        return "bus";
    case GoodixGT6972P::ReadResult::kChecksumError:
        return "checksum";
    case GoodixGT6972P::ReadResult::kFormatError:
        return "format";
    case GoodixGT6972P::ReadResult::kNoData:
        return "no-data";
    case GoodixGT6972P::ReadResult::kEvent:
        return "event";
    }
    return "unknown";
}

void printHexBytes(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        Serial.printf("%02X", data[i]);
    }
}

void printDeviceInfo()
{
    const GoodixGT6972P::DeviceInfo &info = touch.deviceInfo();

    Serial.println();
    Serial.println("GT6972P initialization succeeded");
    Serial.printf("I2C address       : 0x%02X\n", info.address);
    Serial.printf("ROM PID / VID     : %s / ", info.rom_pid);
    printHexBytes(info.rom_vid, sizeof(info.rom_vid));
    Serial.println();
    Serial.printf("Patch PID / VID   : %s / ", info.patch_pid);
    printHexBytes(info.patch_vid, sizeof(info.patch_vid));
    Serial.println();
    Serial.printf("Sensor ID         : %u\n", info.sensor_id);
    Serial.printf("IC info version   : 0x%02X\n", info.info_version);
    Serial.printf("Config ID/version : 0x%08lX / 0x%02X\n",
                  static_cast<unsigned long>(info.config_id),
                  info.config_version);
    Serial.printf("TX x RX channels  : %u x %u\n", info.driver_channels,
                  info.sensor_channels);
    Serial.printf("Touch data        : 0x%08lX (head=%u, point=%u)\n",
                  static_cast<unsigned long>(info.touch_data_address),
                  info.touch_data_head_length, info.point_struct_length);
    Serial.printf("Reported range     : %u x %u\n", info.max_x, info.max_y);
    Serial.printf("Stylus capability : %s (feature=0x%04X, frequencies=%u)\n",
                  info.stylus_feature ? "yes" : "not advertised",
                  info.stylus_feature, info.stylus_frequency_count);

    if (strstr(info.patch_pid, "6972") == nullptr) {
        Serial.println("WARNING: Patch PID does not contain 6972; verify the fitted IC.");
    }
    Serial.println();
    Serial.println("Touch the panel or bring the active pen into range.");
    Serial.println();
}

void printTouchEvent(const GoodixGT6972P::Event &event)
{
    bool current_fingers[GoodixGT6972P::kMaxTouchPoints] = {};

    for (uint8_t i = 0; i < event.finger_count; ++i) {
        const GoodixGT6972P::FingerPoint &point = event.fingers[i];
        current_fingers[point.id] = true;
        Serial.printf("%lums FINGER id=%u state=%s%s x=%u y=%u width=%u\n",
                      static_cast<unsigned long>(millis()), point.id,
                      previous_fingers[point.id] ? "MOVE" : "DOWN",
                      point.glove ? " glove" : "", point.x, point.y,
                      point.width);
    }

    for (uint8_t id = 0; id < GoodixGT6972P::kMaxTouchPoints; ++id) {
        if (previous_fingers[id] && !current_fingers[id]) {
            Serial.printf("%lums FINGER id=%u state=UP\n",
                          static_cast<unsigned long>(millis()), id);
        }
        previous_fingers[id] = current_fingers[id];
    }

    if (event.pen.present) {
        const GoodixGT6972P::PenPoint &pen = event.pen;
        Serial.printf(
            "%lums PEN state=%s x=%u y=%u pressure=%u tilt=(%d,%d) "
            "buttons=(%u,%u) eraser=%u battery=",
            static_cast<unsigned long>(millis()),
            pen.hover ? "HOVER"
                      : (!previous_pen_present || previous_pen_hover ? "DOWN"
                                                                     : "MOVE"),
            pen.x, pen.y, pen.pressure, pen.tilt_x, pen.tilt_y, pen.button1,
            pen.button2, pen.eraser);
        if (pen.battery >= 0) {
            Serial.printf("%d%%\n", pen.battery);
        } else {
            Serial.println("unknown");
        }
    } else if (previous_pen_present) {
        Serial.printf("%lums PEN state=UP\n",
                      static_cast<unsigned long>(millis()));
    }
    previous_pen_present = event.pen.present;
    previous_pen_hover = event.pen.present && event.pen.hover;

    if (event.key_mask != 0) {
        Serial.printf("%lums TOUCH_KEY mask=0x%03X\n",
                      static_cast<unsigned long>(millis()), event.key_mask);
    }
}

} // namespace

void setup()
{
    Serial.begin(115200);
    const uint32_t serial_wait_start = millis();
    while (!Serial && millis() - serial_wait_start < 3000) {
        delay(10);
    }

    Serial.println();
    Serial.println("Goodix GT6972P touch + active pen test");
    Serial.printf("Pins: SDA=%d SCL=%d INT=%d RST=%d\n", kTouchSda,
                  kTouchScl, kTouchInterrupt, kTouchReset);

    if (!touch.begin(Wire, kTouchSda, kTouchScl, kTouchReset,
                     kTouchInterrupt)) {
        Serial.printf("Initialization failed: %s\n", touch.lastError());
        Serial.println("Check power, wiring, I2C address, firmware, and reset timing.");
        while (true) {
            delay(1000);
        }
    }

    printDeviceInfo();
}

void loop()
{
    static uint32_t last_fallback_poll_ms = 0;
    const uint32_t now = millis();
    if (!touch.interruptAsserted() && now - last_fallback_poll_ms < 10) {
        delay(1);
        return;
    }
    last_fallback_poll_ms = now;

    GoodixGT6972P::Event event;
    const GoodixGT6972P::ReadResult result = touch.readEvent(event);

    if (result == GoodixGT6972P::ReadResult::kEvent) {
        read_error_count = 0;
        if (event.has_touch) {
            printTouchEvent(event);
        }
        if (event.has_request) {
            Serial.printf("%lums REQUEST code=0x%02X\n",
                          static_cast<unsigned long>(millis()),
                          event.request_code);
        }
        if (event.has_gesture) {
            Serial.printf("%lums GESTURE code=0x%02X\n",
                          static_cast<unsigned long>(millis()),
                          event.gesture_code);
        }
    } else if (result != GoodixGT6972P::ReadResult::kNoData) {
        ++read_error_count;
        if (read_error_count == 1 || read_error_count % 20 == 0) {
            Serial.printf("Read error #%lu: %s (%s), INT=%d\n",
                          static_cast<unsigned long>(read_error_count),
                          readResultName(result), touch.lastError(),
                          touch.interruptAsserted());
        }
    }

    delay(2);
}
