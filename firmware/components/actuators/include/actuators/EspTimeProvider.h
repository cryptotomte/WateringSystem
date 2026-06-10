// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file EspTimeProvider.h
 * @brief Target (esp32) monotonic clock backed by esp_timer.
 *
 * ESP32-ONLY: esp_timer is not simulated on the IDF linux preview target,
 * so this header must never be included from host-tested code. Host tests
 * use actuators/testing/FakeTimeProvider.h instead (research.md D1/D3).
 */

#ifndef WATERINGSYSTEM_ACTUATORS_ESPTIMEPROVIDER_H
#define WATERINGSYSTEM_ACTUATORS_ESPTIMEPROVIDER_H

#include <cstdint>

#include "esp_timer.h"
#include "interfaces/ITimeProvider.h"

/**
 * @brief Monotonic millisecond clock from esp_timer (microsecond source).
 */
class EspTimeProvider : public ITimeProvider {
public:
    int64_t nowMs() override
    {
        return esp_timer_get_time() / 1000;
    }
};

#endif /* WATERINGSYSTEM_ACTUATORS_ESPTIMEPROVIDER_H */
