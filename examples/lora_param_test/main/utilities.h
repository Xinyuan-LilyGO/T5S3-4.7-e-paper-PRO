#pragma once

#define BOARD_SCL  (40)
#define BOARD_SDA  (39)

#define BOARD_SPI_MISO    (21)
#define BOARD_SPI_MOSI    (13)
#define BOARD_SPI_SCLK    (14)

#define TOUCH_SCL  (BOARD_SCL)
#define TOUCH_SDA  (BOARD_SDA)
#define TOUCH_INT  (3)
#define TOUCH_RST  (9)

#define RTC_SCL (BOARD_SCL)
#define RTC_SDA (BOARD_SDA)
#define RTC_IRQ (2)

#define SD_MISO    (BOARD_SPI_MISO)
#define SD_MOSI    (BOARD_SPI_MOSI)
#define SD_SCLK    (BOARD_SPI_SCLK)
#define SD_CS      (12)

#define LORA_MISO (BOARD_SPI_MISO)
#define LORA_MOSI (BOARD_SPI_MOSI)
#define LORA_SCLK (BOARD_SPI_SCLK)
#define LORA_CS   (46)
#define LORA_IRQ  (10)
#define LORA_RST  (1)
#define LORA_BUSY (47)

#define BL_EN (11)

#define BOOT_BTN (0)


