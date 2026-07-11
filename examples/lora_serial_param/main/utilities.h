#pragma once

#define BOARD_SCL  (40)
#define BOARD_SDA  (39)

#define BOARD_SPI_MISO    (21)
#define BOARD_SPI_MOSI    (13)
#define BOARD_SPI_SCLK    (14)

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

