// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LockedLevelSensor.h
 * @brief Mutex-serializing ILevelSensor decorator (header-only).
 *
 * WHY THIS EXISTS: each level sensor is reached from more than one FreeRTOS
 * task — the main loop polls update() at 10 Hz while the diag console REPL
 * task issues `level` commands (the watering controller in PR-11 adds
 * another reader). DebouncedLevelSensor itself is deliberately
 * unsynchronized pure C++ (host-testable), so its plain members would race:
 * update() advances the state machine member-by-member, and a concurrent
 * getter could observe a half-advanced state. This decorator wraps an
 * ILevelSensor and takes a mutex around every interface call, serializing
 * all access (pattern of LockedEnvironmentalSensor/LockedWaterPump).
 *
 * USAGE RULE: once a sensor is wrapped, the underlying sensor must ONLY be
 * accessed through the wrapper — every call site (boot wiring, main-loop
 * update, console registration, controllers, ...) goes through the
 * LockedLevelSensor, never through the wrapped object directly.
 *
 * SCOPE (per-call atomicity, cross-call gap): each interface call is
 * individually atomic, but an isValid()-then-isWaterPresent() sequence is
 * NOT protected against an interleaving update() from another task in
 * between — the state may change (or invalidate) between the two calls.
 * snapshot() (PR-11) CLOSES that gap: it copies validity + logical state out
 * under a SINGLE lock, so a reader (controller / API status / console) never
 * observes a torn validity/state tuple. Any other multi-call sequence still
 * needs higher-level coordination.
 *
 * Pure C++ (<mutex> is available via pthread on ESP-IDF and on the linux
 * preview target), so the decorator is host-testable.
 */

#ifndef WATERINGSYSTEM_SENSORS_LOCKEDLEVELSENSOR_H
#define WATERINGSYSTEM_SENSORS_LOCKEDLEVELSENSOR_H

#include <mutex>

#include "interfaces/ILevelSensor.h"

/**
 * @brief Consistent level snapshot (PR-11): validity + logical state copied
 * out under one lock so they are mutually consistent.
 *
 * waterPresent is meaningful ONLY while valid is true; when invalid it is
 * false (never a stale or phantom value — the interface contract). The
 * reservoir truth table treats an invalid sensor as "do not act", never as
 * "water absent".
 */
struct LevelSnapshot {
    bool valid;
    bool waterPresent;
};

/**
 * @brief ILevelSensor decorator that serializes every call with a mutex.
 *
 * Composition, not inheritance from a concrete sensor: the base class
 * stays pure (no locking) and the host tests are unchanged. The wrapped
 * sensor must outlive this object.
 */
class LockedLevelSensor : public ILevelSensor {
public:
    /// Wrap @p sensor; the wrapped sensor must outlive this object.
    explicit LockedLevelSensor(ILevelSensor& sensor) : sensor_(sensor) {}

    LockedLevelSensor(const LockedLevelSensor&) = delete;
    LockedLevelSensor& operator=(const LockedLevelSensor&) = delete;

    void update() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sensor_.update();
    }

    bool isValid() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.isValid();
    }

    bool isWaterPresent() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.isWaterPresent();
    }

    bool rawState() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return sensor_.rawState();
    }

    void notifyPowerOn() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sensor_.notifyPowerOn();
    }

    /**
     * @brief Copy validity + logical water state out under one lock (PR-11),
     * closing the isValid()-then-isWaterPresent() cross-call gap.
     *
     * update() is NOT part of this call — the main loop owns the update()
     * cadence; snapshot() only reads the current debounced state, which is
     * pure in-memory state (no I/O). waterPresent is false whenever invalid.
     */
    LevelSnapshot snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        LevelSnapshot s;
        s.valid = sensor_.isValid();
        s.waterPresent = sensor_.isWaterPresent();
        return s;
    }

private:
    ILevelSensor& sensor_;
    mutable std::mutex mutex_;
};

#endif /* WATERINGSYSTEM_SENSORS_LOCKEDLEVELSENSOR_H */
