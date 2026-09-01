// SPDX-License-Identifier: GPL-2.0-only

#include "DiaryController.h"

#include <algorithm>
#include <new>

#include "RiddleDisplay.h"

namespace {

riddle::Rect unite(const riddle::Rect &a, const riddle::Rect &b)
{
    if (a.w <= 0 || a.h <= 0) {
        return b;
    }
    if (b.w <= 0 || b.h <= 0) {
        return a;
    }
    const int left = std::min(a.x, b.x);
    const int top = std::min(a.y, b.y);
    const int right = std::max(a.x + a.w, b.x + b.w);
    const int bottom = std::max(a.y + a.h, b.y + b.h);
    return {left, top, right - left, bottom - top};
}

} // namespace

namespace riddle {

bool DiaryController::begin(lgfx::LovyanGFX &surface)
{
    surface_ = &surface;
    if (!ink_.begin(surface.width(), surface.height())) {
        Serial.printf("[RIDDLE] ink bitmap allocation failed: %s\n",
                      ink_.lastError() != nullptr ? ink_.lastError() :
                      "unknown error");
        return false;
    }
    if (!oracle_.begin()) {
        Serial.println("[RIDDLE] oracle worker allocation failed");
        return false;
    }

    surface_->setTextWrap(false);
    surface_->fillScreen(TFT_WHITE);
    markDirty({0, 0, surface_->width(), surface_->height()});
    Serial.println("[RIDDLE] diary controller ready");
    return true;
}

void DiaryController::setQualityRefreshHook(QualityRefreshHook hook)
{
    quality_refresh_hook_ = hook;
}

void DiaryController::handlePen(const PenEvent &event)
{
    if (surface_ == nullptr) {
        return;
    }
    const bool contact = event.present && !event.hover;
    if (state_ != State::Listening) {
        if (!contact) {
            pen_down_ = false;
        } else if (state_ == State::Lingering) {
            startFade(event.captured_ms);
        }
        return;
    }

    if (!contact) {
        releasePen(event.captured_ms);
        return;
    }

    if (pen_down_ && event.captured_ms - last_pen_ms_ >= kStrokeGapMs) {
        ink_.penUp();
    }
    const bool mode_changed = pen_down_ && pen_eraser_ != event.eraser;
    if (mode_changed) {
        ink_.penUp();
        pen_down_ = false;
    }

    Rect dirty;
    if (event.eraser) {
        dirty = ink_.erasePoint(*surface_, event.x, event.y, kEraserRadius);
    } else {
        dirty = ink_.penPoint(*surface_, event.x, event.y,
                              pressureRadius(event.pressure));
    }
    pen_down_ = true;
    pen_eraser_ = event.eraser;
    last_pen_valid_ = false;
    last_pen_ms_ = event.captured_ms;
    markDirty(dirty);
}

void DiaryController::tick()
{
    if (surface_ == nullptr) {
        return;
    }
    const uint32_t now = millis();

    if (state_ == State::Listening) {
        OracleResult orphan;
        while (oracle_.poll(orphan)) {
            Serial.println("[RIDDLE] discarded response for a cleared page");
        }
        if (last_pen_valid_ && !pen_down_ && !ink_.isEmpty() &&
            now - last_pen_ms_ >= kIdleCommitMs && !oracle_.busy()) {
            startCommit(now);
        }
    } else if (state_ == State::Drinking && due(now, next_action_ms_)) {
        dissolvePass(*surface_, effect_region_, effect_stage_, kDrinkStages);
        markDirty(effect_region_);
        ++effect_stage_;
        if (effect_stage_ >= kDrinkStages) {
            surface_->fillRect(effect_region_.x, effect_region_.y,
                               effect_region_.w, effect_region_.h, TFT_WHITE);
            markDirty(effect_region_);
            state_ = State::Thinking;
            next_action_ms_ = now;
            pulse_stage_ = 0;
        } else {
            next_action_ms_ = now + kDrinkStepMs;
        }
    } else if (state_ == State::Thinking) {
        OracleResult result;
        if (oracle_submitted_ && oracle_.poll(result)) {
            oracle_submitted_ = false;
            startReply(result, now);
        } else if (due(now, next_action_ms_)) {
            drawThinkingPulse();
            next_action_ms_ = now + kThinkingPulseMs;
        }
    } else if (state_ == State::Replying && due(now, next_action_ms_)) {
        const Rect glyphs = reply_.drawNext(*surface_, 2);
        markDirty(glyphs);
        if (reply_.done()) {
            effect_region_ = reply_.region();
            const uint32_t linger = std::max<uint32_t>(4000,
                std::min<uint32_t>(20000,
                    3000 + static_cast<uint32_t>(reply_.textBytes()) * 45));
            linger_until_ms_ = now + linger;
            state_ = State::Lingering;
        } else {
            next_action_ms_ = now + kReplyStepMs;
        }
    } else if (state_ == State::Lingering && due(now, linger_until_ms_)) {
        startFade(now);
    } else if (state_ == State::FadingReply && due(now, next_action_ms_)) {
        dissolvePass(*surface_, effect_region_, effect_stage_, kFadeStages);
        markDirty(effect_region_);
        ++effect_stage_;
        if (effect_stage_ >= kFadeStages) {
            surface_->fillScreen(TFT_WHITE);
            dirty_ = {};
            if (!qualityRefresh()) {
                Serial.println("[RIDDLE] reply quality refresh failed");
                dirty_ = {0, 0, surface_->width(), surface_->height()};
            }
            reply_.clear();
            state_ = State::Listening;
            last_pen_valid_ = false;
            pen_down_ = false;
            Serial.println("[RIDDLE] listening");
        } else {
            next_action_ms_ = now + kFadeStepMs;
        }
    }

    flushDirty();
}

void DiaryController::clearPage(bool quality_refresh)
{
    if (surface_ == nullptr) {
        return;
    }
    ink_.penUp();
    ink_.clear();
    reply_.clear();
    surface_->fillScreen(TFT_WHITE);
    state_ = State::Listening;
    pen_down_ = false;
    last_pen_valid_ = false;
    oracle_submitted_ = false;
    if (quality_refresh) {
        dirty_ = {};
        if (!qualityRefresh()) {
            Serial.println("[RIDDLE] page quality refresh failed");
            dirty_ = {0, 0, surface_->width(), surface_->height()};
        }
    } else {
        dirty_ = {0, 0, surface_->width(), surface_->height()};
    }
    Serial.printf("[RIDDLE] page cleared%s\n",
                  quality_refresh ? " with quality refresh" : "");
}

void DiaryController::releasePen(uint32_t now)
{
    if (!pen_down_) {
        return;
    }
    ink_.penUp();
    pen_down_ = false;
    last_pen_valid_ = true;
    last_pen_ms_ = now;
}

int DiaryController::pressureRadius(uint16_t pressure) const
{
    const uint32_t limited = std::min<uint32_t>(pressure, kPenMaxPressure);
    return kMinBrushRadius +
        limited * (kMaxBrushRadius - kMinBrushRadius) / kPenMaxPressure;
}

bool DiaryController::startCommit(uint32_t now)
{
    PageBitmap *page = new (std::nothrow) PageBitmap();
    const char *snapshot_error = nullptr;
    const bool snapshot_ok =
        page != nullptr && ink_.snapshot(*page, &snapshot_error);
    if (!snapshot_ok) {
        if (page != nullptr) {
            page->reset();
            delete page;
        }
        last_pen_ms_ = now;
        Serial.printf("[RIDDLE] page capture deferred: %s\n",
                      snapshot_error != nullptr ? snapshot_error :
                      "out of memory");
        return false;
    }
    if (!oracle_.submit(page)) {
        page->reset();
        delete page;
        last_pen_ms_ = now;
        Serial.println("[RIDDLE] oracle queue busy; keeping page for retry");
        return false;
    }

    effect_region_ = ink_.bbox().toRect();
    oracle_submitted_ = true;
    ink_.clear();
    last_pen_valid_ = false;
    effect_stage_ = 0;
    next_action_ms_ = now;
    state_ = State::Drinking;
    Serial.printf("[RIDDLE] page committed (%dx%d)\n",
                  effect_region_.w, effect_region_.h);
    return true;
}

void DiaryController::startReply(const OracleResult &result, uint32_t now)
{
    String text;
    if (result.ok) {
        text = result.text;
    } else {
        text = "The diary is silent.\n";
        text += result.error[0] != '\0' ? result.error : "Unknown oracle error.";
    }

    surface_->fillScreen(TFT_WHITE);
    markDirty({0, 0, surface_->width(), surface_->height()});
    reply_.begin(*surface_, text.c_str());
    state_ = State::Replying;
    next_action_ms_ = now;
    Serial.printf("[RIDDLE] reply ready (%u bytes, HTTP %u)\n",
                  static_cast<unsigned>(text.length()), result.http_status);
}

void DiaryController::startFade(uint32_t now)
{
    effect_stage_ = 0;
    next_action_ms_ = now;
    state_ = State::FadingReply;
}

void DiaryController::drawThinkingPulse()
{
    const int x = surface_->width() / 2;
    const int y = surface_->height() / 2;
    const Rect pulse = {x - 24, y - 9, 48, 18};
    surface_->fillRect(pulse.x, pulse.y, pulse.w, pulse.h, TFT_WHITE);
    const uint8_t dots = static_cast<uint8_t>((pulse_stage_++ % 3) + 1);
    for (uint8_t i = 0; i < dots; ++i) {
        surface_->fillCircle(x - 12 + i * 12, y, 3, TFT_BLACK);
    }
    markDirty(pulse);
}

void DiaryController::markDirty(const Rect &rect)
{
    if (surface_ == nullptr || rect.w <= 0 || rect.h <= 0) {
        return;
    }
    const int left = std::max(0, rect.x);
    const int top = std::max(0, rect.y);
    const int right = std::min(surface_->width(), rect.x + rect.w);
    const int bottom = std::min(surface_->height(), rect.y + rect.h);
    if (left < right && top < bottom) {
        dirty_ = unite(dirty_, {left, top, right - left, bottom - top});
    }
}

void DiaryController::flushDirty()
{
    if (dirty_.w <= 0 || dirty_.h <= 0) {
        return;
    }
    const uint32_t now = millis();
    const uint32_t interval = pen_down_ ? 0 : 24;
    if (now - last_present_ms_ < interval) {
        return;
    }
    if (t5epd::present(dirty_.x, dirty_.y, dirty_.w, dirty_.h)) {
        dirty_ = {};
        last_present_ms_ = now;
    }
}

bool DiaryController::qualityRefresh()
{
    return quality_refresh_hook_ != nullptr
               ? quality_refresh_hook_()
               : t5epd::clean();
}

bool DiaryController::due(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

} // namespace riddle
