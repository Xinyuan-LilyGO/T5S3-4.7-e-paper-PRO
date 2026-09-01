// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <Arduino.h>
#include <M5GFX.h>

#include "DiaryInk.h"
#include "OracleClient.h"
#include "ReplyRenderer.h"

namespace riddle {

struct PenEvent {
    bool present = false;
    bool hover = false;
    bool eraser = false;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t pressure = 0;
    uint32_t captured_ms = 0;
};

class DiaryController {
public:
    using QualityRefreshHook = bool (*)();

    bool begin(lgfx::LovyanGFX &surface);
    void setQualityRefreshHook(QualityRefreshHook hook);
    void handlePen(const PenEvent &event);
    void tick();
    void clearPage(bool quality_refresh);

private:
    enum class State : uint8_t {
        Listening,
        Drinking,
        Thinking,
        Replying,
        Lingering,
        FadingReply,
    };

    static constexpr uint32_t kIdleCommitMs = 2800;
    static constexpr uint32_t kStrokeGapMs = 180;
    static constexpr uint32_t kDrinkStepMs = 70;
    static constexpr uint32_t kReplyStepMs = 24;
    static constexpr uint32_t kFadeStepMs = 80;
    static constexpr uint32_t kThinkingPulseMs = 450;
    static constexpr uint8_t kDrinkStages = 14;
    static constexpr uint8_t kFadeStages = 10;
    static constexpr uint16_t kPenMaxPressure = 4096;
    static constexpr int kMinBrushRadius = 1;
    static constexpr int kMaxBrushRadius = 8;
    static constexpr int kEraserRadius = 14;

    void releasePen(uint32_t now);
    int pressureRadius(uint16_t pressure) const;
    bool startCommit(uint32_t now);
    void startReply(const OracleResult &result, uint32_t now);
    void startFade(uint32_t now);
    void drawThinkingPulse();
    void markDirty(const Rect &rect);
    void flushDirty();
    bool qualityRefresh();
    static bool due(uint32_t now, uint32_t deadline);

    lgfx::LovyanGFX *surface_ = nullptr;
    QualityRefreshHook quality_refresh_hook_ = nullptr;
    DiaryInk ink_;
    OracleClient oracle_;
    ReplyRenderer reply_;
    State state_ = State::Listening;
    Rect dirty_ = {};
    Rect effect_region_ = {};
    bool pen_down_ = false;
    bool pen_eraser_ = false;
    bool last_pen_valid_ = false;
    bool oracle_submitted_ = false;
    uint8_t effect_stage_ = 0;
    uint8_t pulse_stage_ = 0;
    uint32_t last_pen_ms_ = 0;
    uint32_t next_action_ms_ = 0;
    uint32_t linger_until_ms_ = 0;
    uint32_t last_present_ms_ = 0;
};

} // namespace riddle
