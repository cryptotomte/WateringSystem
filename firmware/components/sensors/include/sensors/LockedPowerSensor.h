// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LockedPowerSensor.h
 * @brief Mutex-serializing IPowerSensor decorator (header-only).
 *
 * WHY THIS EXISTS: in this PR the power sensor is only reached from the
 * diag console REPL task (`power` command), but PR-09's web server and
 * PR-11's controller add readers — the wrapper exists from day one per the
 * established rule (every cross-task-reachable sensor is wrapped).
 * Ina226Sensor itself is deliberately unsynchronized pure C++
 * (host-testable), so its plain float members would race: read() publishes
 * three values member-by-member, and a concurrent getter could observe a
 * half-published reading (fresh voltage with stale power). This decorator
 * wraps an IPowerSensor and takes a mutex around every interface call,
 * serializing all access (pattern of LockedEnvironmentalSensor/
 * LockedWaterPump).
 *
 * Bus-level safety across I2C devices (BME280 + INA226 on the shared bus)
 * is provided by the i2c_master driver's per-transaction bus lock — this
 * decorator exists for reading-snapshot consistency, not bus safety.
 *
 * USAGE RULE: once a sensor is wrapped, the underlying sensor must ONLY be
 * accessed through the wrapper — every call site (boot wiring, console
 * registration, controllers, ...) goes through the LockedPowerSensor,
 * never through the wrapped object directly.
 *
 * SCOPE: this decorator provides PER-CALL atomicity only, not cross-call.
 * A read-then-getters sequence spanning multiple calls (read() then
 * getBusVoltage()/getCurrent()/getPower()) is NOT protected against an
 * interleaving read() from another task in between — another task may
 * refresh (or invalidate) the values first. Such sequences need
 * higher-level coordination.
 * TODO(PR-11): add a consistent-snapshot helper (one locked call returning
 * all three values + validity) when a second periodic reader appears —
 * same bookkeeping as the other Locked* wrappers' PR-11 notes.
 *
 * Pure C++ (<mutex> is available via pthread on ESP-IDF and on the linux
 * preview target), so the decorator is host-testable.
 */

#ifndef WATERINGSYSTEM_SENSORS_LOCKEDPOWERSENSOR_H
#define WATERINGSYSTEM_SENSORS_LOCKEDPOWERSENSOR_H

#include <mutex>

#include "interfaces/IPowerSensor.h"

/**
 * @brief IPowerSensor decorator that serializes every call with a mutex.
 *
 * Composition, not inheritance from a concrete sensor: the base class
 * stays pure (no locking) and the host tests are unchanged. The wrapped
 * sensor must outlive this object.
 */
class LockedPowerSensor : public IPowerSensor {
public:
    /// Wrap @p sensor; the wrapped sensor must outlive this object.
    explicit LockedPowerSensor(IPowerSensor& sensor) : sensor_(sensor) {}

    LockedPowerSensor(const LockedPowerSensor&) = delete;
    LockedPowerSensor& operator=(const LockedPowerSensor&) = delete;

    bool initialize() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.initialize();
    }

    bool read() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.read();
    }

    bool isAvailable() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.isAvailable();
    }

    int getLastError() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.getLastError();
    }

    float getBusVoltage() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.getBusVoltage();
    }

    float getCurrent() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.getCurrent();
    }

    float getPower() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.getPower();
    }

private:
    IPowerSensor& sensor_;
    mutable std::mutex mutex_;
};

#endif /* WATERINGSYSTEM_SENSORS_LOCKEDPOWERSENSOR_H */
