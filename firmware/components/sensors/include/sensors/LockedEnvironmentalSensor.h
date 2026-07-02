// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LockedEnvironmentalSensor.h
 * @brief Mutex-serializing IEnvironmentalSensor decorator (header-only).
 *
 * WHY THIS EXISTS: the environmental sensor is reached from more than one
 * FreeRTOS task — the 5 s sensor task polls read() while the diag console
 * REPL task issues `env` commands (web server in PR-09 and the watering
 * controller in PR-11 add more readers). Bme280Sensor itself is
 * deliberately unsynchronized pure C++ (host-testable), so its plain float
 * members would race: read() publishes three values member-by-member, and
 * a concurrent getter could observe a half-published reading (fresh
 * temperature with stale pressure). This decorator wraps an
 * IEnvironmentalSensor and takes a mutex around every interface call,
 * serializing all access (pattern of LockedSoilSensor/LockedWaterPump).
 *
 * Bus-level safety across I2C devices (INA226, PR-05) is provided by the
 * i2c_master driver's per-transaction bus lock (research.md R3) — this
 * decorator exists for reading-snapshot consistency, not bus safety.
 *
 * USAGE RULE: once a sensor is wrapped, the underlying sensor must ONLY be
 * accessed through the wrapper — every call site (boot wiring, console
 * registration, sensor task, controllers, ...) goes through the
 * LockedEnvironmentalSensor, never through the wrapped object directly.
 *
 * SCOPE: this decorator provides PER-CALL atomicity only, not cross-call.
 * A read-then-getters sequence spanning multiple calls (read() then
 * getTemperature()/getHumidity()/getPressure()) is NOT protected against
 * an interleaving read() from another task in between — another task may
 * refresh (or invalidate) the values first. Such sequences need
 * higher-level coordination.
 * TODO(PR-11): add a consistent-snapshot helper (one locked call returning
 * all three values + validity) when the controller becomes a second
 * periodic reader — same bookkeeping as LockedSoilSensor's PR-11 notes.
 *
 * Pure C++ (<mutex> is available via pthread on ESP-IDF and on the linux
 * preview target), so the decorator is host-testable.
 */

#ifndef WATERINGSYSTEM_SENSORS_LOCKEDENVIRONMENTALSENSOR_H
#define WATERINGSYSTEM_SENSORS_LOCKEDENVIRONMENTALSENSOR_H

#include <mutex>

#include "interfaces/IEnvironmentalSensor.h"

/**
 * @brief IEnvironmentalSensor decorator that serializes every call with a
 * mutex.
 *
 * Composition, not inheritance from a concrete sensor: the base class
 * stays pure (no locking) and the host tests are unchanged. The wrapped
 * sensor must outlive this object.
 */
class LockedEnvironmentalSensor : public IEnvironmentalSensor {
public:
    /// Wrap @p sensor; the wrapped sensor must outlive this object.
    explicit LockedEnvironmentalSensor(IEnvironmentalSensor& sensor)
        : sensor_(sensor)
    {
    }

    LockedEnvironmentalSensor(const LockedEnvironmentalSensor&) = delete;
    LockedEnvironmentalSensor& operator=(const LockedEnvironmentalSensor&) =
        delete;

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

    float getTemperature() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.getTemperature();
    }

    float getHumidity() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.getHumidity();
    }

    float getPressure() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.getPressure();
    }

private:
    IEnvironmentalSensor& sensor_;
    mutable std::mutex mutex_;
};

#endif /* WATERINGSYSTEM_SENSORS_LOCKEDENVIRONMENTALSENSOR_H */
