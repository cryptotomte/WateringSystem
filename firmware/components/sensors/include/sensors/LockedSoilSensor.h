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
 * SCOPE (per-call atomicity, cross-call gap): each interface call is
 * individually atomic, but a read-then-get sequence spanning two calls
 * (read() then getMoisture()) is NOT protected against an interleaving
 * read() from another task in between — another task may refresh (or
 * invalidate) the values first. snapshot() (PR-11) CLOSES that gap for the
 * common consumer case: it copies the last-good values + validity + error
 * out under a SINGLE lock, so a reader (controller / API status / console)
 * never observes a torn read/getter tuple. Any other multi-call sequence
 * still needs higher-level coordination (a caller-held lock or single-owner
 * task).
 *
 * Pure C++ (<mutex> is available via pthread on ESP-IDF and on the linux
 * preview target), so the decorator is host-testable.
 */

#ifndef WATERINGSYSTEM_SENSORS_LOCKEDSOILSENSOR_H
#define WATERINGSYSTEM_SENSORS_LOCKEDSOILSENSOR_H

#include <mutex>

#include "interfaces/ISoilSensor.h"

/**
 * @brief Consistent, non-blocking soil-reading snapshot (PR-11).
 *
 * All eight quantity values plus the validity/error flags, copied out under
 * one lock so they are mutually consistent (no torn read()-then-getter
 * tuple). readOk reports whether the most recent read() through the wrapper
 * succeeded; available reports whether at least one successful read is on
 * record, so the last-good values are meaningful even when the latest read
 * failed (stale-but-usable). lastError is the wrapped sensor's most recent
 * error code.
 */
struct SoilSnapshot {
    bool readOk;
    bool available;
    int lastError;
    float moisture;
    float temperature;
    float humidity;
    float ph;
    float ec;
    float nitrogen;
    float phosphorus;
    float potassium;
};

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
        const bool ok = sensor_.read();
        // Cache the outcome so snapshot() can report validity without a
        // fresh (blocking) bus transaction.
        lastReadOk_ = ok;
        hasEverReadOk_ = hasEverReadOk_ || ok;
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

    /**
     * @brief Copy the last-good reading + validity + error out under one
     * lock (PR-11), closing the read-then-getter cross-call gap.
     *
     * NON-BLOCKING by contract: it performs NO fresh read() and NO
     * isAvailable() probe — both are RS485/Modbus bus I/O and must never run
     * in a status/API/controller path (QUIRK 5). The periodic soil reader
     * owns the read() cadence; this returns the current cached values.
     * readOk = the most recent read() through this wrapper succeeded;
     * available = at least one successful read is on record (last-good values
     * are meaningful); lastError = the wrapped sensor's current error code.
     */
    SoilSnapshot snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SoilSnapshot s;
        s.readOk = lastReadOk_;
        s.available = hasEverReadOk_;
        s.lastError = sensor_.getLastError();
        s.moisture = sensor_.getMoisture();
        s.temperature = sensor_.getTemperature();
        s.humidity = sensor_.getHumidity();
        s.ph = sensor_.getPH();
        s.ec = sensor_.getEC();
        s.nitrogen = sensor_.getNitrogen();
        s.phosphorus = sensor_.getPhosphorus();
        s.potassium = sensor_.getPotassium();
        return s;
    }

private:
    ISoilSensor& sensor_;
    mutable std::mutex mutex_;
    bool lastReadOk_ = false;    ///< result of the most recent read()
    bool hasEverReadOk_ = false; ///< any successful read() on record
};

#endif /* WATERINGSYSTEM_SENSORS_LOCKEDSOILSENSOR_H */
