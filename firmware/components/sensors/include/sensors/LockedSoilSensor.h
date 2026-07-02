// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LockedSoilSensor.h
 * @brief Mutex-serializing ISoilSensor decorator (header-only).
 *
 * WHY THIS EXISTS: the soil sensor will be reached from more than one
 * FreeRTOS task — the diag console REPL task issues soil/calibration
 * commands while application tasks (watering controller in PR-11) run
 * periodic reads. ModbusSoilSensor itself is deliberately unsynchronized
 * pure C++ (host-testable), so its plain float members would race: read()
 * publishes eight values member-by-member, and a concurrent getter could
 * observe a half-published reading (fresh moisture with stale pH). This
 * decorator wraps an ISoilSensor and takes a mutex around every interface
 * call, serializing all access (pattern of actuators/LockedWaterPump.h and
 * storage/LockedConfigStore.h).
 *
 * USAGE RULE: once a sensor is wrapped, the underlying sensor must ONLY be
 * accessed through the wrapper — every call site (boot wiring, console
 * registration, controllers, ...) goes through the LockedSoilSensor, never
 * through the wrapped object directly.
 *
 * SCOPE: this decorator provides PER-CALL atomicity only, not cross-call.
 * A read-then-get sequence spanning two calls (read() then getMoisture())
 * is NOT protected against an interleaving read() from another task in
 * between — another task may refresh (or invalidate) the values first.
 * Such sequences need higher-level coordination (a caller-held lock or
 * single-owner task).
 *
 * Pure C++ (<mutex> is available via pthread on ESP-IDF and on the linux
 * preview target), so the decorator is host-testable.
 */

#ifndef WATERINGSYSTEM_SENSORS_LOCKEDSOILSENSOR_H
#define WATERINGSYSTEM_SENSORS_LOCKEDSOILSENSOR_H

#include <mutex>

#include "interfaces/ISoilSensor.h"

/**
 * @brief ISoilSensor decorator that serializes every call with a mutex.
 *
 * Composition, not inheritance from a concrete sensor: the base class
 * stays pure (no locking) and the host tests are unchanged. The wrapped
 * sensor must outlive this object.
 */
class LockedSoilSensor : public ISoilSensor {
public:
    /// Wrap @p sensor; the wrapped sensor must outlive this object.
    explicit LockedSoilSensor(ISoilSensor& sensor) : sensor_(sensor) {}

    LockedSoilSensor(const LockedSoilSensor&) = delete;
    LockedSoilSensor& operator=(const LockedSoilSensor&) = delete;

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

    float getMoisture() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.getMoisture();
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

    float getPH() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.getPH();
    }

    float getEC() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.getEC();
    }

    float getNitrogen() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.getNitrogen();
    }

    float getPhosphorus() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.getPhosphorus();
    }

    float getPotassium() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.getPotassium();
    }

    bool calibrateMoisture(float referenceValue) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.calibrateMoisture(referenceValue);
    }

    bool calibratePH(float referenceValue) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.calibratePH(referenceValue);
    }

    bool calibrateEC(float referenceValue) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.calibrateEC(referenceValue);
    }

private:
    ISoilSensor& sensor_;
    mutable std::mutex mutex_;
};

#endif /* WATERINGSYSTEM_SENSORS_LOCKEDSOILSENSOR_H */
