// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file IDataStorage.h
 * @brief Bounded sensor history, rotating event log, storage statistics.
 *
 * Replaces the data half of the legacy IDataStorage — a deliberate
 * contract redesign, same approach as IConfigStore. The two legacy
 * methods with no callers (getLastSensorReading, pruneOldReadings) are
 * dropped; retention is an internal bounded-storage guarantee instead of
 * a caller obligation. Normative contract:
 * specs/003-nvs-littlefs-storage/contracts/IDataStorage.md; on-disk
 * formats and budgets: specs/003-nvs-littlefs-storage/data-model.md.
 *
 * Invariants:
 *  1. History and event storage never exceed their documented budgets;
 *     writes at the bound evict oldest data, never fail the append.
 *  2. A power loss during append loses at most the in-flight record;
 *     previously stored records remain readable (torn tails detected
 *     and skipped on read).
 *  3. Reads are side-effect free except torn-tail truncation/skip.
 *  4. Timestamps are caller-supplied epoch seconds, stored verbatim
 *     (time correctness is the caller's concern, parity checklist 184).
 *
 * Part of the header-only `interfaces` component: no IDF includes allowed
 * (compiled on the host in the linux-target test suite).
 */

#ifndef WATERINGSYSTEM_INTERFACES_IDATASTORAGE_H
#define WATERINGSYSTEM_INTERFACES_IDATASTORAGE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// One sensor reading; `metric` follows the legacy naming
/// (env_temperature, soil_moisture, ...) but the set is open.
struct SensorReading {
    std::string metric;
    uint32_t epoch = 0;  ///< epoch seconds, caller-supplied
    float value = 0.0f;
};

/// One persisted safety-relevant event.
struct EventRecord {
    uint32_t epoch = 0;    ///< epoch seconds, caller-supplied
    uint8_t category = 0;  ///< IDataStorage::kCategory* (open set)
    std::string detail;    ///< <= kEventDetailMaxLen bytes
};

/// Total/used bytes of the data filesystem (FR-008).
struct StorageStats {
    uint32_t totalBytes = 0;
    uint32_t usedBytes = 0;
};

/**
 * @brief Bounded data storage with internal retention guarantees.
 *
 * Implementations are unsynchronized by design; cross-task consumers wrap
 * them in the Locked* decorator (research.md D9, PR-02 CP3 precedent).
 */
class IDataStorage {
public:
    // Contract-level bounds (data-model.md) — the single source of truth
    // shared by implementations, mocks and contract tests.

    /// Budget guard: at most this many distinct metrics; storing an
    /// extra distinct metric is rejected (prevents a buggy caller from
    /// silently destroying history or blowing the budget).
    static constexpr std::size_t kMaxMetrics = 10;

    /// Longer event details are silently truncated to this many bytes on
    /// store — the event itself is always recorded, never rejected.
    static constexpr std::size_t kEventDetailMaxLen = 120;

    // Event categories (FR-011). PR-08 may extend the set — unknown
    // values are stored and returned verbatim.
    static constexpr uint8_t kCategoryPump = 1;
    static constexpr uint8_t kCategoryFailsafe = 2;
    static constexpr uint8_t kCategoryConnectivity = 3;
    static constexpr uint8_t kCategoryOta = 4;
    static constexpr uint8_t kCategoryReset = 5;

    virtual ~IDataStorage() = default;

    /**
     * @brief Append one sensor reading.
     *
     * Durable once true is returned (survives power loss). An unknown
     * metric is accepted up to kMaxMetrics distinct metrics; one more
     * distinct metric is rejected with false. Bounding/eviction is
     * internal: >= 30-day retention at the default log interval,
     * oldest-first eviction, never failing the append at the bound.
     */
    virtual bool storeSensorReading(const std::string& metric,
                                    uint32_t epoch, float value) = 0;

    /**
     * @brief Readings for `metric` with epoch in [t0, t1] (inclusive).
     *
     * Chronological order. Empty vector on no data, unknown metric,
     * t0 > t1, or read error — never throws/fails (legacy parity).
     */
    virtual std::vector<SensorReading> getSensorReadings(
        const std::string& metric, uint32_t t0, uint32_t t1) const = 0;

    /**
     * @brief Append one event record.
     *
     * `detail` longer than kEventDetailMaxLen bytes is silently
     * truncated (the event is always recorded, never rejected for
     * length). Rotation keeps total event storage within its budget and
     * always retains the newest records.
     */
    virtual bool storeEvent(uint32_t epoch, uint8_t category,
                            const std::string& detail) = 0;

    /**
     * @brief Newest-first events, at most maxCount.
     *
     * Empty vector on no data or read error. "Newest-first" assumes
     * monotonic caller-supplied epochs; under a non-monotonic clock the
     * ordering degrades to active-file-first (time correctness is the
     * caller's concern, parity checklist 184).
     */
    virtual std::vector<EventRecord> getEvents(std::size_t maxCount) const = 0;

    /// Total/used bytes of the data filesystem.
    virtual StorageStats getStorageStats() const = 0;
};

#endif /* WATERINGSYSTEM_INTERFACES_IDATASTORAGE_H */
