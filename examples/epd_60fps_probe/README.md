# EPD 60 FPS Probe

This example bypasses the normal full-frame EPD update path and drives the
parallel panel with the ESP-IDF I80 peripheral and DMA. It is intended for
refresh-rate and ghosting experiments, not normal application rendering.

In `platformio.ini`, comment out the currently selected `src_dir` and enable
`src_dir = examples/epd_60fps_probe/main`. Then build and upload with:

```text
pio run -e T5_E_PAPER_S3_V7 -t upload
```

The driver source enables `-O3` for its timing-critical loop and defaults to
`60 FPS` with a `26.6 MHz` I80 clock. If the panel shows
tearing, noise, or excessive ghosting, try `-DTARGET_FPS=30` or
`-DTARGET_FPS=24`, and reduce `-DEPD_BUS_HZ=24000000UL` if needed. The VCOM
default is `-1600 mV`; change `-DEPD_VCOM_MV` only when the panel's measured
configuration requires it.
