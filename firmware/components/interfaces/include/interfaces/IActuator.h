// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file IActuator.h
 * @brief Base interface for all actuators in the WateringSystem.
 *
 * Ported from the frozen Arduino firmware (include/actuators/IActuator.h)
 * with Arduino types removed. This header is part of the header-only
 * `interfaces` component and MUST NOT include any IDF or hardware headers
 * (it is compiled on the host in the linux-target test suite).
 */

#ifndef WATERINGSYSTEM_INTERFACES_IACTUATOR_H
#define WATERINGSYSTEM_INTERFACES_IACTUATOR_H

#include <string>

/**
 * @brief Base interface for all actuators.
 *
 * Defines the common functionality for all actuator types: initialization,
 * availability and error reporting.
 */
class IActuator {
public:
    virtual ~IActuator() = default;

    /**
     * @brief Initialize the actuator.
     *
     * Idempotent. Implementations MUST force the actuator into its safe
     * OFF state before any other action (boot fail-safe chain).
     *
     * @return true if initialization succeeded, false otherwise.
     */
    virtual bool initialize() = 0;

    /**
     * @brief Check if the actuator is available and working.
     * @return true if available, false otherwise.
     */
    virtual bool isAvailable() const = 0;

    /**
     * @brief Get the name of the actuator (identity for logs/diagnostics).
     * @return Actuator name.
     */
    virtual const std::string& getName() const = 0;

    /**
     * @brief Get the last error code.
     * @return Error code, 0 if no error.
     */
    virtual int getLastError() const = 0;
};

#endif /* WATERINGSYSTEM_INTERFACES_IACTUATOR_H */
