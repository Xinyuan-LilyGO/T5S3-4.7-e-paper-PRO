# T5 E-paper S3 Pro 引脚映射

> 本文按 `hardware/T5 E-paper S3 Pro V1.0 24-12-24.pdf` 整理。


## 1. 主控直连 GPIO

| 功能 | 建议名 | GPIO / 信号 | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| 主 I2C SDA | `PIN_I2C_SDA` | 39 | 原理图 Page 2/3/4 | 板级网络 `II2C_SDA`，全板共享 |
| 主 I2C SCL | `PIN_I2C_SCL` | 40 | 原理图 Page 2/3/4 | 板级网络 `II2C_SCL`，全板共享 |
| PCA9535 中断 | `PIN_PCA9535_INT` | 38 | 原理图 Page 2/3 | `PCA_INT` |
| RTC 中断 | `PIN_RTC_INT` | 2 | 原理图 Page 2/3 | `RTC_INT` |
| 触摸中断 | `PIN_TOUCH_INT` | 3 | 原理图 Page 2/3 | `T_INT` |
| 触摸复位 | `PIN_TOUCH_RST` | 9 | 原理图 Page 2/3 | `T_RST` |
| USB D- | `PIN_USB_DM` | 19 | 原理图 Page 1/2 | 网络 `DM` |
| USB D+ | `PIN_USB_DP` | 20 | 原理图 Page 1/2 | 网络 `DP` |
| BOOT 键 | `PIN_BOOT` | 0 | 原理图 Page 2 | `IO0`，低电平进入下载模式 |
| ESP32 `U0TXD -> GPS_RX` | `PIN_GPS_TX` | 43 | 原理图 Page 2 | 图中写 `U0TXD`，按 ESP32-S3 命名对应 GPIO43 |
| ESP32 `U0RXD <- GPS_TX` | `PIN_GPS_RX` | 44 | 原理图 Page 2 | 图中写 `U0RXD`，按 ESP32-S3 命名对应 GPIO44 |
| 复位脚 | `PIN_RESET_EN` | `EN` | 原理图 Page 2 | 不是普通 GPIO |

> 说明：`GPIO35`、`GPIO36`、`GPIO37` 在 Page 2 的 core 图里未见板级网络名，可视为当前版本未使用。

## 2. 墨水屏与背光

| 功能 | 建议名 | GPIO | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| EPD CKH | `PIN_EPD_CKH` | 4 | 原理图 Page 2/3 | 网络 `EP_CKH` |
| EPD D0 | `PIN_EPD_D0` | 5 | 原理图 Page 2/3 | 网络 `EP_D0` |
| EPD D1 | `PIN_EPD_D1` | 6 | 原理图 Page 2/3 | 网络 `EP_D1` |
| EPD D2 | `PIN_EPD_D2` | 7 | 原理图 Page 2/3 | 网络 `EP_D2` |
| EPD D7 | `PIN_EPD_D7` | 8 | 原理图 Page 2/3 | 网络 `EP_D7` |
| EPD D3 | `PIN_EPD_D3` | 15 | 原理图 Page 2/3 | 网络 `EP_D3` |
| EPD D4 | `PIN_EPD_D4` | 16 | 原理图 Page 2/3 | 网络 `EP_D4` |
| EPD D5 | `PIN_EPD_D5` | 17 | 原理图 Page 2/3 | 网络 `EP_D5` |
| EPD D6 | `PIN_EPD_D6` | 18 | 原理图 Page 2/3 | 网络 `EP_D6` |
| EPD STV | `PIN_EPD_STV` | 45 | 原理图 Page 2/3 | 网络 `EP_STV` |
| EPD CKV | `PIN_EPD_CKV` | 48 | 原理图 Page 2/3 | 网络 `EP_CKV` |
| EPD STH | `PIN_EPD_STH` | 41 | 原理图 Page 2/3 | 网络 `EP_STH` |
| EPD LE | `PIN_EPD_LE` | 42 | 原理图 Page 2/3 | 网络 `EP_LE` |
| 背光使能 / PWM | `PIN_BL_EN` | 11 | 原理图 Page 2/3 | `BL_EN` 进 `PT4103B23F EN` |

> 说明：EPD 的 `EP_OE`、`EP_MODE`、`EP_VCOM` 不直连主控，分别经过 `PCA9535` 和 `TPS651851` 控制。

> 说明：`PIN_BL_EN` 接的是 `PT4103B23F` 这颗 LED 升压驱动的 `EN` 脚，亮度用 PWM 调。PWM 频率不要超过约 1 kHz。0% 占空比是关，100% 占空比是满电流。

## 3. PCA9535 扩展 IO

I2C 地址：`0x20`，中断输出 `PCA_INT -> GPIO38`。

| PCA9535 | 建议名 | 板级网络 | 方向 | 来源 | 备注 |
| --- | --- | --- | --- | --- | --- |
| IO0_0 | `PIN_PCA9535_LORA_EN` | `LORA_EN` | 输出 | 原理图 Page 1/3 | 打开 LoRa / GPS 共用 3.3V 支路 |
| IO0_1 | - | NC | - | 原理图 Page 3 | 当前未接出 |
| IO0_2 | - | NC | - | 原理图 Page 3 | 当前未接出 |
| IO0_3 | - | NC | - | 原理图 Page 3 | 当前未接出 |
| IO0_4 | - | NC | - | 原理图 Page 3 | 当前未接出 |
| IO0_5 | - | NC | - | 原理图 Page 3 | 当前未接出 |
| IO0_6 | - | NC | - | 原理图 Page 3 | 当前未接出 |
| IO0_7 | - | NC | - | 原理图 Page 3 | 当前未接出 |
| IO1_0 | `PIN_PCA9535_EPD_OE` | `EP_OE` | 输出 | 原理图 Page 3 | EPD 输出使能 |
| IO1_1 | `PIN_PCA9535_EPD_MODE` | `EP_MODE` | 输出 | 原理图 Page 3 | EPD 模式控制 |
| IO1_2 | `PIN_PCA9535_BUTTON` | `BUTTON` | 输入 | 原理图 Page 2/3 | 板载功能键 `S3` |
| IO1_3 | `PIN_PCA9535_TPS_PWRUP` | `TPS_PWRUP` | 输出 | 原理图 Page 3/4 | EPD 电源上电序列 |
| IO1_4 | `PIN_PCA9535_VCOM_CTRL` | `VCOM_CTRL` | 输出 | 原理图 Page 3/4 | EPD VCOM 控制 |
| IO1_5 | `PIN_PCA9535_TPS_WAKEUP` | `TPS_WAKEUP` | 输出 | 原理图 Page 3/4 | 唤醒 `TPS651851` |
| IO1_6 | `PIN_PCA9535_TPS_PWR_GOOD` | `TPS_PWR_GOOD` | 输入 | 原理图 Page 3/4 | 电源良好状态 |
| IO1_7 | `PIN_PCA9535_TPS_INT` | `TPS_INT` | 输入 | 原理图 Page 3/4 | `TPS651851` 中断 |

## 4. LoRa / GPS / SD 卡

### SPI 总线

| 功能 | 建议名 | GPIO | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| SPI MISO | `PIN_SPI_MISO` | 21 | 原理图 Page 2 | LoRa / SD 共用 |
| SPI SCK | `PIN_SPI_SCK` | 14 | 原理图 Page 2 | LoRa / SD 共用 |
| SPI MOSI | `PIN_SPI_MOSI` | 13 | 原理图 Page 2 | LoRa / SD 共用 |

### LoRa

| 功能 | 建议名 | GPIO / 映射 | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| LoRa CS | `PIN_LORA_CS` | 46 | 原理图 Page 2 | 网络 `LORA_CS` |
| LoRa IRQ | `PIN_LORA_IRQ` | 10 | 原理图 Page 2 | 网络 `LORA_IRQ` |
| LoRa RST | `PIN_LORA_RST` | 1 | 原理图 Page 2 | 网络 `LORA_RST` |
| LoRa BUSY | `PIN_LORA_BUSY` | 47 | 原理图 Page 2 | 网络 `LORA_BUSY` |
| LoRa 电源使能 | `PIN_LORA_EN` | `PCA9535 IO0_0` | 原理图 Page 1/3 | 打开共用 3.3V 支路 |

### GPS

| 功能 | 建议名 | GPIO / 映射 | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| ESP32 `TX -> GPS_RX` | `PIN_GPS_TX` | 43 (`U0TXD`) | 原理图 Page 2 | 通过 `R11` 到 `GPS_RX` |
| ESP32 `RX <- GPS_TX` | `PIN_GPS_RX` | 44 (`U0RXD`) | 原理图 Page 2 | 通过 `R12` 到 `GPS_TX` |
| GPS 电源使能 | `PIN_GPS_EN` | `PCA9535 IO0_0` | 原理图 Page 1/3 | 与 LoRa 共用 `LORA_EN` |

这块板子上会装两种 GNSS 模块，`MIA-M10Q` 和 `L76K`。两者封装位置相同，光看板子分辨不出装的是哪一颗，只能在软件里探测。两者默认波特率不一样：

| 模块 | 默认波特率 |
| --- | --- |
| L76K | 9600 |
| MIA-M10Q | 38400 |

`examples/GPS/main/main.ino` 里已经做了这个探测：先用 9600 打开串口，如果 L76K 初始化没有响应，再用 38400 打开并走 u-blox 恢复流程。用户代码里写死一个波特率，只能在其中一种模块上工作，另一种上会完全收不到数据。

### SD 卡

| 功能 | 建议名 | GPIO | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| SD SPI MISO | `PIN_SD_MISO` | 21 | 原理图 Page 2 | 共用 SPI |
| SD SPI SCK | `PIN_SD_SCK` | 14 | 原理图 Page 2 | 共用 SPI |
| SD SPI MOSI | `PIN_SD_MOSI` | 13 | 原理图 Page 2 | 共用 SPI |
| SD SPI CS | `PIN_SD_CS` | 12 | 原理图 Page 2 | 独立片选 |

## 5. 触摸 / RTC / EPD 电源控制

### GT911 触摸

| 功能 | 建议名 | GPIO / 映射 | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| 触摸 I2C SDA | `PIN_TOUCH_SDA` | 39 | 原理图 Page 2/3 | 共用主 I2C |
| 触摸 I2C SCL | `PIN_TOUCH_SCL` | 40 | 原理图 Page 2/3 | 共用主 I2C |
| 触摸 INT | `PIN_TOUCH_INT` | 3 | 原理图 Page 2/3 | `T_INT` |
| 触摸 RST | `PIN_TOUCH_RST` | 9 | 原理图 Page 2/3 | `T_RST` |

### RTC `PCF8563TS`

| 功能 | 建议名 | GPIO / 映射 | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| RTC I2C SDA | `PIN_RTC_SDA` | 39 | 原理图 Page 2/3 | 共用主 I2C |
| RTC I2C SCL | `PIN_RTC_SCL` | 40 | 原理图 Page 2/3 | 共用主 I2C |
| RTC INT | `PIN_RTC_INT` | 2 | 原理图 Page 2/3 | `RTC_INT` |
| RTC 备份电池 | `PIN_RTC_VRTC` | `VRTC` | 原理图 Page 3 | 通过 `J12` 接钮扣电池 |

### `TPS651851` / EPD 电源

| 功能 | 建议名 | GPIO / 映射 | 来源 | 备注 |
| --- | --- | --- | --- | --- |
| TPS I2C SDA | `PIN_TPS_SDA` | 39 | 原理图 Page 3/4 | 共用主 I2C |
| TPS I2C SCL | `PIN_TPS_SCL` | 40 | 原理图 Page 3/4 | 共用主 I2C |
| TPS `PWRUP` | `PIN_TPS_PWRUP` | `PCA9535 IO1_3` | 原理图 Page 3/4 | 上电序列控制 |
| TPS `WAKEUP` | `PIN_TPS_WAKEUP` | `PCA9535 IO1_5` | 原理图 Page 3/4 | 唤醒 |
| TPS `PWR_GOOD` | `PIN_TPS_PWR_GOOD` | `PCA9535 IO1_6` | 原理图 Page 3/4 | 状态输入 |
| TPS `INT` | `PIN_TPS_INT` | `PCA9535 IO1_7` | 原理图 Page 3/4 | 状态输入 |
| `VCOM_CTRL` | `PIN_EPD_VCOM_CTRL` | `PCA9535 IO1_4` | 原理图 Page 3/4 | 控制 `EP_VCOM` |

## 6. I2C 地址表

| 设备 | 地址 | 来源 | 备注 |
| --- | --- | --- | --- |
| PCA9535PW | `0x20` | 原理图 Page 3 | IO 扩展 |
| PCF8563TS | `0x51` | 原理图 Page 3 | RTC |
| BQ27220YZFR | `0x55` | 原理图 Page 1 | 电量计 |
| GT911 | `0x5D` | 原理图 Page 3 | 触摸控制器 |
| TPS651851RSLR | `0x68` | 原理图 Page 4 | EPD 电源管理 |
| BQ25896 | `0x6B` | 原理图 Page 1 | 充电管理 |

> 说明：上述器件都挂在主 I2C 总线 `GPIO39 / GPIO40` 上。

## 7. 代码现状提示

- `README.md` 的 `Pins` 段落，以及 `examples/factory/main/utilities.h`、`examples/FastEPD_factory/main/utilities.h` 里的宏，和这份原理图基本一致。
- `README.md` 产品表把 RTC 写成了 `PCF85063 (0x51)`，但原理图 Page 3 / U3 是 `PCF8563TS`；如果驱动初始化有差异，应该以原理图和实际器件为准。
- `GPS` 在原理图里是 `U0TXD/U0RXD -> GPS_RX/GPS_TX`，仓库代码把它写成 `BOARD_GPS_TXD = 43`、`BOARD_GPS_RXD = 44`；这和 ESP32-S3 的引脚命名是一致的。
- LoRa 与 GPS 的 3.3V 支路不是一直上电，使用前需要先拉起 `PCA9535 IO0_0 / LORA_EN`。
