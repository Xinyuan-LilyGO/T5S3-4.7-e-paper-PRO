// SPDX-License-Identifier: GPL-2.0-only

#include "NvsRecovery.h"

#include <nvs.h>
#include <nvs_flash.h>

namespace {

bool canRecoverByErasing(esp_err_t error)
{
    return error == ESP_ERR_NVS_NOT_ENOUGH_SPACE ||
           error == ESP_ERR_NVS_NO_FREE_PAGES ||
           error == ESP_ERR_NVS_NEW_VERSION_FOUND;
}

} // namespace

NvsRecoveryResult ensureDefaultNvsReady()
{
    NvsRecoveryResult result;
    result.initial_error = nvs_flash_init();
    result.final_error = result.initial_error;
    if (result.initial_error == ESP_OK) {
        result.ready = true;
        return result;
    }
    if (!canRecoverByErasing(result.initial_error)) {
        return result;
    }

    result.erase_error = nvs_flash_erase();
    if (result.erase_error != ESP_OK) {
        result.final_error = result.erase_error;
        return result;
    }
    result.erased = true;
    result.final_error = nvs_flash_init();
    result.ready = result.final_error == ESP_OK;
    return result;
}
