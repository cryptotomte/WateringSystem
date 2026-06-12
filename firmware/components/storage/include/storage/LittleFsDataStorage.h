// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LittleFsDataStorage.h
 * @brief IDataStorage over POSIX file I/O against an injectable base path.
 *
 * Written against plain POSIX stdio only — NO esp_littlefs includes — so
 * the same code runs under the /storage littlefs VFS mount on target and
 * against a temp directory in the linux-target host tests (research.md
 * D4). Storage statistics come from an injected provider
 * (esp_littlefs_info on target — wired in user story 4; a fake on host).
 *
 * On-disk formats per specs/003-nvs-littlefs-storage/data-model.md:
 *  - History: /hist/<metric>/<first_epoch>.dat append-only chunks of
 *    8-byte little-endian records {uint32 epoch, float value}; chunks
 *    sealed at 8 KiB, at most 10 chunks per metric (ring eviction), at
 *    most 10 distinct metrics (11th rejected).
 *  - Events: /events/0.log + 1.log, 0xE7-framed records
 *    {marker, uint32 epoch, uint8 category, uint8 detail_len, detail};
 *    16 KiB per file, truncate-and-switch rotation (newest always kept).
 *
 * Durability (research.md D5): fflush+fsync per appended record; chunk
 * eviction via remove(); no in-place overwrites of committed data. Torn
 * tails: history = file size % 8 truncated logically on read; events =
 * marker/length framing, invalid tail skipped. The write path repairs a
 * torn tail (truncate to the valid prefix) before appending so committed
 * records always stay parseable.
 *
 * Stateless with respect to the filesystem: every operation derives its
 * state (active chunk, active event file) from the files themselves, so
 * a restart needs no recovery step. Unsynchronized by design —
 * cross-task consumers wrap the storage in the Locked* decorator
 * (research.md D9, PR-02 CP3 precedent).
 */

#ifndef WATERINGSYSTEM_STORAGE_LITTLEFSDATASTORAGE_H
#define WATERINGSYSTEM_STORAGE_LITTLEFSDATASTORAGE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "interfaces/IDataStorage.h"

/**
 * @brief File-backed data storage (target littlefs VFS + host POSIX).
 */
class LittleFsDataStorage : public IDataStorage {
public:
    /// Filesystem statistics callback; returns false when unavailable.
    /// Target: esp_littlefs_info (user story 4). Host tests: a fake.
    using StatsProvider =
        std::function<bool(uint32_t& totalBytes, uint32_t& usedBytes)>;

    // On-disk format constants (data-model.md) — exposed for the
    // contract tests; the contract-level bounds live in IDataStorage.
    static constexpr std::size_t kHistoryRecordBytes = 8;
    static constexpr std::size_t kHistoryChunkMaxBytes = 8192;
    static constexpr std::size_t kHistoryMaxChunksPerMetric = 10;
    static constexpr std::size_t kEventFileMaxBytes = 16384;
    static constexpr std::size_t kEventHeaderBytes = 7;  ///< marker..detail_len
    static constexpr uint8_t kEventMarker = 0xE7;

    /**
     * @param basePath storage root without trailing slash ("/storage" on
     *                 target, a temp directory in host tests)
     * @param statsProvider filesystem statistics source; getStorageStats()
     *                      reports zeros when absent or failing
     */
    explicit LittleFsDataStorage(std::string basePath,
                                 StatsProvider statsProvider = nullptr);

    // IDataStorage
    bool storeSensorReading(const std::string& metric, uint32_t epoch,
                            float value) override;
    std::vector<SensorReading> getSensorReadings(const std::string& metric,
                                                 uint32_t t0,
                                                 uint32_t t1) const override;
    bool storeEvent(uint32_t epoch, uint8_t category,
                    const std::string& detail) override;
    std::vector<EventRecord> getEvents(std::size_t maxCount) const override;
    StorageStats getStorageStats() const override;

private:
    std::string histDir() const;
    std::string metricDir(const std::string& metric) const;
    std::string eventsDir() const;
    std::string eventPath(int index) const;

    /// Which of the two event files appends are directed to, derived
    /// from the files (fullness, then newest-last-record fallback).
    int activeEventIndex() const;

    std::string basePath_;
    StatsProvider statsProvider_;
};

#endif /* WATERINGSYSTEM_STORAGE_LITTLEFSDATASTORAGE_H */
