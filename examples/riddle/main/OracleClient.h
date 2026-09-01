// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include "RiddleConfig.h"
#include "RiddlePng.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <stdint.h>

namespace riddle {

struct OracleResult {
    bool ok = false;
    bool offline = false;
    bool truncated = false;
    uint16_t http_status = 0;
    char text[RIDDLE_ORACLE_REPLY_BYTES] = {};
    char error[RIDDLE_ORACLE_ERROR_BYTES] = {};
};

class OracleClient {
public:
    OracleClient() = default;
    OracleClient(const OracleClient &) = delete;
    OracleClient &operator=(const OracleClient &) = delete;

    bool begin();

    // A successful submit transfers ownership of page to the worker. The
    // worker deletes it on every success/error path. On false, ownership stays
    // with the caller.
    bool submit(PageBitmap *page);
    bool poll(OracleResult &result);
    bool busy() const;

private:
    static void taskEntry(void *argument);
    void taskLoop();
    void process(PageBitmap *page, OracleResult &result);

    QueueHandle_t request_queue_ = nullptr;
    QueueHandle_t result_queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> busy_{false};
    bool clock_ready_ = false; // Accessed only by the oracle worker.
};

} // namespace riddle
