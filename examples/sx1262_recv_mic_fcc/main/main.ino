/*
   SX1262 LoRa receive example aligned to examples/sx1262_ce_mic_fcc.

   Use this receiver together with the transmitter example when
   examples/sx1262_ce_mic_fcc runs in LoRa packet mode.
*/

#include <RadioLib.h>
#include "utilities.h"
#include "ExtensionIOXL9555.hpp"

static constexpr float RADIO_FREQ_MHZ = 915.0f;
static constexpr float RADIO_BW_KHZ = 500.0f;
static constexpr uint8_t RADIO_SF = 10;
static constexpr uint8_t RADIO_CODING_RATE = 6;
static constexpr uint8_t RADIO_SYNC_WORD = 0x6B;
static constexpr uint8_t RADIO_CURRENT_LIMIT_MA = 140;
static constexpr uint16_t RADIO_PREAMBLE_LEN = 15;
static constexpr float RADIO_TCXO_VOLTAGE = 2.4f;

ExtensionIOXL9555 io;
SX1262 radio = new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY);

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

    io.configPort(ExtensionIOXL9555::PORT0, 0x00);
    io.configPort(ExtensionIOXL9555::PORT1, 0xFF);

    Serial.println("Power on LoRa and GPS!");
    io.digitalWrite(ExtensionIOXL9555::IO0, en ? HIGH : LOW);

    delay(1500);
}

volatile bool receivedFlag = false;

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void setFlag(void)
{
    receivedFlag = true;
}

void setup()
{
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    Serial.begin(115200);

    io_extend_lora_gps_power_on(true);

    SPI.begin(BOARD_SPI_SCLK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

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

    radio.setPacketReceivedAction(setFlag);

    if (radio.setFrequency(RADIO_FREQ_MHZ) == RADIOLIB_ERR_INVALID_FREQUENCY)
    {
        Serial.println(F("Selected frequency is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setBandwidth(RADIO_BW_KHZ) == RADIOLIB_ERR_INVALID_BANDWIDTH)
    {
        Serial.println(F("Selected bandwidth is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setSpreadingFactor(RADIO_SF) == RADIOLIB_ERR_INVALID_SPREADING_FACTOR)
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

    if (radio.setCurrentLimit(RADIO_CURRENT_LIMIT_MA) == RADIOLIB_ERR_INVALID_CURRENT_LIMIT)
    {
        Serial.println(F("Selected current limit is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setPreambleLength(RADIO_PREAMBLE_LEN) == RADIOLIB_ERR_INVALID_PREAMBLE_LENGTH)
    {
        Serial.println(F("Selected preamble length is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setCRC(false) == RADIOLIB_ERR_INVALID_CRC_CONFIGURATION)
    {
        Serial.println(F("Selected CRC is invalid for this module!"));
        while (true)
            ;
    }

    if (radio.setTCXO(RADIO_TCXO_VOLTAGE) == RADIOLIB_ERR_INVALID_TCXO_VOLTAGE)
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

    Serial.println(F("All settings succesfully changed!"));

    Serial.print(F("[SX1262] Starting to listen ... "));
    state = radio.startReceive();
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
}

void loop()
{
    if (receivedFlag)
    {
        receivedFlag = false;

        String str;
        int state = radio.readData(str);

        if (state == RADIOLIB_ERR_NONE)
        {
            Serial.println(F("[SX1262] Received packet!"));

            Serial.print(F("[SX1262] Data:\t\t"));
            Serial.println(str);

            Serial.print(F("[SX1262] RSSI:\t\t"));
            Serial.print(radio.getRSSI());
            Serial.println(F(" dBm"));

            Serial.print(F("[SX1262] SNR:\t\t"));
            Serial.print(radio.getSNR());
            Serial.println(F(" dB"));

            Serial.print(F("[SX1262] Frequency error:\t"));
            Serial.print(radio.getFrequencyError());
            Serial.println(F(" Hz"));
        }
        else if (state == RADIOLIB_ERR_CRC_MISMATCH)
        {
            Serial.println(F("CRC error!"));
        }
        else
        {
            Serial.print(F("failed, code "));
            Serial.println(state);
        }
    }

    delay(1);
}
