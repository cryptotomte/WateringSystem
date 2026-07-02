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
 * SensorTaskLogPolicy::kFailureLogInterval-th consecutive failure
 * (12 × 5 s ≈ once a minute) to avoid log flood. The WHAT-to-log decision
 * lives in the pure SensorTaskLogPolicy (sensors component) so it is
 * host-tested, not review-verified; this task owns only the ESP_LOG calls
 * and the cadence. The task never exits and never reboots on failures;
 * recovery is the sensor driver's lazy re-init, driven by this cadence.
 */

#include "sensor_task.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sensors/SensorTaskLogPolicy.h"

static const char *TAG = "sensor_task";

namespace {

constexpr uint32_t kPeriodMs = 5000;    ///< parity poll cadence (R7)
constexpr uint32_t kStackBytes = 4096;  ///< parity stack size (R7)
constexpr UBaseType_t kPriority = 1;    ///< parity priority (R7)

[[noreturn]] void sensor_task(void *arg)
{
    IEnvironmentalSensor &sensor =
        *static_cast<IEnvironmentalSensor *>(arg);

    // Host-tested log-decision policy: starts "valid" so the FIRST failure
    // (e.g. booting with no sensor attached) WARNs exactly once.
    SensorTaskLogPolicy logPolicy;

    TickType_t lastWake = xTaskGetTickCount();
    while (true) {
        // First poll one period after start — the NORMAL-mode first
        // conversion completes well within 5 s (research.md R9).
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(kPeriodMs));

        const bool ok = sensor.read();
        switch (logPolicy.onReadResult(ok)) {
        case SensorTaskLogPolicy::Event::Recovery:
            ESP_LOGW(TAG, "environmental sensor recovered");
            [[fallthrough]];  // a recovered sensor also logs its reading
        case SensorTaskLogPolicy::Event::Reading:
            // Periodic reading, consistent with the legacy 5 s status print.
            ESP_LOGI(TAG,
                     "temperature=%.1f C humidity=%.1f %%RH pressure=%.1f hPa",
                     static_cast<double>(sensor.getTemperature()),
                     static_cast<double>(sensor.getHumidity()),
                     static_cast<double>(sensor.getPressure()));
            break;
        case SensorTaskLogPolicy::Event::FailureTransition:
            // Valid → invalid transition: WARN exactly once.
            ESP_LOGW(TAG,
                     "environmental reading invalid (error %d) — "
                     "keeping last-good values, retrying every %lu ms",
                     sensor.getLastError(),
                     static_cast<unsigned long>(kPeriodMs));
            break;
        case SensorTaskLogPolicy::Event::RepeatedFailure:
            // Bounded repeat cadence while the failure persists.
            ESP_LOGW(TAG,
                     "environmental sensor still failing (error %d, "
                     "%lu consecutive failures)",
                     sensor.getLastError(),
                     static_cast<unsigned long>(
                         logPolicy.consecutiveFailures()));
            break;
        case SensorTaskLogPolicy::Event::Silent:
            break;
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
