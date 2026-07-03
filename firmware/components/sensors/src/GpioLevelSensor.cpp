// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file GpioLevelSensor.cpp
 * @brief GPIO raw-input implementation (esp32 targets only).
 *
 * Error handling is explicit (not ESP_ERROR_CHECK): a misconfigured level
 * input must be visible in the logs regardless of the configured assertion
 * level, and must never abort — level sensing is not boot-critical. On an
 * initialize() failure the wiring site (app_main) latches the
 * DebouncedLevelSensor above into its Faulted state (markFaulted), so the
 * floating, unconfigured pin is never debounced into a valid reading and
 * PR-11's fail-safe treats invalid as "do not act".
 */

#include "sensors/GpioLevelSensor.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "gpiolevelsensor";

GpioLevelSensor::GpioLevelSensor(int pin) : pin_(pin)
{
}

bool GpioLevelSensor::initialize()
{
    // Input with internal pull-up on both boards (research.md R4; rev1
    // parity with legacy INPUT_PULLUP, rev2 belt-and-braces on top of the
    // external 10 kΩ). The pull-up pins the fail direction: disconnected
    // reads HIGH.
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << static_cast<unsigned>(pin_)),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config(%d) failed: %s", pin_,
                 esp_err_to_name(err));
        return false;
    }
    return true;
}

bool GpioLevelSensor::read()
{
    return gpio_get_level(static_cast<gpio_num_t>(pin_)) != 0;
}
