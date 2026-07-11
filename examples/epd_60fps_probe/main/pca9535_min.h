#pragma once

#include <Arduino.h>
#include <Wire.h>

class Pca9535Min {
public:
    bool begin(TwoWire &wire, uint8_t address);
    bool configureProbeDefaults();
    bool readState(uint8_t &config0, uint8_t &config1,
                   uint8_t &output0, uint8_t &output1);
    bool readPowerGood(bool &power_good);
    bool setOutputMask(uint8_t port, uint8_t mask, bool high);
    bool safeShutdownOutputs();

private:
    static constexpr uint8_t kRegInput0 = 0x00;
    static constexpr uint8_t kRegInput1 = 0x01;
    static constexpr uint8_t kRegOutput0 = 0x02;
    static constexpr uint8_t kRegOutput1 = 0x03;
    static constexpr uint8_t kRegPolarity0 = 0x04;
    static constexpr uint8_t kRegPolarity1 = 0x05;
    static constexpr uint8_t kRegConfig0 = 0x06;
    static constexpr uint8_t kRegConfig1 = 0x07;

    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegister(uint8_t reg, uint8_t &value);

    TwoWire *wire_ = nullptr;
    uint8_t address_ = 0;
    bool present_ = false;
    uint8_t output0_ = 0;
    uint8_t output1_ = 0;
};
