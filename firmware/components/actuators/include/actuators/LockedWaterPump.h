// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LockedWaterPump.h
 * @brief Mutex-serializing IWaterPump decorator (header-only).
 *
 * WHY THIS EXISTS: the pump objects are accessed from two FreeRTOS tasks —
 * the main task polls update() at 10 Hz while the esp_console REPL task
 * (diag_console) calls runFor()/stop()/isRunning()/getCurrentRunTimeMs().
 * WaterPump itself is deliberately unsynchronized pure C++ (host-testable),
 * so its plain bool/int64_t members would race: e.g. runFor() sets
 * running_ = true before runStartedAtMs_, and a concurrently running
 * update() could observe a stale start time and force a MaxRuntimeForced
 * stop on a fresh start; torn int64 reads are also possible on 32-bit
 * Xtensa. This decorator wraps an IWaterPump and takes a mutex around
 * every interface call, serializing all access.
 *
 * USAGE RULE: once a pump is wrapped, the underlying pump must ONLY be
 * accessed through the wrapper — every call site (initialize, update,
 * console registration, ...) goes through the LockedWaterPump, never
 * through the wrapped object directly.
 *
 * Pure C++ (<mutex> is available via pthread on ESP-IDF and on the linux
 * preview target), so the decorator is host-testable.
 */

#ifndef WATERINGSYSTEM_ACTUATORS_LOCKEDWATERPUMP_H
#define WATERINGSYSTEM_ACTUATORS_LOCKEDWATERPUMP_H

#include <cstdint>
#include <mutex>
#include <string>

#include "interfaces/IWaterPump.h"

/**
 * @brief IWaterPump decorator that serializes every call with a mutex.
 *
 * Composition, not inheritance from WaterPump: the base class stays pure
 * (no locking) and the existing host tests are unchanged. The wrapped pump
 * must outlive this object.
 */
class LockedWaterPump : public IWaterPump {
public:
    /// Wrap @p pump; the wrapped pump must outlive this object.
    explicit LockedWaterPump(IWaterPump& pump) : pump_(pump) {}

    LockedWaterPump(const LockedWaterPump&) = delete;
    LockedWaterPump& operator=(const LockedWaterPump&) = delete;

    // IActuator
    bool initialize() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pump_.initialize();
    }

    bool isAvailable() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pump_.isAvailable();
    }

    const std::string& getName() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pump_.getName();
    }

    int getLastError() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pump_.getLastError();
    }

    // IWaterPump
    bool runFor(int durationS) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pump_.runFor(durationS);
    }

    bool stop() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pump_.stop();
    }

    bool isRunning() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pump_.isRunning();
    }

    void update() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pump_.update();
    }

    int64_t getCurrentRunTimeMs() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pump_.getCurrentRunTimeMs();
    }

    int64_t getAccumulatedRunTimeMs() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pump_.getAccumulatedRunTimeMs();
    }

    StopReason getLastStopReason() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pump_.getLastStopReason();
    }

private:
    IWaterPump& pump_;
    mutable std::mutex mutex_;
};

#endif /* WATERINGSYSTEM_ACTUATORS_LOCKEDWATERPUMP_H */
