// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file IDigitalInput.h
 * @brief Injected raw digital-input source (single-pin read seam).
 *
 * The host-test seam for GPIO-input drivers (feature 006, research.md R1):
 * DebouncedLevelSensor holds all settle/debounce/polarity policy above this
 * interface and is host-tested against a scripted implementation;
 * GpioLevelSensor is the only hardware-touching implementation (raw
 * gpio_get_level, no logic). A tiny interface was chosen over std::function
 * to match the codebase's interface-injection style (ITimeProvider,
 * IModbusClient, II2cBus) and to avoid std::function's potential allocation.
 *
 * Part of the header-only `interfaces` component: no IDF includes allowed.
 */

#ifndef WATERINGSYSTEM_INTERFACES_IDIGITALINPUT_H
#define WATERINGSYSTEM_INTERFACES_IDIGITALINPUT_H

/**
 * @brief One digital input, read on demand.
 */
class IDigitalInput {
public:
    virtual ~IDigitalInput() = default;

    /**
     * @brief Current raw electrical level: true = HIGH, false = LOW.
     *
     * No debounce, no polarity mapping — policy belongs to the consumer.
     */
    virtual bool read() = 0;
};

#endif /* WATERINGSYSTEM_INTERFACES_IDIGITALINPUT_H */
