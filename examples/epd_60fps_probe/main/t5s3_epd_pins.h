#pragma once

#include <stdint.h>

#ifndef EPD_ACTIVE_X
#define EPD_ACTIVE_X 432
#endif

#ifndef EPD_ACTIVE_Y
#define EPD_ACTIVE_Y 30
#endif

#ifndef EPD_ACTIVE_WIDTH
#define EPD_ACTIVE_WIDTH 432
#endif

#ifndef EPD_ACTIVE_HEIGHT
#define EPD_ACTIVE_HEIGHT 480
#endif

namespace t5s3_epd {

static constexpr const char *kBoardName = "LilyGO T5 E-Paper S3 Pro";

static constexpr uint16_t kPanelWidth = 960;
static constexpr uint16_t kPanelHeight = 540;

// The raw scanner can target either the centered probe area or the complete
// panel; the active geometry is selected by the EPD_ACTIVE_* macros above.
static constexpr uint16_t kActiveX = EPD_ACTIVE_X;
static constexpr uint16_t kActiveY = EPD_ACTIVE_Y;
static constexpr uint16_t kActiveWidth = EPD_ACTIVE_WIDTH;
static constexpr uint16_t kActiveHeight = EPD_ACTIVE_HEIGHT;

static constexpr uint8_t kI2cSda = 39;
static constexpr uint8_t kI2cScl = 40;
static constexpr uint8_t kPca9535Address = 0x20;
static constexpr uint8_t kTps65185Address = 0x68;

static constexpr uint8_t kPcaBitEpdOe = 0;
static constexpr uint8_t kPcaBitEpdMode = 1;
static constexpr uint8_t kPcaBitTpsPwrup = 3;
static constexpr uint8_t kPcaBitVcomCtrl = 4;
static constexpr uint8_t kPcaBitTpsWakeup = 5;
static constexpr uint8_t kPcaBitTpsPwrGood = 6;

static constexpr uint8_t kPcaMaskTpsPwrGood = 1U << kPcaBitTpsPwrGood;
static constexpr uint8_t kPcaMaskShutdownOutputs =
    (1U << kPcaBitEpdOe) |
    (1U << kPcaBitEpdMode) |
    (1U << kPcaBitTpsPwrup) |
    (1U << kPcaBitVcomCtrl) |
    (1U << kPcaBitTpsWakeup);

}  // namespace t5s3_epd

#ifndef TARGET_FPS
#define TARGET_FPS 24
#endif

#ifndef EPD_DIRTY_PADDING
#define EPD_DIRTY_PADDING 8
#endif

#ifndef EPD_BUS_HZ
#define EPD_BUS_HZ 26600000UL
#endif

#ifndef EPD_VIDEO_TOP_DUMMY_LINES
#define EPD_VIDEO_TOP_DUMMY_LINES 30
#endif

#ifndef EPD_VIDEO_BOTTOM_DUMMY_LINES
#define EPD_VIDEO_BOTTOM_DUMMY_LINES 30
#endif

#ifndef EPD_VCOM_MV
#define EPD_VCOM_MV -1600
#endif
