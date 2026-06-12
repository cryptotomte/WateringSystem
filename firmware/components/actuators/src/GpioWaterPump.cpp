// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file GpioWaterPump.cpp
 * @brief GPIO pump driver implementation (esp32 targets only).
 *
 * Error handling is explicit (not ESP_ERROR_CHECK): pump output control is
 * safety-relevant and must be checked regardless of the configured
 * assertion level (ESP_ERROR_CHECK degrades to a no-op under NDEBUG).
 */

#include "actuators/GpioWaterPump.h"

#include <utility>

#include "esp_log.h"

static const char *TAG = "gpiowaterpump";

GpioWaterPump::GpioWaterPump(gpio_num_t pin, std::string name,
                             ITimeProvider& timeProvider,
                             int64_t maxRunTimeMs)
    : WaterPump(std::move(name), timeProvider, maxRunTimeMs),
      pin_(pin)
{
}

bool GpioWaterPump::initialize()
{
    // Set the output level to 0 BEFORE switching the pin to output mode, so
    // the pin never glitches high when the output driver is enabled (same
    // order as the app_main boot fail-safe).
    esp_err_t err = gpio_set_level(pin_, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: gpio_set_level(%d, 0) failed: %s",
                 getName().c_str(), static_cast<int>(pin_),
                 esp_err_to_name(err));
        setLastError(err);
        return false;
    }

    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << static_cast<unsigned>(pin_)),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: gpio_config(%d) failed: %s",
                 getName().c_str(), static_cast<int>(pin_),
                 esp_err_to_name(err));
        setLastError(err);
        return false;
    }

    // Base class re-asserts OFF via applyOutput(false) and arms the state
    // machine.
    return WaterPump::initialize();
}

bool GpioWaterPump::applyOutput(bool on)
{
    // Active HIGH: MOSFET gate, level 1 = pump on.
    const esp_err_t err = gpio_set_level(pin_, on ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: gpio_set_level(%d, %d) failed: %s",
                 getName().c_str(), static_cast<int>(pin_), on ? 1 : 0,
                 esp_err_to_name(err));
        setLastError(err);
        return false;
    }
    return true;
}
