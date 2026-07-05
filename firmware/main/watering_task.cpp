// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file watering_task.cpp
 * @brief Decision-layer watering task at the sensor-read cadence (feature 011).
 *
 * This task runs the DECISION layer, separate from the 10 Hz main loop that
 * still owns precise pump-timing enforcement (pump update() self-stop + 300 s
 * cap, level update(), observer.poll()). Each cycle it calls
 * WateringController::tick() and, on boards with a reservoir pump,
 * ReservoirController::tick(...).
 *
 * Controller-as-reader: WateringController::tick() performs the periodic soil
 * read() (the blocking Modbus round-trip) which refreshes the LockedSoilSensor
 * cache that the PR-09 /sensors endpoint serves — so NO separate soil-reader
 * task is needed. That blocking read (plus the periodic littlefs data-log) is
 * isolated on THIS task and never runs on the 10 Hz safety loop.
 *
 * Watchdog: this is a watering-critical task, so it subscribes to the task WDT
 * and feeds once per cycle (mirrors sensor_task). The blocking Modbus read is
 * well under the WDT timeout.
 *
 * Isolation: the task shares nothing with the network/HTTP path beyond the same
 * Locked* wrappers every other task uses (FR-017).
 */

#include "watering_task.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "task_watchdog.h"

static const char *TAG = "watering_task";

namespace {

constexpr uint32_t kStackBytes = 8192;  ///< blocking Modbus read + littlefs log
constexpr UBaseType_t kPriority = 1;    ///< same class as sensor_task
constexpr uint32_t kFloorMs = 1000;     ///< IConfigStore sensor-interval floor

/// Long-lived task context (the task never exits). A single static instance
/// holds borrowed pointers to the app_main collaborators.
struct WateringTaskCtx {
    WateringController* controller;
    IConfigStore* config;
#if BOARD_HAS_RESERVOIR_PUMP
    ReservoirController* reservoir;
#endif
};

WateringTaskCtx ctx;

[[noreturn]] void watering_task(void* arg)
{
    WateringTaskCtx* c = static_cast<WateringTaskCtx*>(arg);

    // Watering-critical task: subscribe to the task WDT (feature 008 US3). The
    // period is re-read each loop and floored at 1000 ms, well under the 20 s
    // default timeout; the feed each cycle is the liveness proof the WDT needs.
    watchdog_subscribe_current_task();

    while (true) {
        // Re-read the cadence each loop so a config change takes effect without
        // a restart. vTaskDelay (not vTaskDelayUntil) because the period is
        // dynamic.
        uint32_t periodMs = c->config->getSensorReadIntervalMs();
        if (periodMs < kFloorMs) {
            periodMs = kFloorMs;
        }
        vTaskDelay(pdMS_TO_TICKS(periodMs));

        // Feed once per cycle: this task is alive and servicing the WDT.
        watchdog_feed();

        // Decision layer. tick() does the periodic soil read (refreshing the
        // LockedSoilSensor cache) + the watering decision + the periodic
        // data-log.
        c->controller->tick();
#if BOARD_HAS_RESERVOIR_PUMP
        // Reservoir flag mapping: `enabled` is always true — on rev1 the
        // reservoir pump always exists and the feature is on, and we never
        // force-off a pump the operator might be manually filling. Automatic
        // level-based filling is gated by the SAME mode flag as plant watering
        // (getWateringEnabled), so "manual mode" suspends all automation while
        // manual API fills still work. There is deliberately NO dedicated
        // auto-level config flag.
        c->reservoir->tick(true, c->config->getWateringEnabled());
#endif
    }
}

}  // namespace

#if BOARD_HAS_RESERVOIR_PUMP
void watering_task_start(WateringController& controller, ReservoirController& reservoir,
                         IConfigStore& config)
{
    ctx.controller = &controller;
    ctx.config = &config;
    ctx.reservoir = &reservoir;

    const BaseType_t created =
        xTaskCreate(watering_task, "watering_task", kStackBytes, &ctx,
                    kPriority, nullptr);
    if (created != pdPASS) {
        // Not a safety function: log and continue. The 10 Hz loop still
        // enforces pump timing; only the decision layer is absent.
        ESP_LOGE(TAG, "failed to create watering task");
        return;
    }
    ESP_LOGI(TAG, "watering task started (%lu ms cadence floor)",
             static_cast<unsigned long>(kFloorMs));
}
#else
void watering_task_start(WateringController& controller, IConfigStore& config)
{
    ctx.controller = &controller;
    ctx.config = &config;

    const BaseType_t created =
        xTaskCreate(watering_task, "watering_task", kStackBytes, &ctx,
                    kPriority, nullptr);
    if (created != pdPASS) {
        // Not a safety function: log and continue. The 10 Hz loop still
        // enforces pump timing; only the decision layer is absent.
        ESP_LOGE(TAG, "failed to create watering task");
        return;
    }
    ESP_LOGI(TAG, "watering task started (%lu ms cadence floor)",
             static_cast<unsigned long>(kFloorMs));
}
#endif
