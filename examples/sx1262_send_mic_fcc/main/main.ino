/*
  SX1262 packet transmit example using the same radio parameters
  as SX126x_Transmit_CE.ino.
*/

// include the library
#include <RadioLib.h>
#include "utilities.h"
#include "ExtensionIOXL9555.hpp"

static constexpr uint32_t TX_INTERVAL_MS = 500;
static constexpr float RADIO_FREQ_MHZ = 915.0f;
static constexpr float RADIO_BANDWIDTH_KHZ = 500.0f;
static constexpr uint32_t RADIO_SPREADING_FACTOR = 5;
static constexpr uint32_t RADIO_CODING_RATE = 6;
static constexpr int8_t RADIO_TX_POWER_DBM = 7;
static constexpr uint8_t RADIO_SYNC_WORD = 0xAB;
static constexpr uint32_t RADIO_PREAMBLE_LENGTH = 15;
static constexpr bool RADIO_CRC_ENABLED = false;
static constexpr float RADIO_TCXO_VOL = 3.0f;
static constexpr uint8_t RADIO_CURRENT_LIMIT_MA = 140;

static uint8_t payload[] = {1, 2, 3, 4, 5};

ExtensionIOXL9555 io;
// SX1262 has the following connections:
// NSS pin:   10
// DIO1 pin:  2
// NRST pin:  3
// BUSY pin:  9
SX1262 radio = new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY);

int transmissionState = RADIOLIB_ERR_NONE;
volatile bool transmittedFlag = false;

void setFlag(void)
{
    // we sent a packet, set the flag
    transmittedFlag = true;
}

void settingLoRaParams()
{
    if (radio.setFrequency(RADIO_FREQ_MHZ) == RADIOLIB_ERR_INVALID_FREQUENCY)
    {
        Serial.println(F("Selected frequency is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setBandwidth(RADIO_BANDWIDTH_KHZ) == RADIOLIB_ERR_INVALID_BANDWIDTH)
    {
        Serial.println(F("Selected bandwidth is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setSpreadingFactor(RADIO_SPREADING_FACTOR) == RADIOLIB_ERR_INVALID_SPREADING_FACTOR)
    {
        Serial.println(F("Selected spreading factor is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setCodingRate(RADIO_CODING_RATE) == RADIOLIB_ERR_INVALID_CODING_RATE)
    {
        Serial.println(F("Selected coding rate is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setSyncWord(RADIO_SYNC_WORD) != RADIOLIB_ERR_NONE)
    {
        Serial.println(F("Unable to set sync word!"));
        while (true)
            ;
    }

    if (radio.setOutputPower(RADIO_TX_POWER_DBM) == RADIOLIB_ERR_INVALID_OUTPUT_POWER)
    {
        Serial.println(F("Selected output power is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setCurrentLimit(RADIO_CURRENT_LIMIT_MA) == RADIOLIB_ERR_INVALID_CURRENT_LIMIT)
    {
        Serial.println(F("Selected current limit is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setPreambleLength(RADIO_PREAMBLE_LENGTH) == RADIOLIB_ERR_INVALID_PREAMBLE_LENGTH)
    {
        Serial.println(F("Selected preamble length is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setCRC(RADIO_CRC_ENABLED) == RADIOLIB_ERR_INVALID_CRC_CONFIGURATION)
    {
        Serial.println(F("Selected CRC is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setTCXO(RADIO_TCXO_VOL) == RADIOLIB_ERR_INVALID_TCXO_VOLTAGE)
    {
        Serial.println(F("Selected TCXO voltage is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setDio2AsRfSwitch() != RADIOLIB_ERR_NONE)
    {
        Serial.println(F("Failed to set DIO2 as RF switch!"));
        while (true)
            ;
    }
}

void io_extend_lora_gps_power_on(bool en)
{
    const uint8_t chip_address = XL9555_SLAVE_ADDRESS0;

    if (!io.init(Wire, BOARD_SDA, BOARD_SCL, chip_address))
    {
        while (1)
        {
            Serial.println("Failed to find XL9555 - check your wiring!");
            delay(1000);
        }
    }

    // Set PORT0 as input,mask = 0xFF = all pin input
    io.configPort(ExtensionIOXL9555::PORT0, 0x00);
    // Set PORT1 as input,mask = 0xFF = all pin input
    io.configPort(ExtensionIOXL9555::PORT1, 0xFF);

    Serial.println("Power on LoRa and GPS!");
    if (en)
        io.digitalWrite(ExtensionIOXL9555::IO0, HIGH);
    else
        io.digitalWrite(ExtensionIOXL9555::IO0, LOW);

    delay(1500);
}

void setup()
{
    // lora and sd use the same spi, in order to avoid mutual influence;
    // before powering on, all CS signals should be pulled high and in an unselected state;
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    Serial.begin(115200);

    // This must be turned on, otherwise LoRa and GPS will not work
    io_extend_lora_gps_power_on(true);

    SPI.begin(BOARD_SPI_SCLK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    // initialize SX1262 with default settings
    Serial.print(F("[SX1262] Initializing ... "));
    int state = radio.begin(RADIO_FREQ_MHZ);
    if (state == RADIOLIB_ERR_NONE)
    {
        Serial.println(F("success!"));
    }
    else
    {
        Serial.print(F("failed, code "));
        Serial.println(state);
        while (true)
            ;
    }

    // set the function that will be called
    // when packet transmission is finished
    radio.setPacketSentAction(setFlag);

    settingLoRaParams();

    Serial.println(F("All settings succesfully changed!"));

    // start transmitting the first packet
    Serial.print(F("[SX1262] Sending first packet ... "));

    transmissionState = radio.startTransmit(payload, sizeof(payload));
    Serial.println(F("success!"));
}

void loop()
{
    if (transmittedFlag)
    {
        transmittedFlag = false;

        if (transmissionState == RADIOLIB_ERR_NONE)
        {
            Serial.println(F("transmission finished!"));
        }
        else
        {
            Serial.print(F("failed, code "));
            Serial.println(transmissionState);
        }

        radio.finishTransmit();
        delay(TX_INTERVAL_MS);
        transmissionState = radio.startTransmit(payload, sizeof(payload));
    }

    delay(1);
}
