// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LockedDataStorage.h
 * @brief Mutex-serializing IDataStorage decorator (header-only).
 *
 * WHY THIS EXISTS: the data storage is reached from more than one FreeRTOS
 * task — the diag console REPL task issues store/query/stats commands
 * while application tasks (data logger, event sources, web server in later
 * PRs) append and read. LittleFsDataStorage itself is deliberately
 * unsynchronized (host-testable POSIX file logic) and derives its state
 * from the files on every call, so concurrent appends could interleave —
 * e.g. two storeEvent() calls deciding on the same active file offset, or
 * a rotation truncating a file mid-append from another task. This
 * decorator wraps an IDataStorage and takes a mutex around every interface
 * call, serializing all access (FR-013; research.md D9, PR-02 CP3
 * precedent — actuators/LockedWaterPump.h).
 *
 * USAGE RULE: once a storage is wrapped, the underlying storage must ONLY
 * be accessed through the wrapper — every call site (boot wiring, console
 * registration, loggers, ...) goes through the LockedDataStorage, never
 * through the wrapped object directly.
 *
 * Pure C++ (<mutex> is available via pthread on ESP-IDF and on the linux
 * preview target), so the decorator is host-testable.
 */

#ifndef WATERINGSYSTEM_STORAGE_LOCKEDDATASTORAGE_H
#define WATERINGSYSTEM_STORAGE_LOCKEDDATASTORAGE_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "interfaces/IDataStorage.h"

/**
 * @brief IDataStorage decorator that serializes every call with a mutex.
 *
 * Composition, not inheritance from a concrete storage: the base class
 * stays pure (no locking) and the existing host tests are unchanged. The
 * wrapped storage must outlive this object.
 */
class LockedDataStorage : public IDataStorage {
public:
    /// Wrap @p storage; the wrapped storage must outlive this object.
    explicit LockedDataStorage(IDataStorage& storage) : storage_(storage) {}

    LockedDataStorage(const LockedDataStorage&) = delete;
    LockedDataStorage& operator=(const LockedDataStorage&) = delete;

    bool storeSensorReading(const std::string& metric, uint32_t epoch,
                            float value) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return storage_.storeSensorReading(metric, epoch, value);
    }

    std::vector<SensorReading> getSensorReadings(const std::string& metric,
                                                 uint32_t t0,
                                                 uint32_t t1) const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return storage_.getSensorReadings(metric, t0, t1);
    }

    bool storeEvent(uint32_t epoch, uint8_t category,
                    const std::string& detail) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return storage_.storeEvent(epoch, category, detail);
    }

    std::vector<EventRecord> getEvents(std::size_t maxCount) const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return storage_.getEvents(maxCount);
    }

    StorageStats getStorageStats() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return storage_.getStorageStats();
    }

private:
    IDataStorage& storage_;
    mutable std::mutex mutex_;
};

#endif /* WATERINGSYSTEM_STORAGE_LOCKEDDATASTORAGE_H */
