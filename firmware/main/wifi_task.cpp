// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file wifi_task.cpp
 * @brief WiFi station tick task + status LED (feature 007 US2, T019/T021).
 *
 * Mirrors sensor_task.cpp: its own 4096 B stack at priority 1, a [[noreturn]]
 * fixed-cadence loop, the manager injected via the void* task arg, and a
 * non-fatal creation failure. This is a SEPARATE task from the 10 Hz
 * pump/level loop and holds no watering mutex (FR-014); all it does is advance
 * the pure WifiManager (non-blocking tick()) and mirror the snapshot on the
 * status LED.
 *
 * LED policy (parity §7/§9): ~500 ms toggle while Connecting/Reconnecting,
 * steady ON when Connected, OFF while ReconnectPaused (or any non-station
 * state). The 100 ms config-button-hold blink is US3 (T026), not here.
 */

#include "wifi_task.h"

#include "board/board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "network/WifiState.h"

static const char *TAG = "wifi_task";

namespace {

constexpr uint32_t kTickPeriodMs = 250;       ///< manager tick cadence
constexpr uint32_t kBlinkHalfPeriodMs = 500;  ///< LED toggle half-period
constexpr uint32_t kStackBytes = 4096;        ///< match sensor_task (R7)
constexpr UBaseType_t kPriority = 1;          ///< match sensor_task (R7)

/// Configure the status LED as a driven, initially-off output. Non-fatal: a
/// failure only costs the visual indicator, never the tick loop.
void status_led_init()
{
    const gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << BOARD_PIN_STATUS_LED,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t err = gpio_config(&led_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "status LED gpio_config failed: %s (LED disabled)",
                 esp_err_to_name(err));
        return;
    }
    gpio_set_level(static_cast<gpio_num_t>(BOARD_PIN_STATUS_LED), 0);
}

[[noreturn]] void wifi_task(void *arg)
{
    WifiManager &manager = *static_cast<WifiManager *>(arg);

    status_led_init();
    bool led_on = false;
    uint32_t blink_accum_ms = 0;

    const auto set_led = [&led_on](bool on) {
        if (led_on != on) {
            led_on = on;
            gpio_set_level(static_cast<gpio_num_t>(BOARD_PIN_STATUS_LED),
                           on ? 1 : 0);
        }
    };

    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(kTickPeriodMs));

        // Non-blocking: advances from time deltas + drained driver events;
        // never sleeps, waits or reboots (FR-013/FR-014).
        manager.tick();

        const WifiConnectionSnapshot snap = manager.snapshot();
        switch (snap.state) {
        case WifiState::Connecting:
        case WifiState::Reconnecting:
            // ~500 ms toggle: attempting to (re)join the network.
            blink_accum_ms += kTickPeriodMs;
            if (blink_accum_ms >= kBlinkHalfPeriodMs) {
                blink_accum_ms = 0;
                set_led(!led_on);
            }
            break;
        case WifiState::Connected:
            // Steady on: usable link.
            blink_accum_ms = 0;
            set_led(true);
            break;
        case WifiState::ReconnectPaused:
        case WifiState::Provisioning:
            // Off: paused between rounds (or not station-managed at all).
            blink_accum_ms = 0;
            set_led(false);
            break;
        }
    }
}

}  // namespace

void wifi_task_start(WifiManager &manager)
{
    const BaseType_t created =
        xTaskCreate(wifi_task, "wifi_task", kStackBytes, &manager, kPriority,
                    nullptr);
    if (created != pdPASS) {
        // Not a safety function: log and continue without the WiFi tick loop
        // (the watering path is unaffected — FR-014).
        ESP_LOGE(TAG, "failed to create wifi task");
        return;
    }
    ESP_LOGI(TAG, "wifi task started (%lu ms tick cadence)",
             static_cast<unsigned long>(kTickPeriodMs));
}
