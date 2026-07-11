#pragma once

#include <stddef.h>
#include <stdint.h>

class Pca9535Min;

bool epd_video_init(Pca9535Min &expander);
bool epd_video_power_on();
bool epd_video_start();
uint8_t *epd_video_get_backbuffer();
size_t epd_video_get_backbuffer_size();
void epd_video_flip(uint16_t dirty_y, uint16_t dirty_height);
uint32_t epd_video_get_vsync_count();
void epd_video_shutdown();
