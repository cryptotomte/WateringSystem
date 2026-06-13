// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LockedConfigStore.h
 * @brief Mutex-serializing IConfigStore decorator (header-only).
 *
 * WHY THIS EXISTS: the config store is reached from more than one FreeRTOS
 * task — the diag console REPL task issues get/set/factory-reset commands
 * while application tasks (watering controller, web server in later PRs)
 * read configuration. NvsConfigStore itself is deliberately unsynchronized
 * (host-testable pure logic over per-operation NVS handles), so concurrent
 * setters could interleave — e.g. setWifiCredentials() writes two NVS
 * entries and a concurrent factoryReset() erasing the partition in between
 * would leave a half-written credential pair. This decorator wraps an
 * IConfigStore and takes a mutex around every interface call, serializing
 * all access (FR-013; research.md D9, PR-02 CP3 precedent —
 * actuators/LockedWaterPump.h).
 *
 * USAGE RULE: once a store is wrapped, the underlying store must ONLY be
 * accessed through the wrapper — every call site (boot wiring, console
 * registration, controllers, ...) goes through the LockedConfigStore,
 * never through the wrapped object directly.
 *
 * SCOPE: this decorator provides PER-CALL atomicity only, not cross-call.
 * A read-modify-write sequence spanning two calls (e.g. get* then set*) is
 * NOT protected against an interleaving writer between the two — another
 * task may change the value in between. Such sequences need higher-level
 * coordination (a caller-held lock or single-owner task).
 *
 * Pure C++ (<mutex> is available via pthread on ESP-IDF and on the linux
 * preview target), so the decorator is host-testable.
 */

#ifndef WATERINGSYSTEM_STORAGE_LOCKEDCONFIGSTORE_H
#define WATERINGSYSTEM_STORAGE_LOCKEDCONFIGSTORE_H

#include <cstdint>
#include <mutex>
#include <string>

#include "interfaces/IConfigStore.h"

/**
 * @brief IConfigStore decorator that serializes every call with a mutex.
 *
 * Composition, not inheritance from a concrete store: the base class stays
 * pure (no locking) and the existing host tests are unchanged. The wrapped
 * store must outlive this object.
 */
class LockedConfigStore : public IConfigStore {
public:
    /// Wrap @p store; the wrapped store must outlive this object.
    explicit LockedConfigStore(IConfigStore& store) : store_(store) {}

    LockedConfigStore(const LockedConfigStore&) = delete;
    LockedConfigStore& operator=(const LockedConfigStore&) = delete;

    float getMoistureThresholdLow() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.getMoistureThresholdLow();
    }

    bool setMoistureThresholdLow(float percent) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.setMoistureThresholdLow(percent);
    }

    float getMoistureThresholdHigh() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.getMoistureThresholdHigh();
    }

    bool setMoistureThresholdHigh(float percent) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.setMoistureThresholdHigh(percent);
    }

    uint32_t getWateringDurationS() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.getWateringDurationS();
    }

    bool setWateringDurationS(uint32_t seconds) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.setWateringDurationS(seconds);
    }

    uint32_t getMinWateringIntervalS() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.getMinWateringIntervalS();
    }

    bool setMinWateringIntervalS(uint32_t seconds) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.setMinWateringIntervalS(seconds);
    }

    bool getWateringEnabled() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.getWateringEnabled();
    }

    bool setWateringEnabled(bool enabled) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.setWateringEnabled(enabled);
    }

    uint32_t getSensorReadIntervalMs() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.getSensorReadIntervalMs();
    }

    bool setSensorReadIntervalMs(uint32_t ms) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.setSensorReadIntervalMs(ms);
    }

    uint32_t getDataLogIntervalMs() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.getDataLogIntervalMs();
    }

    bool setDataLogIntervalMs(uint32_t ms) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.setDataLogIntervalMs(ms);
    }

    std::string getWifiSsid() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.getWifiSsid();
    }

    std::string getWifiPassword() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.getWifiPassword();
    }

    bool setWifiCredentials(const std::string& ssid,
                            const std::string& password) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.setWifiCredentials(ssid, password);
    }

    bool clearWifiCredentials() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.clearWifiCredentials();
    }

    bool factoryReset() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.factoryReset();
    }

private:
    IConfigStore& store_;
    mutable std::mutex mutex_;
};

#endif /* WATERINGSYSTEM_STORAGE_LOCKEDCONFIGSTORE_H */
