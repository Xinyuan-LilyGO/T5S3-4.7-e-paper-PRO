# GT6972P touch and active pen test

This example validates the Goodix GT6972P over I2C and prints finger and active
pen events to the serial monitor at 115200 baud.

Pins are identical to `examples/touch`:

| Signal | ESP32-S3 GPIO |
| --- | ---: |
| SDA | 39 |
| SCL | 40 |
| INT | 3 |
| RST | 9 |

The example automatically tries I2C addresses `0x5D` and `0x14`. On startup it
prints the firmware PID/VID, dynamically discovered touch-data register, panel
range, and advertised stylus capability. Runtime output includes multi-touch
coordinates and active-pen hover, pressure, tilt, buttons, eraser, and battery.

The shared Arduino driver in `lib/GoodixGT6972P` follows the register and event
protocol in `goodix_berlin_driver-Beta-v1.4.4.4` and retains the reference
driver's GPL-2.0-only license.

Build and monitor with:

```text
pio run -e T5_E_PAPER_S3_V7
pio device monitor -b 115200
```
