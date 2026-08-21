
<h1 align = "center">🏆T5_E_Paper_S3_Pro🏆</h1>

<p align="center">
  <a href="./README.md">English</a> | <b>中文</b>
</p>

![Build Status](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO/actions/workflows/platformio.yml/badge.svg?event=push)

<p>
<!-- <img src="https://img.shields.io/badge/ESP—IDF-5.1.1-ff3034" height="20px"></a> -->
<img src="https://img.shields.io/badge/PlatformIO-6.5.0-ff7f00" height="20px"></a>
<img src="https://img.shields.io/badge/Arduino-2.0.14-008284" height="20px"></a>
</p>

| 正面 | 反面 |
| :---: | :---: |
| ![alt text](./docs/README_img/T5_S3_正面.png) | ![alt text](./docs/README_img/T5_S3_反面.png) |

## :zero: 版本 🎁

### 1、版本

| 版本 | TPS65185 | GPS | LoRa | 分支 | 购买链接 |
| :---: | :---: | :---: | :---: | :---: | :---: |
| T5 E-Paper S3 Pro | ✅ | ✅ | ✅ | [H752-01](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO) | [LilyGo Store](https://lilygo.cc/products/t5-e-paper-s3-pro?_pos=2&_sid=e9bdb39ec&_ss=r) |
| T5 E-Paper S3 Pro Lite | ✅ | ❌ | ❌ | [H752-01](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO) | [LilyGo Store](https://lilygo.cc/products/t5-e-paper-s3-pro-lite?_pos=1&_sid=e9bdb39ec&_ss=r) |
| H752 | ❌ | ❌ | ✅ | [H752](https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO/tree/H752) | 已售罄 |

说明：`Lite` 版本和 `Pro` 版本共用同一份原理图，但 `Lite` 版本不带 LoRa 和 GPS。

## :one: 产品 🎁

| H752-01 | [T5 E-Paper S3 Pro](https://lilygo.cc/products/t5-e-paper-s3-pro) |
| :---: | :---: |
| MCU | ESP32-S3-WROOM-1 |
| Flash / PSRAM | 16M / 8M |
| LoRa | SX1262 |
| GPS | MIA-M10Q / L76K |
| Driver IC | ED047TC1（4.7 英寸，960x540，16 级灰度） |
| 电池容量 | 3.7V-1500mAh |
| 电池芯片 | BQ25896 (0x6B), BQ27220 (0x55) |
| 触摸 | GT911 (0x5D) |
| RTC | PCF85063 (0x51) |
| E-link 电源驱动 | TPS65185 (0x68) |
| IO 扩展 | PCA9535PW (0x20) |

➡ **T5_E_Paper_S3_Pro 相关项目**：

- [ [FastEPD](https://github.com/Xinyuan-LilyGO/FastEPD) ]：针对 ESP32 并口墨水屏优化的驱动库

## :two: 更新程序 🎁

下载程序前，请先将设备连接到电脑，选择对应的 COM 端口，并让设备进入下载模式：

1. 按住 BOOT 键不要松开；
2. 点击背面的 RST 按键后松开；
3. 最后松开 BOOT 键。

![alt text](./docs/flash_download_tool/image.png)

### 2.1. 使用 `LILYGO Spark` 下载程序（推荐）

- 从 [LILYGO Spark](https://lilygo.cc/en-us/pages/lilygo-spark?srsltid=AfmBOoorTB7ptFu2LQNLRnoI2SA0zBGJTN6JpI9J3hmHEkKhBQSmeu0Y) 下载软件。

- 搜索你的设备名称，并下载对应程序。

![alt text](./docs/README_img/lilygo_spark.png)

示例：

| 固件 | 说明 | Github |
| --- | --- | --- |
| T5_E_PAPER_S3_PRO_V1.0_20260506 | 出厂程序<br> | - |
| corsspoint_lilygo_t5s3_e_paper | 阅读器程序 | [T5S3-Reader](https://github.com/ShallowGreen123/T5S3-Reader) |
| T5S3-GameBoy              | 游戏机模拟器     | [T5S3-GameBoy](https://github.com/ShallowGreen123/T5S3-GameBoy) |

### 2.2. 使用 ESP 官方 `flash_download_tool` 下载程序

演示示例位于 `firmware/` 目录下的 `examples/factory` 文件夹中。

参考文档：[flash_download_tool](./docs/flash_download_tool/flash_download_tool.md)

## :three: 快速开始 🎁

🟢 推荐使用 PlatformIO，因为本仓库示例基于 PlatformIO 开发。🟢

### 3.1. PlatformIO

1. 安装 [Visual Studio Code](https://code.visualstudio.com/) 和 [Python](https://www.python.org/)，并克隆或下载本项目；
2. 在 Visual Studio Code 扩展中搜索并安装 `PlatformIO` 插件；
3. 安装完成后重启 Visual Studio Code；
4. 打开本项目后，PlatformIO 会自动下载依赖库和相关组件，首次下载耗时较长，请耐心等待；
5. 依赖安装完成后，打开 `platformio.ini`，取消注释需要运行的示例配置，然后按 `ctrl+s` 保存；
6. 点击 VS Code 底部的 :ballot_box_with_check: 编译按钮，连接 USB 并选择对应 COM 端口；
7. 最后点击 :arrow_right: 按钮将程序下载到 Flash。

### 3.2. Arduino IDE

1. 安装 [Arduino IDE](https://www.arduino.cc/en/software)。

2. 将 `this project/lib/` 下的所有文件复制到 Arduino 库目录中，通常为 `C:\Users\YourName\Documents\Arduino\libraries`。

3. 打开 Arduino IDE，点击左上角 `File->Open`，打开本项目 `examples/xxx/xxx.ino` 下的示例。

4. 按下表配置 Arduino IDE，配置完成后即可点击左上角按钮编译并下载。

| Arduino IDE 设置 | 值 |
| --- | --- |
| Board | ***ESP32S3 Dev Module*** |
| Port | Your port |
| USB CDC On Boot | Enable |
| CPU Frequency | 240MHZ(WiFi) |
| Core Debug Level | None |
| USB DFU On Boot | Disable |
| Erase All Flash Before Sketch Upload | Disable |
| Events Run On | Core1 |
| Flash Mode | QIO 80MHZ |
| Flash Size | **16MB(128Mb)** |
| Arduino Runs On | Core1 |
| USB Firmware MSC On Boot | Disable |
| Partition Scheme | **16M Flash(3M APP/9.9MB FATFS)** |
| PSRAM | **OPI PSRAM** |
| Upload Mode | **UART0/Hardware CDC** |
| Upload Speed | 921600 |
| USB Mode | **CDC and JTAG** |

### 3.3. 目录结构

~~~
├─boards      : PlatformIO 板级配置文件；
├─DXF         : 板卡和外壳尺寸图（DXF/DWG 格式）；
├─docs        : 文档图片；
├─examples    : 各硬件功能测试示例；
├─firmware    : 预编译出厂固件；
├─hardware    : 原理图和芯片数据手册；
├─lib         : 项目使用的第三方库；
~~~

### 3.4. 示例

| 示例 | 路径 | 说明 |
| :--- | :--- | :--- |
| bq25896 | examples/bq25896 | BQ25896 充电管理 IC 测试 |
| bq27220 | examples/bq27220 | BQ27220 电量计 IC 测试 |
| factory | examples/factory | 出厂固件程序 |
| GPS | examples/GPS | GPS 模块测试（需要室外环境） |
| io_extend | examples/io_extend | PCA9535PW I2C IO 扩展 IC 测试 |
| lora_recv | examples/lora_recv | SX1262 LoRa 接收测试 |
| lora_send | examples/lora_send | SX1262 LoRa 发送测试 |
| lvgl_test | examples/lvgl_test | LVGL 图形库测试 |
| nvs_test | examples/nvs_test | NVS（非易失性存储）测试 |
| sd_card | examples/sd_card | SD 卡读写测试 |
| touch | examples/touch | GT911 电容触摸 IC 测试 |
| touch_gt6972p | examples/touch_gt6972p | GT6972P 触摸及主动笔测试 |
| touch_gt6972p_pen_display | examples/touch_gt6972p_pen_display | GT6972P 主动笔屏幕书写测试 |

## :four: 引脚 🎁

### 4.1 引脚映射

[./docs/pinmap_cn.md](./docs/pinmap_cn.md)

### 4.2 引脚定义

[./docs/pin_define.md](./docs/pin_define.md)

## :five: 测试 🎁

休眠功耗。

![alt text](./docs/README_img/image-2.png)

## :six: FAQ 🎁

| 文档 | 链接 |
| :---: | :---: |
| 如何通过 `flash_download_tool` 下载程序？ | [docs](./docs/flash_download_tool/flash_download_tool.md) |

## :seven: 原理图与 3D 🎁

更多资料请查看 `./hardware` 目录。

原理图：[T5_E-Paper-S3-Pro](./hardware/T5%20E-paper%20S3%20Pro%20V1.0%2024-12-24.pdf)

[板卡尺寸](./DXF/H752-Board%20size.dxf)

[外壳尺寸](./DXF/H752-Shell%20size.dwg)
