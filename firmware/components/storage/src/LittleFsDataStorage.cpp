// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LittleFsDataStorage.cpp
 * @brief IDataStorage over POSIX file I/O (skeleton — T021/T024 fill it in).
 *
 * POSIX stdio only, no esp_littlefs/IDF includes: the identical code runs
 * against the /storage littlefs VFS mount on target and a temp directory
 * in the linux-target host tests (research.md D4).
 */

#include "storage/LittleFsDataStorage.h"

LittleFsDataStorage::LittleFsDataStorage(std::string basePath,
                                         StatsProvider statsProvider)
    : basePath_(std::move(basePath)), statsProvider_(std::move(statsProvider))
{
}

bool LittleFsDataStorage::storeSensorReading(const std::string& /*metric*/,
                                             uint32_t /*epoch*/,
                                             float /*value*/)
{
    // T021 (user story 2): history append not implemented yet.
    return false;
}

std::vector<SensorReading> LittleFsDataStorage::getSensorReadings(
    const std::string& /*metric*/, uint32_t /*t0*/, uint32_t /*t1*/) const
{
    // T021 (user story 2): history query not implemented yet.
    return {};
}

bool LittleFsDataStorage::storeEvent(uint32_t /*epoch*/, uint8_t /*category*/,
                                     const std::string& /*detail*/)
{
    // T024 (user story 3): event log not implemented yet.
    return false;
}

std::vector<EventRecord> LittleFsDataStorage::getEvents(
    std::size_t /*maxCount*/) const
{
    // T024 (user story 3): event log not implemented yet.
    return {};
}

StorageStats LittleFsDataStorage::getStorageStats() const
{
    StorageStats stats{};
    if (statsProvider_) {
        uint32_t totalBytes = 0;
        uint32_t usedBytes = 0;
        if (statsProvider_(totalBytes, usedBytes)) {
            stats.totalBytes = totalBytes;
            stats.usedBytes = usedBytes;
        }
    }
    return stats;
}

std::string LittleFsDataStorage::histDir() const { return basePath_ + "/hist"; }

std::string LittleFsDataStorage::metricDir(const std::string& metric) const
{
    return histDir() + "/" + metric;
}

std::string LittleFsDataStorage::eventsDir() const
{
    return basePath_ + "/events";
}

std::string LittleFsDataStorage::eventPath(int index) const
{
    return eventsDir() + "/" + std::to_string(index) + ".log";
}

int LittleFsDataStorage::activeEventIndex() const
{
    // T024 (user story 3): event log not implemented yet.
    return 0;
}
