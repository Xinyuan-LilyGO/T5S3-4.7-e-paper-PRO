// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <esp_err.h>

struct NvsRecoveryResult {
    bool ready = false;
    bool erased = false;
    esp_err_t initial_error = ESP_OK;
    esp_err_t erase_error = ESP_OK;
    esp_err_t final_error = ESP_OK;
};

// Arduino initializes NVS before setup(), but Core 2.0.14 does not recover
// ESP_ERR_NVS_NOT_ENOUGH_SPACE. Retry initialization and erase only for the
// small whitelist of errors that identify an unusable NVS data set.
NvsRecoveryResult ensureDefaultNvsReady();
