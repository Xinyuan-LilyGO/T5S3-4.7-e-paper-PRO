// SPDX-License-Identifier: GPL-2.0-only

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>

#include "DiaryController.h"
#include "GoodixGT6972P.h"
#include "RiddleDisplay.h"

namespace {

constexpr int kTouchSda = 39;
constexpr int kTouchScl = 40;
constexpr int kTouchInterrupt = 3;
constexpr int kTouchReset = 9;
constexpr int kBootPin = 0;
constexpr uint16_t kTouchWidth = 684;
constexpr uint16_t kTouchHeight = 1216;
constexpr uint16_t kTouchQueueLength = 256;
constexpr uint32_t kTouchTaskStackSize = 4096;
constexpr UBaseType_t kTouchTaskPriority = 4;
constexpr BaseType_t kTouchTaskCore = 0;
constexpr uint32_t kUiTaskStackSize = 12288;
constexpr UBaseType_t kUiTaskPriority = 2;
constexpr BaseType_t kUiTaskCore = 0;
constexpr uint32_t kFallbackPollMs = 10;
constexpr uint32_t kTouchRetryMs = 5000;
constexpr uint32_t kBootDebounceMs = 30;
constexpr uint32_t kBootLongPressMs = 1000;
constexpr uint16_t kMaxTouchSamplesPerUiTick = 64;

struct QueuedTouchSample {
    uint32_t generation = 0;
    uint32_t captured_ms = 0;
    GoodixGT6972P::ReadResult result =
        GoodixGT6972P::ReadResult::kNoData;
    bool has_touch = false;
    GoodixGT6972P::PenPoint pen = {};
};

GoodixGT6972P touch;
riddle::DiaryController diary;
QueueHandle_t touch_queue = nullptr;
SemaphoreHandle_t touch_mutex = nullptr;
TaskHandle_t touch_task = nullptr;
TaskHandle_t ui_task = nullptr;
std::atomic<bool> touch_ready{false};
std::atomic<uint32_t> dropped_samples{0};
std::atomic<uint32_t> touch_generation{1};
uint32_t last_touch_poll_ms = 0;
uint32_t last_touch_retry_ms = 0;
uint32_t consecutive_errors = 0;
bool boot_raw_pressed = false;
bool boot_stable_pressed = false;
bool boot_long_handled = false;
uint32_t boot_raw_change_ms = 0;
uint32_t boot_press_ms = 0;

const char *resultName(GoodixGT6972P::ReadResult result)
{
    switch (result) {
    case GoodixGT6972P::ReadResult::kBusError:
        return "bus error";
    case GoodixGT6972P::ReadResult::kChecksumError:
        return "checksum error";
    case GoodixGT6972P::ReadResult::kFormatError:
        return "format error";
    case GoodixGT6972P::ReadResult::kNoData:
        return "no data";
    case GoodixGT6972P::ReadResult::kEvent:
        return "event";
    }
    return "unknown";
}

void advanceTouchGenerationAndResetQueue()
{
    if (touch_mutex != nullptr) {
        xSemaphoreTake(touch_mutex, portMAX_DELAY);
    }
    uint32_t next_generation =
        touch_generation.load(std::memory_order_relaxed) + 1U;
    if (next_generation == 0) {
        next_generation = 1;
    }
    touch_generation.store(next_generation, std::memory_order_release);
    if (touch_queue != nullptr) {
        xQueueReset(touch_queue);
    }
    if (touch_mutex != nullptr) {
        xSemaphoreGive(touch_mutex);
    }
}

bool qualityRefreshWithTouchBoundary()
{
    advanceTouchGenerationAndResetQueue();
    const bool refreshed = t5epd::clean();
    advanceTouchGenerationAndResetQueue();
    return refreshed;
}

void clearDiaryPage(bool quality_refresh)
{
    if (quality_refresh) {
        diary.clearPage(true);
        return;
    }
    advanceTouchGenerationAndResetQueue();
    diary.clearPage(false);
    advanceTouchGenerationAndResetQueue();
}

void enqueueTouchSampleLocked(const QueuedTouchSample &sample)
{
    if (xQueueSend(touch_queue, &sample, 0) == pdTRUE) {
        return;
    }

    ++dropped_samples;
    xQueueReset(touch_queue);

    QueuedTouchSample release;
    release.generation = sample.generation;
    release.captured_ms = sample.captured_ms;
    release.result = GoodixGT6972P::ReadResult::kEvent;
    release.has_touch = true;
    xQueueSend(touch_queue, &release, 0);
    xQueueSend(touch_queue, &sample, 0);
}

void mapPen(uint16_t raw_x, uint16_t raw_y, uint16_t &x, uint16_t &y)
{
    const uint32_t clamped_x = std::min<uint32_t>(raw_x, kTouchWidth - 1);
    const uint32_t clamped_y = std::min<uint32_t>(raw_y, kTouchHeight - 1);
    x = static_cast<uint16_t>(
        clamped_x * (t5epd::display().width() - 1) / (kTouchWidth - 1));
    y = static_cast<uint16_t>(
        clamped_y * (t5epd::display().height() - 1) / (kTouchHeight - 1));
}

bool initializeTouch()
{
    touch_ready = false;
    if (touch_mutex != nullptr) {
        xSemaphoreTake(touch_mutex, portMAX_DELAY);
    }
    const bool ready = touch.begin(Wire, kTouchSda, kTouchScl,
                                   kTouchReset, kTouchInterrupt);
    if (touch_queue != nullptr) {
        xQueueReset(touch_queue);
    }
    touch_ready = ready;
    if (touch_mutex != nullptr) {
        xSemaphoreGive(touch_mutex);
    }

    consecutive_errors = 0;
    if (!ready) {
        Serial.printf("[RIDDLE] touch initialization failed: %s\n",
                      touch.lastError());
        return false;
    }
    const GoodixGT6972P::DeviceInfo &info = touch.deviceInfo();
    Serial.printf("[RIDDLE] touch ready PID=%s address=0x%02X "
                  "range=%ux%u display=%dx%d\n",
                  info.patch_pid, info.address, kTouchWidth, kTouchHeight,
                  t5epd::display().width(), t5epd::display().height());
    return true;
}

void touchSamplerTask(void *unused)
{
    (void)unused;
    for (;;) {
        if (!touch_ready || touch_queue == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        const uint32_t now = millis();
        if (!touch.interruptAsserted() &&
            now - last_touch_poll_ms < kFallbackPollMs) {
            vTaskDelay(1);
            continue;
        }
        last_touch_poll_ms = now;

        xSemaphoreTake(touch_mutex, portMAX_DELAY);
        if (!touch_ready) {
            xSemaphoreGive(touch_mutex);
            continue;
        }
        GoodixGT6972P::Event event;
        const GoodixGT6972P::ReadResult result = touch.readEvent(event);
        if (result == GoodixGT6972P::ReadResult::kNoData) {
            xSemaphoreGive(touch_mutex);
            vTaskDelay(1);
            continue;
        }

        QueuedTouchSample sample;
        sample.generation =
            touch_generation.load(std::memory_order_acquire);
        sample.captured_ms = now;
        sample.result = result;
        if (result == GoodixGT6972P::ReadResult::kEvent) {
            sample.has_touch = event.has_touch;
            sample.pen = event.pen;
        }
        enqueueTouchSampleLocked(sample);
        xSemaphoreGive(touch_mutex);
    }
}

bool startTouchSampler()
{
    touch_queue = xQueueCreate(kTouchQueueLength, sizeof(QueuedTouchSample));
    touch_mutex = xSemaphoreCreateMutex();
    if (touch_queue == nullptr || touch_mutex == nullptr) {
        Serial.println("[RIDDLE] touch queue allocation failed");
        return false;
    }

    initializeTouch();
    const BaseType_t created = xTaskCreatePinnedToCore(
        touchSamplerTask, "riddle_pen", kTouchTaskStackSize, nullptr,
        kTouchTaskPriority, &touch_task, kTouchTaskCore);
    if (created != pdPASS) {
        touch_task = nullptr;
        Serial.println("[RIDDLE] touch sampler task creation failed");
        return false;
    }
    return true;
}

void deliverTouchSample(const QueuedTouchSample &sample)
{
    if (sample.generation !=
        touch_generation.load(std::memory_order_acquire)) {
        return;
    }
    if (sample.result != GoodixGT6972P::ReadResult::kEvent) {
        ++consecutive_errors;
        if (consecutive_errors == 1 || consecutive_errors % 20 == 0) {
            Serial.printf("[RIDDLE] touch %s (%lu consecutive)\n",
                          resultName(sample.result),
                          static_cast<unsigned long>(consecutive_errors));
        }
        if (consecutive_errors >= 20) {
            touch_ready = false;
            riddle::PenEvent release;
            release.captured_ms = sample.captured_ms;
            diary.handlePen(release);
            last_touch_retry_ms = millis();
        }
        return;
    }
    consecutive_errors = 0;
    if (!sample.has_touch) {
        return;
    }

    riddle::PenEvent event;
    event.present = sample.pen.present;
    event.hover = sample.pen.hover;
    event.eraser = sample.pen.eraser;
    event.pressure = sample.pen.pressure;
    event.captured_ms = sample.captured_ms;
    mapPen(sample.pen.x, sample.pen.y, event.x, event.y);
    diary.handlePen(event);
}

void pollTouchSynchronously()
{
    if (!touch_ready) {
        return;
    }
    const uint32_t now = millis();
    if (!touch.interruptAsserted() &&
        now - last_touch_poll_ms < kFallbackPollMs) {
        return;
    }
    last_touch_poll_ms = now;
    GoodixGT6972P::Event event;
    QueuedTouchSample sample;
    sample.generation = touch_generation.load(std::memory_order_acquire);
    sample.captured_ms = now;
    sample.result = touch.readEvent(event);
    if (sample.result == GoodixGT6972P::ReadResult::kNoData) {
        return;
    }
    if (sample.result == GoodixGT6972P::ReadResult::kEvent) {
        sample.has_touch = event.has_touch;
        sample.pen = event.pen;
    }
    deliverTouchSample(sample);
}

void pollTouch()
{
    if (touch_task == nullptr) {
        pollTouchSynchronously();
        return;
    }
    QueuedTouchSample sample;
    uint16_t delivered = 0;
    while (delivered < kMaxTouchSamplesPerUiTick &&
           xQueueReceive(touch_queue, &sample, 0) == pdTRUE) {
        deliverTouchSample(sample);
        ++delivered;
    }
}

void retryTouchIfNeeded()
{
    if (touch_ready || millis() - last_touch_retry_ms < kTouchRetryMs) {
        return;
    }
    last_touch_retry_ms = millis();
    initializeTouch();
}

void pollBootButton()
{
    const uint32_t now = millis();
    const bool pressed = digitalRead(kBootPin) == LOW;
    if (pressed != boot_raw_pressed) {
        boot_raw_pressed = pressed;
        boot_raw_change_ms = now;
    }
    if (now - boot_raw_change_ms >= kBootDebounceMs &&
        boot_stable_pressed != boot_raw_pressed) {
        boot_stable_pressed = boot_raw_pressed;
        if (boot_stable_pressed) {
            boot_press_ms = now;
            boot_long_handled = false;
        } else if (!boot_long_handled) {
            clearDiaryPage(false);
        }
    }
    if (boot_stable_pressed && !boot_long_handled &&
        now - boot_press_ms >= kBootLongPressMs) {
        boot_long_handled = true;
        clearDiaryPage(true);
    }
}

void serviceUiOnce()
{
    pollBootButton();
    if (touch_ready) {
        pollTouch();
    } else {
        retryTouchIfNeeded();
    }
    diary.tick();
}

void diaryUiTask(void *unused)
{
    (void)unused;
    Serial.printf("[RIDDLE] UI task core=%d priority=%u\n",
                  xPortGetCoreID(), static_cast<unsigned>(kUiTaskPriority));
    for (;;) {
        serviceUiOnce();
        vTaskDelay(1);
    }
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("[RIDDLE] ESP32 diary start");
    pinMode(kBootPin, INPUT_PULLUP);

    if (!t5epd::begin()) {
        Serial.println("[RIDDLE] display initialization failed");
        while (true) {
            delay(1000);
        }
    }
    if (!diary.begin(t5epd::display())) {
        Serial.println("[RIDDLE] application initialization failed");
        while (true) {
            delay(1000);
        }
    }
    if (!t5epd::clean()) {
        Serial.println("[RIDDLE] startup quality refresh failed");
    }
    startTouchSampler();
    diary.setQualityRefreshHook(qualityRefreshWithTouchBoundary);

    const BaseType_t created = xTaskCreatePinnedToCore(
        diaryUiTask, "riddle_ui", kUiTaskStackSize, nullptr,
        kUiTaskPriority, &ui_task, kUiTaskCore);
    if (created != pdPASS) {
        ui_task = nullptr;
        Serial.println("[RIDDLE] using Arduino loop for UI");
    }
}

void loop()
{
    if (ui_task == nullptr) {
        serviceUiOnce();
        delay(1);
        return;
    }
    delay(1000);
}
