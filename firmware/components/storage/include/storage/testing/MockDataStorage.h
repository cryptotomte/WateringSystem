// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file MockDataStorage.h
 * @brief In-memory IDataStorage test double (header-only).
 *
 * Holds the same contract invariants as the real storage (FR-012):
 * inclusive chronological range queries that never fail, the
 * kMaxMetrics distinct-metric cap, event detail truncation at
 * kEventDetailMaxLen, newest-first event retrieval, and internal
 * bounding that keeps the newest data. `history`, `events` and `stats`
 * are public so tests can inject state directly; `failWrites` simulates
 * a persistence failure. For consumer tests in later PRs (PR-08, PR-09).
 * Never compiled into target builds (only included from test code).
 * No IDF includes.
 */

#ifndef WATERINGSYSTEM_STORAGE_TESTING_MOCKDATASTORAGE_H
#define WATERINGSYSTEM_STORAGE_TESTING_MOCKDATASTORAGE_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "interfaces/IDataStorage.h"

/**
 * @brief IDataStorage over plain containers, instrumented for tests.
 */
class MockDataStorage : public IDataStorage {
public:
    // In-memory bounds mirroring the real store's budget granularity
    // (data-model.md: 10 chunks x 1024 records per metric; events keep
    // the newest, oldest evicted at the bound).
    static constexpr std::size_t kMaxRecordsPerMetric = 10240;
    static constexpr std::size_t kMaxEvents = 512;

    /// Per-metric history in append order (the real store's
    /// "chronological by construction").
    std::map<std::string, std::vector<SensorReading>> history;

    /// Events in append order, oldest first (retrieval reverses).
    std::vector<EventRecord> events;

    /// Returned verbatim by getStorageStats().
    StorageStats stats{};

    // Instrumentation.
    int acceptedWrites = 0;   ///< appends that were stored
    int rejectedWrites = 0;   ///< appends rejected (cap or failWrites)
    bool failWrites = false;  ///< true: every write fails, state untouched

    /// Defensive metric-name rule shared with the real store: names become
    /// directory names there, so empty, '/' or ".." are rejected.
    static bool isValidMetricName(const std::string& metric)
    {
        return !metric.empty() && metric.find('/') == std::string::npos &&
               metric.find("..") == std::string::npos;
    }

    // IDataStorage
    bool storeSensorReading(const std::string& metric, uint32_t epoch,
                            float value) override
    {
        if (failWrites || !isValidMetricName(metric)) {
            ++rejectedWrites;
            return false;
        }
        auto it = history.find(metric);
        if (it == history.end()) {
            if (history.size() >= kMaxMetrics) {
                // Contract: one more distinct metric is rejected.
                ++rejectedWrites;
                return false;
            }
            it = history.emplace(metric, std::vector<SensorReading>{}).first;
        }
        it->second.push_back(SensorReading{metric, epoch, value});
        if (it->second.size() > kMaxRecordsPerMetric) {
            // Invariant 1: oldest-first eviction, never failing the append.
            it->second.erase(it->second.begin());
        }
        ++acceptedWrites;
        return true;
    }

    std::vector<SensorReading> getSensorReadings(const std::string& metric,
                                                 uint32_t t0,
                                                 uint32_t t1) const override
    {
        std::vector<SensorReading> result;
        if (t0 > t1) {
            return result;  // contract: empty, not an error
        }
        const auto it = history.find(metric);
        if (it == history.end()) {
            return result;
        }
        for (const auto& reading : it->second) {
            if (reading.epoch >= t0 && reading.epoch <= t1) {
                result.push_back(reading);
            }
        }
        return result;
    }

    bool storeEvent(uint32_t epoch, uint8_t category,
                    const std::string& detail) override
    {
        if (failWrites) {
            ++rejectedWrites;
            return false;
        }
        // Over-long detail is truncated, never rejected (contract).
        events.push_back(
            EventRecord{epoch, category, detail.substr(0, kEventDetailMaxLen)});
        if (events.size() > kMaxEvents) {
            events.erase(events.begin());  // newest always retained
        }
        ++acceptedWrites;
        return true;
    }

    std::vector<EventRecord> getEvents(std::size_t maxCount) const override
    {
        std::vector<EventRecord> result;
        for (auto it = events.rbegin();
             it != events.rend() && result.size() < maxCount; ++it) {
            result.push_back(*it);
        }
        return result;
    }

    StorageStats getStorageStats() const override { return stats; }
};

#endif /* WATERINGSYSTEM_STORAGE_TESTING_MOCKDATASTORAGE_H */
