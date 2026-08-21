// SPDX-License-Identifier: GPL-2.0-only

#include "GoodixGT6972P.h"

#include <cstring>

namespace {

constexpr uint8_t kCandidateAddresses[] = {0x5D, 0x14};

constexpr uint32_t kBootOptionAddress = 0x10000;
constexpr uint32_t kFirmwareVersionAddress = 0x10014;
constexpr uint32_t kFirmwareVersionAddressBerlinA = 0x1000C;
constexpr uint32_t kIcInfoAddress = 0x10070;
constexpr uint32_t kIcInfoAddressBerlinA = 0x10068;

constexpr size_t kFirmwareVersionLength = 28;
constexpr size_t kEventHeadLength = 8;
constexpr size_t kBytesPerPoint = 8;
constexpr size_t kBytesPerStylus = 16;
constexpr size_t kInitialEventReadLength =
    kEventHeadLength + kBytesPerPoint * 2 + 2;

constexpr uint8_t kTouchEvent = 0x80;
constexpr uint8_t kRequestEvent = 0x40;
constexpr uint8_t kGestureEvent = 0x20;

constexpr uint8_t kPointTypeStylusHover = 1;
constexpr uint8_t kPointTypeFinger = 2;
constexpr uint8_t kPointTypeStylus = 3;
constexpr uint8_t kPointTypeGlove = 4;
constexpr uint8_t kPointTypeKey = 5;

constexpr uint8_t kMaxFrequencyCount = 8;
constexpr size_t kI2cChunkLength = 64;
constexpr uint8_t kBusRetryCount = 2;

} // namespace

bool GoodixGT6972P::begin(TwoWire &wire, int sda_pin, int scl_pin,
                          int reset_pin, int interrupt_pin,
                          uint32_t frequency)
{
    wire_ = &wire;
    reset_pin_ = reset_pin;
    interrupt_pin_ = interrupt_pin;
    address_ = 0;
    pen_battery_ = -1;
    berlin_a_registers_ = false;
    info_ = {};

    if (!wire_->begin(sda_pin, scl_pin, frequency)) {
        last_error_ = "failed to initialize I2C";
        return false;
    }

    pinMode(interrupt_pin_, INPUT_PULLUP);
    if (!resetController()) {
        return false;
    }

    for (uint8_t candidate : kCandidateAddresses) {
        address_ = candidate;
        if (!confirmDevice()) {
            continue;
        }
        if (!readFirmwareVersion()) {
            continue;
        }
        if (!readIcInfo()) {
            continue;
        }

        info_.address = address_;
        last_error_ = "none";
        return true;
    }

    address_ = 0;
    last_error_ = "GT6972P not found at 0x5D or 0x14";
    return false;
}

bool GoodixGT6972P::resetController()
{
    if (reset_pin_ < 0) {
        last_error_ = "invalid reset pin";
        return false;
    }

    pinMode(reset_pin_, OUTPUT);
    digitalWrite(reset_pin_, LOW);
    delay(2);
    digitalWrite(reset_pin_, HIGH);
    delay(100);
    return true;
}

bool GoodixGT6972P::confirmDevice()
{
    uint8_t expected[8];
    uint8_t actual[8] = {};
    memset(expected, 0xAA, sizeof(expected));

    for (uint8_t retry = 0; retry < 3; ++retry) {
        if (writeRegister(kBootOptionAddress, expected, sizeof(expected)) &&
            readRegister(kBootOptionAddress, actual, sizeof(actual)) &&
            memcmp(expected, actual, sizeof(expected)) == 0) {
            return true;
        }
        delay(5);
    }
    return false;
}

bool GoodixGT6972P::readFirmwareVersion()
{
    if (readFirmwareVersionAt(kFirmwareVersionAddress)) {
        berlin_a_registers_ = false;
        return true;
    }
    if (readFirmwareVersionAt(kFirmwareVersionAddressBerlinA)) {
        berlin_a_registers_ = true;
        return true;
    }

    last_error_ = "firmware version read or checksum failed";
    return false;
}

bool GoodixGT6972P::readFirmwareVersionAt(uint32_t address)
{
    uint8_t data[kFirmwareVersionLength] = {};

    for (uint8_t retry = 0; retry < 2; ++retry) {
        if (readRegister(address, data, sizeof(data)) &&
            checksumValid(data, sizeof(data))) {
            copyPid(info_.rom_pid, sizeof(info_.rom_pid), data, 6);
            memcpy(info_.rom_vid, data + 6, sizeof(info_.rom_vid));
            copyPid(info_.patch_pid, sizeof(info_.patch_pid), data + 10, 8);
            memcpy(info_.patch_vid, data + 18, sizeof(info_.patch_vid));
            info_.sensor_id = data[23];
            return true;
        }
        delay(10);
    }
    return false;
}

bool GoodixGT6972P::readIcInfo()
{
    const uint32_t primary = berlin_a_registers_ ? kIcInfoAddressBerlinA
                                                 : kIcInfoAddress;
    const uint32_t fallback = berlin_a_registers_ ? kIcInfoAddress
                                                  : kIcInfoAddressBerlinA;

    if (readIcInfoAt(primary)) {
        return true;
    }
    if (readIcInfoAt(fallback)) {
        berlin_a_registers_ = !berlin_a_registers_;
        return true;
    }

    last_error_ = "IC information read, checksum, or format failed";
    return false;
}

bool GoodixGT6972P::readIcInfoAt(uint32_t address)
{
    uint8_t header[4] = {};
    uint8_t data[kIcInfoMaxLength] = {};

    for (uint8_t retry = 0; retry < 3; ++retry) {
        if (!readRegister(address, header, sizeof(header))) {
            delay(5);
            continue;
        }

        const size_t length = readLe16(header);
        if (length < 8 || length >= sizeof(data)) {
            delay(5);
            continue;
        }
        if (!readRegister(address, data, length) ||
            !checksumValid(data, length)) {
            delay(5);
            continue;
        }

        info_.info_version = data[3];
        const bool parsed = info_.info_version == 0x02
                                ? parseIcInfoV2(data, length)
                                : parseIcInfoV1(data, length);
        if (parsed && info_.touch_data_address != 0 &&
            info_.point_struct_length >= kBytesPerPoint) {
            return true;
        }
    }
    return false;
}

bool GoodixGT6972P::parseIcInfoV1(const uint8_t *data, size_t length)
{
    constexpr size_t kVersionLength = 16;
    constexpr size_t kFeatureLength = 10;
    constexpr size_t kMiscMinimumLength = 56;

    if (length < 2 + kVersionLength + kFeatureLength + 5 +
                     kMiscMinimumLength + 2) {
        return false;
    }

    size_t offset = 2;
    info_.config_id = readLe32(data + offset + 4);
    info_.config_version = data[offset + 8];
    offset += kVersionLength;

    info_.stylus_feature = readLe16(data + offset + 8);
    offset += kFeatureLength;

    if (offset + 4 > length - 2) {
        return false;
    }
    info_.driver_channels = data[offset++];
    info_.sensor_channels = data[offset++];
    offset += 2; // button and force channel counts

    uint8_t ignored_count = 0;
    if (!skipFrequencyList(data, length, offset, ignored_count) ||
        !skipFrequencyList(data, length, offset, ignored_count) ||
        !skipFrequencyList(data, length, offset, ignored_count) ||
        !skipFrequencyList(data, length, offset, ignored_count) ||
        !skipFrequencyList(data, length, offset,
                           info_.stylus_frequency_count)) {
        return false;
    }

    if (offset + kMiscMinimumLength > length - 2) {
        return false;
    }

    info_.frame_data_address = readLe32(data + offset + 24);
    info_.touch_data_address = readLe32(data + offset + 44);
    info_.touch_data_head_length = readLe16(data + offset + 48);
    info_.point_struct_length = readLe16(data + offset + 50);
    info_.max_x = readLe16(data + offset + 52);
    info_.max_y = readLe16(data + offset + 54);
    return true;
}

bool GoodixGT6972P::parseIcInfoV2(const uint8_t *data, size_t length)
{
    constexpr size_t kCompatibleHeadLength = 10;
    constexpr size_t kVersionPackageLength = 16;
    constexpr size_t kBspPackageLength = 8;
    constexpr size_t kAddressPackageLength = 28;
    constexpr size_t kProtocolPackageLength = 24;

    if (length < 2 + kCompatibleHeadLength + kVersionPackageLength +
                     kBspPackageLength + 4 + 5 + kAddressPackageLength +
                     kProtocolPackageLength + 2) {
        return false;
    }

    size_t offset = 2 + kCompatibleHeadLength;
    info_.config_id = readLe32(data + offset + 4);
    info_.config_version = data[offset + 8];
    offset += kVersionPackageLength;

    info_.driver_channels = data[offset + 4];
    info_.sensor_channels = data[offset + 5];
    offset += kBspPackageLength;

    offset += 4; // variable-length parameter package header
    uint8_t ignored_count = 0;
    if (!skipFrequencyList(data, length, offset, ignored_count) ||
        !skipFrequencyList(data, length, offset, ignored_count) ||
        !skipFrequencyList(data, length, offset, ignored_count) ||
        !skipFrequencyList(data, length, offset, ignored_count) ||
        !skipFrequencyList(data, length, offset,
                           info_.stylus_frequency_count)) {
        return false;
    }

    if (offset + kAddressPackageLength + kProtocolPackageLength >
        length - 2) {
        return false;
    }

    offset += kAddressPackageLength;
    info_.frame_data_address = readLe32(data + offset + 4);
    info_.touch_data_address = readLe32(data + offset + 11);
    info_.touch_data_head_length = readLe16(data + offset + 15);
    info_.point_struct_length = readLe16(data + offset + 17);
    info_.max_x = readLe16(data + offset + 19);
    info_.max_y = readLe16(data + offset + 21);

    const uint8_t stylus_coordinate_length = data[offset + 23];
    info_.stylus_feature =
        (info_.stylus_frequency_count != 0 || stylus_coordinate_length != 0)
            ? 1
            : 0;
    return true;
}

bool GoodixGT6972P::skipFrequencyList(const uint8_t *data, size_t length,
                                      size_t &offset, uint8_t &count)
{
    if (offset >= length - 2) {
        return false;
    }

    count = data[offset++];
    if (count > kMaxFrequencyCount ||
        offset + static_cast<size_t>(count) * 2 > length - 2) {
        return false;
    }
    offset += static_cast<size_t>(count) * 2;
    return true;
}

GoodixGT6972P::ReadResult GoodixGT6972P::readEvent(Event &event)
{
    event = {};
    if (!wire_ || info_.touch_data_address == 0) {
        last_error_ = "driver is not initialized";
        return ReadResult::kBusError;
    }

    uint8_t buffer[kEventBufferLength] = {};
    if (!readRegister(info_.touch_data_address, buffer,
                      kInitialEventReadLength)) {
        last_error_ = "event read failed";
        return ReadResult::kBusError;
    }
    if (buffer[0] == 0) {
        return ReadResult::kNoData;
    }

    if (!clearEvent()) {
        last_error_ = "event acknowledge failed";
        return ReadResult::kBusError;
    }
    if (!checksumValid(buffer, kEventHeadLength)) {
        last_error_ = "event header checksum failed";
        return ReadResult::kChecksumError;
    }

    const uint8_t event_status = buffer[0];
    event.has_request = (event_status & kRequestEvent) != 0;
    event.has_gesture = (event_status & kGestureEvent) != 0;
    event.request_code = event.has_request ? buffer[2] : 0;
    event.gesture_code = event.has_gesture ? buffer[4] : 0;

    if ((event_status & kTouchEvent) == 0) {
        return ReadResult::kEvent;
    }
    event.has_touch = true;

    uint8_t record_count = buffer[2] & 0x0F;
    const bool has_key = ((buffer[3] >> 4) & 0x01) != 0;
    if (has_key) {
        ++record_count;
    }
    if (record_count > kMaxTouchPoints + 1) {
        last_error_ = "event contains too many records";
        return ReadResult::kFormatError;
    }

    size_t buffer_length = kInitialEventReadLength;
    if (record_count > 2) {
        const size_t extra_length =
            static_cast<size_t>(record_count) * kBytesPerPoint;
        if (buffer_length + extra_length > sizeof(buffer) ||
            !readRegister(info_.touch_data_address + buffer_length,
                          buffer + buffer_length, extra_length)) {
            last_error_ = "remaining event records read failed";
            return ReadResult::kBusError;
        }
        buffer_length += extra_length;
    }

    size_t data_offset = kEventHeadLength;
    size_t checksum_length = 0;
    for (uint8_t index = 0; index < record_count; ++index) {
        if (data_offset >= buffer_length) {
            last_error_ = "event record exceeds buffer";
            return ReadResult::kFormatError;
        }

        const uint8_t point_type = buffer[data_offset] & 0x0F;
        const uint8_t point_id = (buffer[data_offset] >> 4) & 0x0F;
        size_t record_length = 0;

        if (point_type == kPointTypeStylusHover ||
            point_type == kPointTypeStylus) {
            record_length = kBytesPerStylus;
            if (data_offset + record_length > buffer_length) {
                last_error_ = "stylus record exceeds buffer";
                return ReadResult::kFormatError;
            }

            event.pen.present = true;
            event.pen.hover = point_type == kPointTypeStylusHover;
            event.pen.x = readLe16(buffer + data_offset + 2);
            event.pen.y = readLe16(buffer + data_offset + 4);
            event.pen.pressure = readLe16(buffer + data_offset + 6);
            const int16_t raw_tilt_x =
                static_cast<int16_t>(readLe16(buffer + data_offset + 8));
            const int16_t raw_tilt_y =
                static_cast<int16_t>(readLe16(buffer + data_offset + 10));
            event.pen.tilt_x = static_cast<int8_t>(raw_tilt_x / 100);
            event.pen.tilt_y = static_cast<int8_t>(raw_tilt_y / 100);

            const uint8_t button_map = (buffer[3] & 0x0F) >> 1;
            event.pen.button1 = (button_map & 0x01) != 0;
            event.pen.button2 = (button_map & 0x02) != 0;
            event.pen.eraser = (button_map & 0x04) != 0;
            if (event.pen.hover && buffer[4] <= 100) {
                pen_battery_ = static_cast<int8_t>(buffer[4]);
            }
            event.pen.battery = pen_battery_;
        } else if (point_type == kPointTypeFinger ||
                   point_type == kPointTypeGlove) {
            record_length = info_.point_struct_length;
            if (record_length < kBytesPerPoint ||
                data_offset + record_length > buffer_length ||
                point_id >= kMaxTouchPoints ||
                event.finger_count >= kMaxTouchPoints) {
                last_error_ = "invalid finger record";
                return ReadResult::kFormatError;
            }

            FingerPoint &point = event.fingers[event.finger_count++];
            point.id = point_id;
            point.glove = point_type == kPointTypeGlove;
            point.x = readLe16(buffer + data_offset + 2);
            point.y = readLe16(buffer + data_offset + 4);
            point.width = readLe16(buffer + data_offset + 6);
        } else if (point_type == kPointTypeKey) {
            record_length = kBytesPerPoint;
            if (data_offset + record_length > buffer_length) {
                last_error_ = "key record exceeds buffer";
                return ReadResult::kFormatError;
            }
            event.key_mask =
                static_cast<uint16_t>(((buffer[data_offset + 3] & 0x03)
                                       << 8) |
                                      buffer[data_offset + 2]);
        } else {
            last_error_ = "unsupported point record type";
            return ReadResult::kFormatError;
        }

        data_offset += record_length;
        checksum_length += record_length;
    }

    if (record_count > 0) {
        if (kEventHeadLength + checksum_length + 2 > buffer_length) {
            last_error_ = "event checksum exceeds buffer";
            return ReadResult::kFormatError;
        }
        if (!checksumValid(buffer + kEventHeadLength, checksum_length + 2)) {
            last_error_ = "event payload checksum failed";
            return ReadResult::kChecksumError;
        }
    }

    last_error_ = "none";
    return ReadResult::kEvent;
}

bool GoodixGT6972P::readRegister(uint32_t reg, uint8_t *data, size_t length)
{
    if (!wire_ || address_ == 0 || !data) {
        return false;
    }

    size_t position = 0;
    while (position < length) {
        const size_t chunk = min(kI2cChunkLength, length - position);
        bool success = false;

        for (uint8_t retry = 0; retry < kBusRetryCount; ++retry) {
            const uint32_t address = reg + position;
            const uint8_t address_bytes[4] = {
                static_cast<uint8_t>(address >> 24),
                static_cast<uint8_t>(address >> 16),
                static_cast<uint8_t>(address >> 8),
                static_cast<uint8_t>(address),
            };

            wire_->beginTransmission(address_);
            const size_t address_written =
                wire_->write(address_bytes, sizeof(address_bytes));
            if (address_written != sizeof(address_bytes) ||
                wire_->endTransmission(false) != 0) {
                delay(2);
                continue;
            }

            const size_t received = wire_->requestFrom(
                static_cast<int>(address_), static_cast<int>(chunk));
            if (received != chunk) {
                while (wire_->available()) {
                    wire_->read();
                }
                delay(2);
                continue;
            }

            for (size_t i = 0; i < chunk; ++i) {
                data[position + i] = static_cast<uint8_t>(wire_->read());
            }
            success = true;
            break;
        }

        if (!success) {
            return false;
        }
        position += chunk;
    }
    return true;
}

bool GoodixGT6972P::writeRegister(uint32_t reg, const uint8_t *data,
                                  size_t length)
{
    if (!wire_ || address_ == 0 || (!data && length != 0)) {
        return false;
    }

    size_t position = 0;
    do {
        const size_t chunk = min(kI2cChunkLength, length - position);
        bool success = false;

        for (uint8_t retry = 0; retry < kBusRetryCount; ++retry) {
            const uint32_t address = reg + position;
            const uint8_t address_bytes[4] = {
                static_cast<uint8_t>(address >> 24),
                static_cast<uint8_t>(address >> 16),
                static_cast<uint8_t>(address >> 8),
                static_cast<uint8_t>(address),
            };

            wire_->beginTransmission(address_);
            const size_t address_written =
                wire_->write(address_bytes, sizeof(address_bytes));
            const size_t data_written = wire_->write(data + position, chunk);
            if (address_written == sizeof(address_bytes) &&
                data_written == chunk && wire_->endTransmission() == 0) {
                success = true;
                break;
            }
            delay(20);
        }

        if (!success) {
            return false;
        }
        position += chunk;
    } while (position < length);
    return true;
}

bool GoodixGT6972P::clearEvent()
{
    const uint8_t sync_clean = 0;
    return writeRegister(info_.touch_data_address, &sync_clean, 1);
}

bool GoodixGT6972P::interruptAsserted() const
{
    return interrupt_pin_ >= 0 && digitalRead(interrupt_pin_) == LOW;
}

bool GoodixGT6972P::checksumValid(const uint8_t *data, size_t length)
{
    if (!data || length < 2) {
        return false;
    }

    uint32_t calculated = 0;
    for (size_t i = 0; i < length - 2; ++i) {
        calculated += data[i];
    }
    const uint16_t expected = readLe16(data + length - 2);
    return static_cast<uint16_t>(calculated) == expected;
}

uint16_t GoodixGT6972P::readLe16(const uint8_t *data)
{
    return static_cast<uint16_t>(data[0]) |
           (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t GoodixGT6972P::readLe32(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void GoodixGT6972P::copyPid(char *destination, size_t destination_length,
                            const uint8_t *source, size_t source_length)
{
    if (!destination || destination_length == 0) {
        return;
    }

    const size_t copy_length = min(destination_length - 1, source_length);
    size_t i = 0;
    for (; i < copy_length; ++i) {
        const uint8_t value = source[i];
        if (value == 0x00 || value == 0xFF) {
            break;
        }
        destination[i] = value >= 0x20 && value <= 0x7E
                             ? static_cast<char>(value)
                             : '.';
    }
    destination[i] = '\0';
}
