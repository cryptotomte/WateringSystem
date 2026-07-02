// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file sensor_task.cpp
 * @brief 5 s environmental sensor poll task (feature 005, research.md R7).
 *
 * Parity parameters from the legacy controller task: 4096 B stack,
 * priority 1, fixed 5000 ms cadence via vTaskDelayUntil (drift-free).
 * Logging discipline (contracts/interfaces.md): INFO with the three values
 * on success, WARN once on the valid→invalid transition and once on
 * recovery, repeated failures at a bounded cadence — every
 * kFailureLogInterval-th consecutive failure (12 × 5 s ≈ once a minute) to
 * avoid log flood. The task never exits and never reboots on failures;
 * recovery is the sensor driver's lazy re-init, driven by this cadence.
 */

#include "sensor_task.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensor_task";

namespace {

constexpr uint32_t kPeriodMs = 5000;    ///< parity poll cadence (R7)
constexpr uint32_t kStackBytes = 4096;  ///< parity stack size (R7)
constexpr UBaseType_t kPriority = 1;    ///< parity priority (R7)

/// Repeated-failure log cadence: every Nth consecutive failure (~1/min).
constexpr uint32_t kFailureLogInterval = 12;

[[noreturn]] void sensor_task(void *arg)
{
    IEnvironmentalSensor &sensor =
        *static_cast<IEnvironmentalSensor *>(arg);

    // Start in the "valid" state so the FIRST failure (e.g. booting with
    // no sensor attached) logs the transition warning exactly once.
    bool wasValid = true;
    uint32_t consecutiveFailures = 0;

    TickType_t lastWake = xTaskGetTickCount();
    while (true) {
        // First poll one period after start — the NORMAL-mode first
        // conversion completes well within 5 s (research.md R9).
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(kPeriodMs));

        if (sensor.read()) {
            consecutiveFailures = 0;
            if (!wasValid) {
                ESP_LOGW(TAG, "environmental sensor recovered");
                wasValid = true;
            }
            // Periodic reading, consistent with the legacy 5 s status print.
            ESP_LOGI(TAG,
                     "temperature=%.1f C humidity=%.1f %%RH pressure=%.1f hPa",
                     static_cast<double>(sensor.getTemperature()),
                     static_cast<double>(sensor.getHumidity()),
                     static_cast<double>(sensor.getPressure()));
        } else {
            ++consecutiveFailures;
            const int error = sensor.getLastError();
            if (wasValid) {
                // Valid → invalid transition: WARN exactly once.
                ESP_LOGW(TAG,
                         "environmental reading invalid (error %d) — "
                         "keeping last-good values, retrying every %lu ms",
                         error, static_cast<unsigned long>(kPeriodMs));
                wasValid = false;
            } else if (consecutiveFailures % kFailureLogInterval == 0) {
                // Bounded repeat cadence while the failure persists.
                ESP_LOGW(TAG,
                         "environmental sensor still failing (error %d, "
                         "%lu consecutive failures)",
                         error,
                         static_cast<unsigned long>(consecutiveFailures));
            }
        }
    }
}

}  // namespace

void sensor_task_start(IEnvironmentalSensor& sensor)
{
    // Task starts even when the sensor failed init: the driver's lazy
    // re-initialization turns later polls into the recovery path (US2).
    const BaseType_t created =
        xTaskCreate(sensor_task, "sensor_task", kStackBytes, &sensor,
                    kPriority, nullptr);
    if (created != pdPASS) {
        // Not a safety function: log and continue without periodic
        // readings (the console `env` command still works).
        ESP_LOGE(TAG, "failed to create sensor task");
        return;
    }
    ESP_LOGI(TAG, "sensor task started (%lu ms cadence)",
             static_cast<unsigned long>(kPeriodMs));
}
