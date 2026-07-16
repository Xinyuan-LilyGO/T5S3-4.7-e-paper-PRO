#pragma once

#include <stdint.h>

#ifndef EPD_ACTIVE_X
#define EPD_ACTIVE_X 0
#endif

#ifndef EPD_ACTIVE_Y
#define EPD_ACTIVE_Y 0
#endif

#ifndef EPD_ACTIVE_WIDTH
#define EPD_ACTIVE_WIDTH 960
#endif

#ifndef EPD_ACTIVE_HEIGHT
#define EPD_ACTIVE_HEIGHT 540
#endif

namespace t5s3_epd {

static constexpr const char *kBoardName = "LilyGO T5 E-Paper S3 Pro";

static constexpr uint16_t kPanelWidth = 960;
static constexpr uint16_t kPanelHeight = 540;

// The raw scanner refreshes the complete panel by default. The active geometry
// can still be overridden with the EPD_ACTIVE_* build flags for experiments.
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
#define TARGET_FPS 60
#endif

#ifndef EPD_BUS_HZ
#define EPD_BUS_HZ 26600000UL
#endif

#ifndef EPD_VIDEO_TOP_DUMMY_LINES
#define EPD_VIDEO_TOP_DUMMY_LINES 0
#endif

#ifndef EPD_VIDEO_BOTTOM_DUMMY_LINES
#define EPD_VIDEO_BOTTOM_DUMMY_LINES 0
#endif

#ifndef EPD_VCOM_MV
#define EPD_VCOM_MV -1600
#endif
