#define EPD_ACTIVE_X 0
#define EPD_ACTIVE_Y 0
#define EPD_ACTIVE_WIDTH 960
#define EPD_ACTIVE_HEIGHT 540
#define EPD_VIDEO_TOP_DUMMY_LINES 0
#define EPD_VIDEO_BOTTOM_DUMMY_LINES 0
#define TARGET_FPS 60
#define EPD_BUS_HZ 26600000UL
#define EPD_VIDEO_INVERT_INPUT 1

// Keep one implementation of the raw scanner so improvements made for the
// probe are automatically used by this UI example as well.
#include "../epd_60fps_probe/main/pca9535_min.cpp"
#include "../epd_60fps_probe/main/epd_video.cpp"
