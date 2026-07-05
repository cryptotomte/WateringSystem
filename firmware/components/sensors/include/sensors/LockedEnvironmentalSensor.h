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
 * SCOPE (per-call atomicity, cross-call gap): each interface call is
 * individually atomic, but a read-then-getters sequence spanning multiple
 * calls (read() then getTemperature()/getHumidity()/getPressure()) is NOT
 * protected against an interleaving read() from another task in between.
 * snapshot() (PR-11) CLOSES that gap: it copies all three values + validity
 * out under a SINGLE lock, so a reader (controller / API status / console)
 * never observes a torn read/getter tuple. Any other multi-call sequence
 * still needs higher-level coordination.
 *
 * Pure C++ (<mutex> is available via pthread on ESP-IDF and on the linux
 * preview target), so the decorator is host-testable.
 */

#ifndef WATERINGSYSTEM_SENSORS_LOCKEDENVIRONMENTALSENSOR_H
#define WATERINGSYSTEM_SENSORS_LOCKEDENVIRONMENTALSENSOR_H

#include <mutex>

#include "interfaces/IEnvironmentalSensor.h"

/**
 * @brief Consistent, non-blocking environmental snapshot (PR-11).
 *
 * Temperature/humidity/pressure plus validity, copied out under one lock so
 * they are mutually consistent. valid reports whether the most recent read()
 * through the wrapper succeeded (fresh data); when false the values are the
 * last-good reading (or the NaN placeholders before the first success).
 */
struct EnvSnapshot {
    bool valid;
    float temperature;
    float humidity;
    float pressure;
};

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
        const bool ok = sensor_.read();
        // Cache the outcome so snapshot() can report validity without a
        // fresh (blocking) bus transaction.
        lastReadOk_ = ok;
        return ok;
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

    /**
     * @brief Copy the last-good T/RH/P + validity out under one lock (PR-11),
     * closing the read-then-getter cross-call gap.
     *
     * NON-BLOCKING by contract: it performs NO fresh read() and NO
     * isAvailable() probe — both are I2C bus I/O and must never run in a
     * status/API/controller path (QUIRK 5). The sensor task owns the read()
     * cadence; this returns the current cached values. valid = the most
     * recent read() through this wrapper succeeded.
     */
    EnvSnapshot snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        EnvSnapshot s;
        s.valid = lastReadOk_;
        s.temperature = sensor_.getTemperature();
        s.humidity = sensor_.getHumidity();
        s.pressure = sensor_.getPressure();
        return s;
    }

private:
    IEnvironmentalSensor& sensor_;
    mutable std::mutex mutex_;
    bool lastReadOk_ = false; ///< result of the most recent read()
};

#endif /* WATERINGSYSTEM_SENSORS_LOCKEDENVIRONMENTALSENSOR_H */
