// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LittleFsDataStorage.cpp
 * @brief IDataStorage over POSIX file I/O (sensor history + event log).
 *
 * POSIX stdio only, no esp_littlefs/IDF includes: the identical code runs
 * against the /storage littlefs VFS mount on target and a temp directory
 * in the linux-target host tests (research.md D4). On-disk formats per
 * specs/003-nvs-littlefs-storage/data-model.md; durability per research D5
 * (fflush+fsync per append, remove() for eviction, torn tails repaired by
 * truncating to the valid prefix before appending).
 */

#include "storage/LittleFsDataStorage.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

/// One history chunk file, identified by its parsed filename epoch.
struct ChunkRef {
    uint32_t firstEpoch = 0;
    std::string name;
};

/// Metric names become directory names; empty, '/' or ".." are unsafe
/// (defensive — a buggy caller must not escape the history tree).
bool isValidMetricName(const std::string& metric)
{
    return !metric.empty() && metric.find('/') == std::string::npos &&
           metric.find("..") == std::string::npos;
}

/// mkdir that treats an already existing directory as success.
bool ensureDir(const std::string& path)
{
    return ::mkdir(path.c_str(), 0775) == 0 || errno == EEXIST;
}

bool isDir(const std::string& path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/// File size in bytes, or -1 when the file cannot be stat'ed.
long fileSize(const std::string& path)
{
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) {
        return -1;
    }
    return static_cast<long>(st.st_size);
}

/// Parse "<decimal uint32>.dat"; anything else is not a chunk file.
bool parseChunkName(const char* name, uint32_t& firstEpochOut)
{
    const char* dot = std::strchr(name, '.');
    if (dot == nullptr || dot == name || std::strcmp(dot, ".dat") != 0) {
        return false;
    }
    unsigned long long value = 0;
    for (const char* p = name; p != dot; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = value * 10 + static_cast<unsigned long long>(*p - '0');
        if (value > UINT32_MAX) {
            return false;
        }
    }
    firstEpochOut = static_cast<uint32_t>(value);
    return true;
}

/// Chunk files of one metric directory, sorted ascending by filename
/// epoch (oldest first). Empty when the directory does not exist.
std::vector<ChunkRef> listChunks(const std::string& dir)
{
    std::vector<ChunkRef> chunks;
    if (DIR* d = ::opendir(dir.c_str())) {
        while (const dirent* entry = ::readdir(d)) {
            uint32_t firstEpoch = 0;
            if (parseChunkName(entry->d_name, firstEpoch)) {
                chunks.push_back(ChunkRef{firstEpoch, entry->d_name});
            }
        }
        ::closedir(d);
    }
    std::sort(chunks.begin(), chunks.end(),
              [](const ChunkRef& a, const ChunkRef& b) {
                  return a.firstEpoch < b.firstEpoch;
              });
    return chunks;
}

/// Number of subdirectories (metric directories) under `dir`.
std::size_t countSubdirs(const std::string& dir)
{
    std::size_t count = 0;
    if (DIR* d = ::opendir(dir.c_str())) {
        while (const dirent* entry = ::readdir(d)) {
            if (std::strcmp(entry->d_name, ".") == 0 ||
                std::strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (isDir(dir + "/" + entry->d_name)) {
                ++count;
            }
        }
        ::closedir(d);
    }
    return count;
}

// Record codec: 8 bytes, explicitly little-endian {uint32 epoch,
// float value-bits} so files are portable between host and target.

void encodeRecord(uint8_t out[LittleFsDataStorage::kHistoryRecordBytes],
                  uint32_t epoch, float value)
{
    uint32_t valueBits = 0;
    std::memcpy(&valueBits, &value, sizeof(valueBits));
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<uint8_t>((epoch >> (8 * i)) & 0xFF);
        out[4 + i] = static_cast<uint8_t>((valueBits >> (8 * i)) & 0xFF);
    }
}

uint32_t decodeU32Le(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

float decodeFloatLe(const uint8_t* bytes)
{
    const uint32_t valueBits = decodeU32Le(bytes);
    float value = 0.0f;
    std::memcpy(&value, &valueBits, sizeof(value));
    return value;
}

// Event-log codec: 0xE7-framed records {marker, uint32 LE epoch,
// uint8 category, uint8 detail_len, detail bytes} (data-model.md).

/// Valid framed prefix of one event file: records in append order plus
/// the byte length of the parseable prefix. A torn tail — bad marker or
/// a frame shorter than its declared length, i.e. a power loss
/// mid-append — ends the prefix; everything before it stays usable
/// (contract invariant 2). An absent file is an empty log.
struct ParsedEventFile {
    std::vector<EventRecord> records;
    long validBytes = 0;
};

ParsedEventFile parseEventFile(const std::string& path)
{
    ParsedEventFile parsed;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return parsed;
    }
    uint8_t header[LittleFsDataStorage::kEventHeaderBytes];
    char detail[UINT8_MAX];  // detail_len is one byte
    for (;;) {
        if (std::fread(header, 1, sizeof(header), file) != sizeof(header) ||
            header[0] != LittleFsDataStorage::kEventMarker) {
            break;  // torn/absent frame header: end of the valid prefix
        }
        const std::size_t detailLen = header[6];
        if (std::fread(detail, 1, detailLen, file) != detailLen) {
            break;  // declared length exceeds the file: torn detail
        }
        parsed.records.push_back(EventRecord{
            decodeU32Le(header + 1), header[5], std::string(detail, detailLen)});
        parsed.validBytes += static_cast<long>(sizeof(header) + detailLen);
    }
    std::fclose(file);
    return parsed;
}

/// Create-or-empty a file: rotation truncates the standby event file
/// before switching appends to it (::truncate cannot create).
bool truncateToEmpty(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    return std::fclose(file) == 0;
}

}  // namespace

LittleFsDataStorage::LittleFsDataStorage(std::string basePath,
                                         StatsProvider statsProvider)
    : basePath_(std::move(basePath)), statsProvider_(std::move(statsProvider))
{
}

bool LittleFsDataStorage::storeSensorReading(const std::string& metric,
                                             uint32_t epoch, float value)
{
    if (!isValidMetricName(metric)) {
        return false;
    }
    const std::string hist = histDir();
    const std::string dir = metricDir(metric);
    if (!isDir(dir)) {
        if (!ensureDir(basePath_) || !ensureDir(hist)) {
            return false;
        }
        if (countSubdirs(hist) >= kMaxMetrics) {
            return false;  // budget guard: 11th distinct metric rejected
        }
        if (!ensureDir(dir)) {
            return false;
        }
    }

    // Derive the active chunk from the files (stateless across restarts).
    std::vector<ChunkRef> chunks = listChunks(dir);
    std::string activePath;
    if (!chunks.empty()) {
        activePath = dir + "/" + chunks.back().name;
        long size = fileSize(activePath);
        if (size < 0) {
            return false;
        }
        const long torn = size % static_cast<long>(kHistoryRecordBytes);
        if (torn != 0) {
            // Repair a torn tail (power loss mid-append) so committed
            // records stay 8-byte aligned and parseable.
            if (::truncate(activePath.c_str(), size - torn) != 0) {
                return false;
            }
            size -= torn;
        }
        if (static_cast<std::size_t>(size) >= kHistoryChunkMaxBytes) {
            activePath.clear();  // sealed at 8 KiB — start a successor
        }
    }
    if (activePath.empty()) {
        if (chunks.size() >= kHistoryMaxChunksPerMetric) {
            // Ring bound: creating chunk #11 deletes the oldest.
            if (std::remove((dir + "/" + chunks.front().name).c_str()) != 0) {
                return false;
            }
            chunks.erase(chunks.begin());
        }
        // Filename = first record's epoch. A non-monotonic epoch that
        // does not sort after the sealed chunk is bumped past it: chunk
        // names must stay strictly increasing for ordering/eviction
        // (time correctness is the caller's concern, parity 184).
        uint32_t chunkEpoch = epoch;
        if (!chunks.empty() && chunkEpoch <= chunks.back().firstEpoch) {
            chunkEpoch = chunks.back().firstEpoch + 1;
        }
        activePath = dir + "/" + std::to_string(chunkEpoch) + ".dat";
    }

    FILE* file = std::fopen(activePath.c_str(), "ab");
    if (file == nullptr) {
        return false;
    }
    uint8_t record[kHistoryRecordBytes];
    encodeRecord(record, epoch, value);
    // Durable once true is returned: flush stdio, then sync to flash.
    bool ok = std::fwrite(record, 1, sizeof(record), file) == sizeof(record) &&
              std::fflush(file) == 0 && ::fsync(fileno(file)) == 0;
    ok = (std::fclose(file) == 0) && ok;
    return ok;
}

std::vector<SensorReading> LittleFsDataStorage::getSensorReadings(
    const std::string& metric, uint32_t t0, uint32_t t1) const
{
    std::vector<SensorReading> result;
    if (t0 > t1 || !isValidMetricName(metric)) {
        return result;  // contract: empty, never an error
    }
    const std::string dir = metricDir(metric);
    // At most 10 chunks (80 KiB) per metric: scanning every chunk and
    // filtering per record is cheap and stays correct for any input.
    for (const ChunkRef& chunk : listChunks(dir)) {
        FILE* file = std::fopen((dir + "/" + chunk.name).c_str(), "rb");
        if (file == nullptr) {
            continue;  // unreadable chunk: skip, never fail the query
        }
        uint8_t record[kHistoryRecordBytes];
        // A short final read is a torn tail — logically truncated here.
        while (std::fread(record, 1, sizeof(record), file) == sizeof(record)) {
            const uint32_t epoch = decodeU32Le(record);
            if (epoch >= t0 && epoch <= t1) {
                result.push_back(
                    SensorReading{metric, epoch, decodeFloatLe(record + 4)});
            }
        }
        std::fclose(file);
    }
    return result;
}

bool LittleFsDataStorage::storeEvent(uint32_t epoch, uint8_t category,
                                     const std::string& detail)
{
    if (!ensureDir(basePath_) || !ensureDir(eventsDir())) {
        return false;
    }
    // Contract: an over-long detail is silently truncated — the event
    // itself is always recorded, never rejected for length.
    const std::size_t detailLen = std::min(detail.size(), kEventDetailMaxLen);
    const std::size_t recordBytes = kEventHeaderBytes + detailLen;

    int active = activeEventIndex();
    std::string path = eventPath(active);
    const ParsedEventFile parsed = parseEventFile(path);
    if (fileSize(path) > parsed.validBytes) {
        // Repair a torn tail (power loss mid-append) so the new record
        // lands on a frame boundary and the whole file stays parseable.
        if (::truncate(path.c_str(), parsed.validBytes) != 0) {
            return false;
        }
    }
    if (parsed.validBytes + static_cast<long>(recordBytes) >
        static_cast<long>(kEventFileMaxBytes)) {
        // Rotation (data-model.md): the append would exceed the 16 KiB
        // cap, so truncate the standby file and switch appends to it —
        // the oldest half is dropped, the newest records always kept.
        active = 1 - active;
        path = eventPath(active);
        if (!truncateToEmpty(path)) {
            return false;
        }
    }

    uint8_t header[kEventHeaderBytes];
    header[0] = kEventMarker;
    for (int i = 0; i < 4; ++i) {
        header[1 + i] = static_cast<uint8_t>((epoch >> (8 * i)) & 0xFF);
    }
    header[5] = category;
    header[6] = static_cast<uint8_t>(detailLen);

    FILE* file = std::fopen(path.c_str(), "ab");
    if (file == nullptr) {
        return false;
    }
    // Durable once true is returned: flush stdio, then sync to flash.
    bool ok = std::fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
              std::fwrite(detail.data(), 1, detailLen, file) == detailLen &&
              std::fflush(file) == 0 && ::fsync(fileno(file)) == 0;
    ok = (std::fclose(file) == 0) && ok;
    return ok;
}

std::vector<EventRecord> LittleFsDataStorage::getEvents(
    std::size_t maxCount) const
{
    // Each file holds records in append order; the active file's records
    // are all newer than the standby's (rotation empties the file it
    // switches to). Newest-first therefore = active reversed, then
    // standby reversed. Files are <= 16 KiB and event rates are low, so
    // re-parsing per call keeps this stateless across restarts.
    const int active = activeEventIndex();
    const ParsedEventFile newer = parseEventFile(eventPath(active));
    const ParsedEventFile older = parseEventFile(eventPath(1 - active));

    std::vector<EventRecord> result;
    result.reserve(
        std::min(maxCount, newer.records.size() + older.records.size()));
    for (const ParsedEventFile* file : {&newer, &older}) {
        for (auto it = file->records.rbegin();
             it != file->records.rend() && result.size() < maxCount; ++it) {
            result.push_back(*it);
        }
    }
    return result;
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
    // Restart-recovery rule, derived from the files alone (stateless):
    // the file whose last valid record carries the newest epoch is the
    // active one — appends always extend the newest end of the log. A
    // file without valid records cannot hold the newest half; both
    // empty/absent means a fresh log starting at 0. Equal last epochs
    // (several events within one second around a rotation) break the
    // tie toward the smaller file: right after a rotation the active
    // file is the freshly truncated, smaller one while the sealed
    // standby sits near the 16 KiB cap. A full file picked as active
    // self-corrects on the next append (rotation switches away from
    // it), so a wrong pick cannot wedge the log. Epoch ordering assumes
    // the caller's clock — time correctness is the caller's concern
    // (parity checklist 184).
    const ParsedEventFile file0 = parseEventFile(eventPath(0));
    const ParsedEventFile file1 = parseEventFile(eventPath(1));
    if (file1.records.empty()) {
        return 0;  // covers both-empty: a fresh log starts at 0
    }
    if (file0.records.empty()) {
        return 1;
    }
    const uint32_t last0 = file0.records.back().epoch;
    const uint32_t last1 = file1.records.back().epoch;
    if (last0 != last1) {
        return last0 > last1 ? 0 : 1;
    }
    return file0.validBytes <= file1.validBytes ? 0 : 1;
}
