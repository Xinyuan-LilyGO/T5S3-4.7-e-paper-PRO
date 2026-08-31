# T5 E-paper S3 Pro Pin Map

> This document is compiled from `hardware/T5 E-paper S3 Pro V1.0 24-12-24.pdf`.

## 1. Direct MCU GPIO

| Function | Suggested Name | GPIO / Signal | Source | Notes |
| --- | --- | --- | --- | --- |
| Main I2C SDA | `PIN_I2C_SDA` | 39 | Schematic Page 2/3/4 | Board net `II2C_SDA`, shared by all I2C devices |
| Main I2C SCL | `PIN_I2C_SCL` | 40 | Schematic Page 2/3/4 | Board net `II2C_SCL`, shared by all I2C devices |
| PCA9535 interrupt | `PIN_PCA9535_INT` | 38 | Schematic Page 2/3 | `PCA_INT` |
| RTC interrupt | `PIN_RTC_INT` | 2 | Schematic Page 2/3 | `RTC_INT` |
| Touch interrupt | `PIN_TOUCH_INT` | 3 | Schematic Page 2/3 | `T_INT` |
| Touch reset | `PIN_TOUCH_RST` | 9 | Schematic Page 2/3 | `T_RST` |
| USB D- | `PIN_USB_DM` | 19 | Schematic Page 1/2 | Net `DM` |
| USB D+ | `PIN_USB_DP` | 20 | Schematic Page 1/2 | Net `DP` |
| BOOT button | `PIN_BOOT` | 0 | Schematic Page 2 | `IO0`, low level enters download mode |
| ESP32 `U0TXD -> GPS_RX` | `PIN_GPS_TX` | 43 | Schematic Page 2 | The schematic labels this as `U0TXD`; on ESP32-S3 this maps to GPIO43 |
| ESP32 `U0RXD <- GPS_TX` | `PIN_GPS_RX` | 44 | Schematic Page 2 | The schematic labels this as `U0RXD`; on ESP32-S3 this maps to GPIO44 |
| Reset pin | `PIN_RESET_EN` | `EN` | Schematic Page 2 | Not a regular GPIO |

> Note: `GPIO35`, `GPIO36`, and `GPIO37` do not have board-level net names in the Page 2 core diagram, so they are treated as unused in this revision.

## 2. E-paper Panel And Backlight

| Function | Suggested Name | GPIO | Source | Notes |
| --- | --- | --- | --- | --- |
| EPD CKH | `PIN_EPD_CKH` | 4 | Schematic Page 2/3 | Net `EP_CKH` |
| EPD D0 | `PIN_EPD_D0` | 5 | Schematic Page 2/3 | Net `EP_D0` |
| EPD D1 | `PIN_EPD_D1` | 6 | Schematic Page 2/3 | Net `EP_D1` |
| EPD D2 | `PIN_EPD_D2` | 7 | Schematic Page 2/3 | Net `EP_D2` |
| EPD D7 | `PIN_EPD_D7` | 8 | Schematic Page 2/3 | Net `EP_D7` |
| EPD D3 | `PIN_EPD_D3` | 15 | Schematic Page 2/3 | Net `EP_D3` |
| EPD D4 | `PIN_EPD_D4` | 16 | Schematic Page 2/3 | Net `EP_D4` |
| EPD D5 | `PIN_EPD_D5` | 17 | Schematic Page 2/3 | Net `EP_D5` |
| EPD D6 | `PIN_EPD_D6` | 18 | Schematic Page 2/3 | Net `EP_D6` |
| EPD STV | `PIN_EPD_STV` | 45 | Schematic Page 2/3 | Net `EP_STV` |
| EPD CKV | `PIN_EPD_CKV` | 48 | Schematic Page 2/3 | Net `EP_CKV` |
| EPD STH | `PIN_EPD_STH` | 41 | Schematic Page 2/3 | Net `EP_STH` |
| EPD LE | `PIN_EPD_LE` | 42 | Schematic Page 2/3 | Net `EP_LE` |
| Backlight enable / PWM | `PIN_BL_EN` | 11 | Schematic Page 2/3 | `BL_EN` drives `PT4103B23F EN` |

> Note: EPD `EP_OE`, `EP_MODE`, and `EP_VCOM` are not directly connected to the MCU. They are controlled through `PCA9535` and `TPS651851`.

> Note: `PIN_BL_EN` drives the `EN` pin of the `PT4103B23F` LED boost driver, and PWM is the way to set brightness. Keep the PWM frequency at or below about 1 kHz. 0% duty is off and 100% duty is full LED current.

## 3. PCA9535 GPIO Expander

I2C address: `0x20`. Interrupt output: `PCA_INT -> GPIO38`.

| PCA9535 | Suggested Name | Board Net | Direction | Source | Notes |
| --- | --- | --- | --- | --- | --- |
| IO0_0 | `PIN_PCA9535_LORA_EN` | `LORA_EN` | Output | Schematic Page 1/3 | Enables the shared LoRa / GPS 3.3V rail |
| IO0_1 | - | NC | - | Schematic Page 3 | Not connected |
| IO0_2 | - | NC | - | Schematic Page 3 | Not connected |
| IO0_3 | - | NC | - | Schematic Page 3 | Not connected |
| IO0_4 | - | NC | - | Schematic Page 3 | Not connected |
| IO0_5 | - | NC | - | Schematic Page 3 | Not connected |
| IO0_6 | - | NC | - | Schematic Page 3 | Not connected |
| IO0_7 | - | NC | - | Schematic Page 3 | Not connected |
| IO1_0 | `PIN_PCA9535_EPD_OE` | `EP_OE` | Output | Schematic Page 3 | EPD output enable |
| IO1_1 | `PIN_PCA9535_EPD_MODE` | `EP_MODE` | Output | Schematic Page 3 | EPD mode control |
| IO1_2 | `PIN_PCA9535_BUTTON` | `BUTTON` | Input | Schematic Page 2/3 | On-board function button `S3` |
| IO1_3 | `PIN_PCA9535_TPS_PWRUP` | `TPS_PWRUP` | Output | Schematic Page 3/4 | EPD power-up sequence control |
| IO1_4 | `PIN_PCA9535_VCOM_CTRL` | `VCOM_CTRL` | Output | Schematic Page 3/4 | EPD VCOM control |
| IO1_5 | `PIN_PCA9535_TPS_WAKEUP` | `TPS_WAKEUP` | Output | Schematic Page 3/4 | Wakes `TPS651851` |
| IO1_6 | `PIN_PCA9535_TPS_PWR_GOOD` | `TPS_PWR_GOOD` | Input | Schematic Page 3/4 | Power-good status |
| IO1_7 | `PIN_PCA9535_TPS_INT` | `TPS_INT` | Input | Schematic Page 3/4 | `TPS651851` interrupt |

## 4. LoRa / GPS / SD Card

### SPI Bus

| Function | Suggested Name | GPIO | Source | Notes |
| --- | --- | --- | --- | --- |
| SPI MISO | `PIN_SPI_MISO` | 21 | Schematic Page 2 | Shared by LoRa and SD |
| SPI SCK | `PIN_SPI_SCK` | 14 | Schematic Page 2 | Shared by LoRa and SD |
| SPI MOSI | `PIN_SPI_MOSI` | 13 | Schematic Page 2 | Shared by LoRa and SD |

### LoRa

| Function | Suggested Name | GPIO / Mapping | Source | Notes |
| --- | --- | --- | --- | --- |
| LoRa CS | `PIN_LORA_CS` | 46 | Schematic Page 2 | Net `LORA_CS` |
| LoRa IRQ | `PIN_LORA_IRQ` | 10 | Schematic Page 2 | Net `LORA_IRQ` |
| LoRa RST | `PIN_LORA_RST` | 1 | Schematic Page 2 | Net `LORA_RST` |
| LoRa BUSY | `PIN_LORA_BUSY` | 47 | Schematic Page 2 | Net `LORA_BUSY` |
| LoRa power enable | `PIN_LORA_EN` | `PCA9535 IO0_0` | Schematic Page 1/3 | Enables the shared 3.3V rail |

### GPS

| Function | Suggested Name | GPIO / Mapping | Source | Notes |
| --- | --- | --- | --- | --- |
| ESP32 `TX -> GPS_RX` | `PIN_GPS_TX` | 43 (`U0TXD`) | Schematic Page 2 | Routed through `R11` to `GPS_RX` |
| ESP32 `RX <- GPS_TX` | `PIN_GPS_RX` | 44 (`U0RXD`) | Schematic Page 2 | Routed through `R12` to `GPS_TX` |
| GPS power enable | `PIN_GPS_EN` | `PCA9535 IO0_0` | Schematic Page 1/3 | Shared with LoRa as `LORA_EN` |

Two GNSS modules ship on this board, the `MIA-M10Q` and the `L76K`. They use the same footprint, a board carrying one cannot be told from a board carrying the other by looking at it, and the only way to know which one is fitted is to probe it in software. The default baud rates differ:

| Module | Default baud |
| --- | --- |
| L76K | 9600 |
| MIA-M10Q | 38400 |

`examples/GPS/main/main.ino` already does that probe: it opens the UART at 9600, and if the L76K setup gets no answer it reopens at 38400 and runs the u-blox recovery. A fixed baud rate in user code works on one module and stays silent on the other.

### SD Card

| Function | Suggested Name | GPIO | Source | Notes |
| --- | --- | --- | --- | --- |
| SD SPI MISO | `PIN_SD_MISO` | 21 | Schematic Page 2 | Shared SPI bus |
| SD SPI SCK | `PIN_SD_SCK` | 14 | Schematic Page 2 | Shared SPI bus |
| SD SPI MOSI | `PIN_SD_MOSI` | 13 | Schematic Page 2 | Shared SPI bus |
| SD SPI CS | `PIN_SD_CS` | 12 | Schematic Page 2 | Dedicated chip select |

## 5. Touch / RTC / EPD Power Control

### GT911 Touch

| Function | Suggested Name | GPIO / Mapping | Source | Notes |
| --- | --- | --- | --- | --- |
| Touch I2C SDA | `PIN_TOUCH_SDA` | 39 | Schematic Page 2/3 | Shared main I2C |
| Touch I2C SCL | `PIN_TOUCH_SCL` | 40 | Schematic Page 2/3 | Shared main I2C |
| Touch INT | `PIN_TOUCH_INT` | 3 | Schematic Page 2/3 | `T_INT` |
| Touch RST | `PIN_TOUCH_RST` | 9 | Schematic Page 2/3 | `T_RST` |

### RTC `PCF8563TS`

| Function | Suggested Name | GPIO / Mapping | Source | Notes |
| --- | --- | --- | --- | --- |
| RTC I2C SDA | `PIN_RTC_SDA` | 39 | Schematic Page 2/3 | Shared main I2C |
| RTC I2C SCL | `PIN_RTC_SCL` | 40 | Schematic Page 2/3 | Shared main I2C |
| RTC INT | `PIN_RTC_INT` | 2 | Schematic Page 2/3 | `RTC_INT` |
| RTC backup battery | `PIN_RTC_VRTC` | `VRTC` | Schematic Page 3 | Connected through `J12` coin-cell holder |

### `TPS651851` / EPD Power

| Function | Suggested Name | GPIO / Mapping | Source | Notes |
| --- | --- | --- | --- | --- |
| TPS I2C SDA | `PIN_TPS_SDA` | 39 | Schematic Page 3/4 | Shared main I2C |
| TPS I2C SCL | `PIN_TPS_SCL` | 40 | Schematic Page 3/4 | Shared main I2C |
| TPS `PWRUP` | `PIN_TPS_PWRUP` | `PCA9535 IO1_3` | Schematic Page 3/4 | Power-up sequence control |
| TPS `WAKEUP` | `PIN_TPS_WAKEUP` | `PCA9535 IO1_5` | Schematic Page 3/4 | Wake control |
| TPS `PWR_GOOD` | `PIN_TPS_PWR_GOOD` | `PCA9535 IO1_6` | Schematic Page 3/4 | Status input |
| TPS `INT` | `PIN_TPS_INT` | `PCA9535 IO1_7` | Schematic Page 3/4 | Status input |
| `VCOM_CTRL` | `PIN_EPD_VCOM_CTRL` | `PCA9535 IO1_4` | Schematic Page 3/4 | Controls `EP_VCOM` |

## 6. I2C Address Table

| Device | Address | Source | Notes |
| --- | --- | --- | --- |
| PCA9535PW | `0x20` | Schematic Page 3 | IO expander |
| PCF8563TS | `0x51` | Schematic Page 3 | RTC |
| BQ27220YZFR | `0x55` | Schematic Page 1 | Fuel gauge |
| GT911 | `0x5D` | Schematic Page 3 | Touch controller |
| TPS651851RSLR | `0x68` | Schematic Page 4 | EPD power management |
| BQ25896 | `0x6B` | Schematic Page 1 | Battery charger |

> Note: all devices above are on the shared main I2C bus, `GPIO39 / GPIO40`.

## 7. Code Notes

- The `Pins` section in `README.md`, plus the macros in `examples/factory/main/utilities.h` and `examples/FastEPD_factory/main/utilities.h`, mostly match this schematic.
- The product table in `README.md` lists the RTC as `PCF85063 (0x51)`, but schematic Page 3 / U3 shows `PCF8563TS`. If the drivers differ, prefer the schematic and the actual mounted component.
- GPS is shown in the schematic as `U0TXD/U0RXD -> GPS_RX/GPS_TX`. The repository maps this as `BOARD_GPS_TXD = 43` and `BOARD_GPS_RXD = 44`, which is consistent with ESP32-S3 pin naming.
- The shared LoRa / GPS 3.3V rail is not always enabled. Before using either module, raise `PCA9535 IO0_0 / LORA_EN`.
