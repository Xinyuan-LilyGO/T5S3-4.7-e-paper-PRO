#include "epdiy.h"
#include <driver/gpio.h>
#include "utilities.h"

// #define PCA_PIN_P00       0x0001
// #define PCA_PIN_P01       0x0002
// #define PCA_PIN_P02       0x0004
// #define PCA_PIN_P03       0x0008
// #define PCA_PIN_P04       0x0010
// #define PCA_PIN_P05       0x0020
// #define PCA_PIN_P06       0x0040
// #define PCA_PIN_P07       0x0080
// #define PCA_PIN_PC10      0x0100
// #define PCA_PIN_PC11      0x0200
// #define PCA_PIN_PC12      0x0400     // Button
// #define PCA_PIN_PC13      0x0800
// #define PCA_PIN_PC14      0x1000
// #define PCA_PIN_PC15      0x2000
// #define PCA_PIN_PC16      0x4000
// #define PCA_PIN_PC17      0x8000

static bool interrupt_done = false;

static void IRAM_ATTR interrupt_handler(void* arg) {
    interrupt_done = true;
    printf("interrupt_handler\n");
}


void io_extend_set_config(uint8_t port, uint8_t mask)
{
    pca9555_set_config(BOARD_I2C_PORT, mask, port);
}

void io_extend_lora_gps_power_on(bool en)
{
    uint8_t value = 0xFF;
    uint8_t io_val = pca9555_read_input(BOARD_I2C_PORT, 0);

    printf("io_extend_lora_gps_power_on : 0x%x\n", io_val);
    // if(en) {
    //     value = io_val | PCA_PIN_P00;
    // } else {
    //     value = io_val & ~(PCA_PIN_P00);
    // }
    // pca9555_set_value(BOARD_I2C_PORT, value, 0);
}

bool button_read(void)
{
    uint8_t io_val = pca9555_read_input(BOARD_I2C_PORT, 1);
    return !(io_val & (PCA_PIN_PC12 >> 8));
}

uint8_t read_io(int io)
{
    uint8_t io_val = pca9555_read_input(BOARD_I2C_PORT, io);
    return io_val;
}

void set_config(i2c_port_t port, uint8_t config_value, int high_port)
{
    pca9555_set_config(port, config_value, high_port);
}

