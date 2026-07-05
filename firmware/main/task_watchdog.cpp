// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file task_watchdog.cpp
 * @brief Task watchdog wrappers (feature 008 US3). See task_watchdog.h for the
 *        subscription policy; the timeout comes from CONFIG_WS_TASK_WDT_TIMEOUT_S.
 */

#include "task_watchdog.h"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"  // portNUM_PROCESSORS
#include "sdkconfig.h"

static const char *TAG = "task_wdt";

esp_err_t watchdog_init()
{
    // The watchdog is ALREADY initialised at boot (CONFIG_ESP_TASK_WDT_INIT=y,
    // watching the idle tasks), so the normal path is a reconfigure of the
    // runtime timeout — NOT a fresh init. Panic (reboot) on timeout is enabled
    // (CONFIG_ESP_TASK_WDT_PANIC=y) and re-asserted here.
    esp_task_wdt_config_t cfg = {
        .timeout_ms = (uint32_t)CONFIG_WS_TASK_WDT_TIMEOUT_S * 1000u,
        // Watch the idle task on every core, preserving the CONFIG_ESP_TASK_WDT_INIT
        // default (esp_task_wdt_reconfigure applies the FULL config, so a 0 mask
        // would UNsubscribe the idle tasks). This keeps the per-core hang detector
        // in addition to the explicitly-subscribed watering-critical tasks.
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true,
    };
    esp_err_t err = esp_task_wdt_reconfigure(&cfg);
    if (err == ESP_ERR_INVALID_STATE) {
        // The watchdog was not inited (CONFIG_ESP_TASK_WDT_INIT disabled) —
        // init it from scratch instead.
        err = esp_task_wdt_init(&cfg);
    }
    if (err != ESP_OK) {
        // Non-fatal: without the WDT a hung task is not caught, but watering
        // still runs. Log loudly so the misconfiguration is visible.
        ESP_LOGE(TAG, "task WDT configure failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "task WDT active: timeout %d s, panic reboot on stall",
                 CONFIG_WS_TASK_WDT_TIMEOUT_S);
    }
    return err;
}

void watchdog_subscribe_current_task()
{
    const esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        // Non-fatal: an unsubscribed task is simply not watched by the WDT.
        ESP_LOGW(TAG, "task WDT subscribe failed: %s", esp_err_to_name(err));
    }
}

void watchdog_feed()
{
    esp_task_wdt_reset();
}
