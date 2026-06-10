// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file GpioWaterPump.h
 * @brief GPIO-backed pump driver (N-channel MOSFET gate, active HIGH).
 *
 * ESP32-ONLY: excluded from the linux-target build together with the
 * esp_driver_gpio dependency (see this component's CMakeLists.txt).
 * All timing/safety logic lives in the WaterPump base class; this class
 * only maps applyOutput() onto gpio_set_level().
 */

#ifndef WATERINGSYSTEM_ACTUATORS_GPIOWATERPUMP_H
#define WATERINGSYSTEM_ACTUATORS_GPIOWATERPUMP_H

#include <string>

#include "driver/gpio.h"

#include "actuators/WaterPump.h"
#include "interfaces/ITimeProvider.h"

/**
 * @brief WaterPump whose output drives a GPIO pin (active HIGH).
 */
class GpioWaterPump : public WaterPump {
public:
    /**
     * @brief Construct a GPIO pump driver.
     *
     * @param pin          MOSFET gate GPIO (from board.h, never hard-coded).
     * @param name         Identity for logs/diagnostics.
     * @param timeProvider Injected monotonic clock.
     * @param maxRunTimeMs Hard cap on a single run, in milliseconds.
     */
    GpioWaterPump(gpio_num_t pin, std::string name,
                  ITimeProvider& timeProvider,
                  int64_t maxRunTimeMs = kDefaultMaxRunTimeMs);

    /**
     * @brief Configure the GPIO and force the pump OFF.
     *
     * Drives the output level to 0 BEFORE enabling the output driver so the
     * pin never glitches high (same glitch-free order as the app_main boot
     * fail-safe), then re-asserts OFF through the base class.
     */
    bool initialize() override;

protected:
    bool applyOutput(bool on) override;

private:
    gpio_num_t pin_;
};

#endif /* WATERINGSYSTEM_ACTUATORS_GPIOWATERPUMP_H */
