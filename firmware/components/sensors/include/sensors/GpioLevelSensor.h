// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file GpioLevelSensor.h
 * @brief GPIO-backed raw input for one level-sensor pin (no logic).
 *
 * ESP32-ONLY: excluded from the linux-target build (see this component's
 * CMakeLists.txt). This is the level-sensor hardware touchpoint — all
 * settle/debounce/polarity policy lives above IDigitalInput, in
 * DebouncedLevelSensor (research.md R1, same split as EspI2cBus /
 * EspModbusClient).
 *
 * PRIV rule (this component's convention): driver/gpio.h appears only in
 * the .cpp, never here — the pin is held as a plain int.
 */

#ifndef WATERINGSYSTEM_SENSORS_GPIOLEVELSENSOR_H
#define WATERINGSYSTEM_SENSORS_GPIOLEVELSENSOR_H

#include "interfaces/IDigitalInput.h"

/**
 * @brief IDigitalInput over one GPIO pin, input with internal pull-up.
 *
 * The internal pull-up is enabled on BOTH boards (research.md R4): parity
 * on rev1 (legacy src/main.cpp:231-233 uses INPUT_PULLUP,
 * docs/parity-checklist.md line 95), redundant-but-harmless on rev2 on top
 * of the external 10 kΩ. The pull-up also pins the documented fail
 * direction: a disconnected input reads HIGH (docs/parity-checklist.md
 * line 97).
 */
class GpioLevelSensor : public IDigitalInput {
public:
    /**
     * @brief Construct the raw input on @p pin.
     *
     * @param pin Level-sensor GPIO (from board.h, never hard-coded). No
     *            hardware access here — call initialize() first.
     */
    explicit GpioLevelSensor(int pin);

    ~GpioLevelSensor() override = default;

    GpioLevelSensor(const GpioLevelSensor&) = delete;
    GpioLevelSensor& operator=(const GpioLevelSensor&) = delete;

    /**
     * @brief Configure the pin as input with the internal pull-up enabled.
     *
     * @return true on success; false is logged by the implementation —
     *         read() then still returns the (unconfigured) pin level and
     *         the caller decides how loudly to complain.
     */
    bool initialize();

    // IDigitalInput
    /// Raw pin level (true = HIGH). One gpio_get_level(), no logic.
    bool read() override;

private:
    int pin_;
};

#endif /* WATERINGSYSTEM_SENSORS_GPIOLEVELSENSOR_H */
