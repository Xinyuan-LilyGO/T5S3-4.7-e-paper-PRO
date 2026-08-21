#pragma once

// Reuse the reference raw scanner, but scan the complete panel for the UI.
#define EPD_ACTIVE_X 0
#define EPD_ACTIVE_Y 0
#define EPD_ACTIVE_WIDTH 960
#define EPD_ACTIVE_HEIGHT 540
#define EPD_VIDEO_TOP_DUMMY_LINES 0
#define EPD_VIDEO_BOTTOM_DUMMY_LINES 0
#define TARGET_FPS 24
#define EPD_BUS_HZ 26600000UL
#define EPD_VIDEO_INVERT_INPUT 1

#include "../epd_60fps_probe/main/t5s3_epd_pins.h"
#include "../epd_60fps_probe/main/pca9535_min.h"
#include "../epd_60fps_probe/main/epd_video.h"
