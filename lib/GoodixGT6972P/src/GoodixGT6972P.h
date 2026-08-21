// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <Arduino.h>
#include <Wire.h>

class GoodixGT6972P {
public:
    static constexpr uint8_t kMaxTouchPoints = 10;

    struct DeviceInfo {
        uint8_t address = 0;
        char rom_pid[7] = {};
        char patch_pid[9] = {};
        uint8_t rom_vid[3] = {};
        uint8_t patch_vid[4] = {};
        uint8_t sensor_id = 0;
        uint8_t info_version = 0;
        uint8_t config_version = 0;
        uint8_t driver_channels = 0;
        uint8_t sensor_channels = 0;
        uint8_t stylus_frequency_count = 0;
        uint16_t stylus_feature = 0;
        uint32_t config_id = 0;
        uint32_t touch_data_address = 0;
        uint32_t frame_data_address = 0;
        uint16_t touch_data_head_length = 0;
        uint16_t point_struct_length = 0;
        uint16_t max_x = 0;
        uint16_t max_y = 0;
    };

    struct FingerPoint {
        uint8_t id = 0;
        bool glove = false;
        uint16_t x = 0;
        uint16_t y = 0;
        uint16_t width = 0;
    };

    struct PenPoint {
        bool present = false;
        bool hover = false;
        bool button1 = false;
        bool button2 = false;
        bool eraser = false;
        uint16_t x = 0;
        uint16_t y = 0;
        uint16_t pressure = 0;
        int8_t tilt_x = 0;
        int8_t tilt_y = 0;
        int8_t battery = -1;
    };

    struct Event {
        bool has_touch = false;
        bool has_request = false;
        bool has_gesture = false;
        uint8_t request_code = 0;
        uint8_t gesture_code = 0;
        uint16_t key_mask = 0;
        uint8_t finger_count = 0;
        FingerPoint fingers[kMaxTouchPoints] = {};
        PenPoint pen = {};
    };

    enum class ReadResult : uint8_t {
        kNoData,
        kEvent,
        kBusError,
        kChecksumError,
        kFormatError,
    };

    bool begin(TwoWire &wire, int sda_pin, int scl_pin, int reset_pin,
               int interrupt_pin, uint32_t frequency = 400000);
    ReadResult readEvent(Event &event);

    const DeviceInfo &deviceInfo() const { return info_; }
    const char *lastError() const { return last_error_; }
    bool interruptAsserted() const;

private:
    static constexpr size_t kIcInfoMaxLength = 256;
    static constexpr size_t kEventBufferLength = 128;

    bool resetController();
    bool confirmDevice();
    bool readFirmwareVersion();
    bool readFirmwareVersionAt(uint32_t address);
    bool readIcInfo();
    bool readIcInfoAt(uint32_t address);
    bool parseIcInfoV1(const uint8_t *data, size_t length);
    bool parseIcInfoV2(const uint8_t *data, size_t length);
    bool skipFrequencyList(const uint8_t *data, size_t length, size_t &offset,
                           uint8_t &count);
    bool readRegister(uint32_t reg, uint8_t *data, size_t length);
    bool writeRegister(uint32_t reg, const uint8_t *data, size_t length);
    bool clearEvent();

    static bool checksumValid(const uint8_t *data, size_t length);
    static uint16_t readLe16(const uint8_t *data);
    static uint32_t readLe32(const uint8_t *data);
    static void copyPid(char *destination, size_t destination_length,
                        const uint8_t *source, size_t source_length);

    TwoWire *wire_ = nullptr;
    uint8_t address_ = 0;
    int reset_pin_ = -1;
    int interrupt_pin_ = -1;
    int8_t pen_battery_ = -1;
    bool berlin_a_registers_ = false;
    DeviceInfo info_ = {};
    const char *last_error_ = "not initialized";
};
