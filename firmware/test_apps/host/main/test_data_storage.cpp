// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_data_storage.cpp
 * @brief Host tests for the IDataStorage contract (linux preview target).
 *
 * LittleFsDataStorage runs against plain POSIX file I/O in a per-test
 * temp directory (research.md D4) — the identical code path used under
 * the /storage littlefs VFS mount on target. MockDataStorage is held to
 * the same contract invariants (FR-012).
 *
 * Coverage maps to specs/003-nvs-littlefs-storage/contracts/IDataStorage.md
 * and the data-model.md history and event layouts (tasks T018-T020, T023).
 */

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "unity.h"

#include "interfaces/IDataStorage.h"
#include "storage/LittleFsDataStorage.h"
#include "storage/LockedDataStorage.h"
#include "storage/testing/MockDataStorage.h"

namespace {

/// Default data-log interval in seconds (data-model.md: 300000 ms).
constexpr uint32_t kLogIntervalS = 300;

constexpr std::size_t kRecordsPerChunk =
    LittleFsDataStorage::kHistoryChunkMaxBytes /
    LittleFsDataStorage::kHistoryRecordBytes;  // 1024

constexpr std::size_t kMetricCapacity =
    kRecordsPerChunk * LittleFsDataStorage::kHistoryMaxChunksPerMetric;  // 10240

/// Per-test temp directory. Cleanup is RAII (scope exit) instead of the
/// Unity tearDown, which is shared by all suites in test_main.cpp.
class TempDir {
public:
    TempDir()
    {
        char templ[] = "/tmp/ws_datastore_XXXXXX";
        char* dir = ::mkdtemp(templ);
        TEST_ASSERT_NOT_NULL_MESSAGE(dir, "mkdtemp failed");
        path_ = dir;
    }
    ~TempDir() { removeTree(path_); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::string& path() const { return path_; }

private:
    static void removeTree(const std::string& path)
    {
        if (DIR* dir = ::opendir(path.c_str())) {
            while (const dirent* entry = ::readdir(dir)) {
                if (std::strcmp(entry->d_name, ".") == 0 ||
                    std::strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                removeTree(path + "/" + entry->d_name);
            }
            ::closedir(dir);
        }
        std::remove(path.c_str());  // file, or directory now empty
    }

    std::string path_;
};

/// Names of the regular entries in `dir` (no "."/".."); empty if absent.
std::vector<std::string> listDir(const std::string& dir)
{
    std::vector<std::string> names;
    if (DIR* d = ::opendir(dir.c_str())) {
        while (const dirent* entry = ::readdir(d)) {
            if (std::strcmp(entry->d_name, ".") != 0 &&
                std::strcmp(entry->d_name, "..") != 0) {
                names.emplace_back(entry->d_name);
            }
        }
        ::closedir(d);
    }
    return names;
}

std::string metricDirOf(const TempDir& dir, const std::string& metric)
{
    return dir.path() + "/hist/" + metric;
}

long sizeOf(const std::string& path)
{
    struct stat st {};
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, ::stat(path.c_str(), &st), path.c_str());
    return static_cast<long>(st.st_size);
}

void appendGarbage(const std::string& path, std::size_t bytes)
{
    FILE* file = std::fopen(path.c_str(), "ab");
    TEST_ASSERT_NOT_NULL(file);
    const std::string garbage(bytes, 'X');
    TEST_ASSERT_EQUAL_size_t(bytes,
                             std::fwrite(garbage.data(), 1, bytes, file));
    TEST_ASSERT_EQUAL_INT(0, std::fclose(file));
}

// --- T018: range-query semantics (FR-009) -------------------------------

void test_query_is_chronological_and_inclusive(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());

    TEST_ASSERT_TRUE(storage.storeSensorReading("soil_moisture", 100, 1.0f));
    TEST_ASSERT_TRUE(storage.storeSensorReading("soil_moisture", 200, 2.0f));
    TEST_ASSERT_TRUE(storage.storeSensorReading("soil_moisture", 300, 3.0f));
    TEST_ASSERT_TRUE(storage.storeSensorReading("soil_moisture", 400, 4.0f));

    // Both bounds inclusive, results in chronological order.
    const auto mid = storage.getSensorReadings("soil_moisture", 200, 300);
    TEST_ASSERT_EQUAL_size_t(2, mid.size());
    TEST_ASSERT_EQUAL_UINT32(200, mid[0].epoch);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, mid[0].value);
    TEST_ASSERT_EQUAL_STRING("soil_moisture", mid[0].metric.c_str());
    TEST_ASSERT_EQUAL_UINT32(300, mid[1].epoch);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, mid[1].value);

    // Degenerate single-point range still matches inclusively.
    const auto point = storage.getSensorReadings("soil_moisture", 400, 400);
    TEST_ASSERT_EQUAL_size_t(1, point.size());
    TEST_ASSERT_EQUAL_UINT32(400, point[0].epoch);

    const auto all = storage.getSensorReadings("soil_moisture", 0, UINT32_MAX);
    TEST_ASSERT_EQUAL_size_t(4, all.size());
    for (std::size_t i = 1; i < all.size(); ++i) {
        TEST_ASSERT_TRUE(all[i - 1].epoch < all[i].epoch);
    }
}

void test_query_empty_never_an_error(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());

    // No data at all (not even the /hist directory).
    TEST_ASSERT_TRUE(storage.getSensorReadings("soil_moisture", 0, 100).empty());

    TEST_ASSERT_TRUE(storage.storeSensorReading("soil_moisture", 100, 1.0f));

    // Unknown metric.
    TEST_ASSERT_TRUE(
        storage.getSensorReadings("env_temperature", 0, 1000).empty());
    // t0 > t1.
    TEST_ASSERT_TRUE(storage.getSensorReadings("soil_moisture", 200, 100).empty());
    // Valid range containing no records.
    TEST_ASSERT_TRUE(storage.getSensorReadings("soil_moisture", 101, 999).empty());
}

void test_unsafe_metric_names_rejected(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());

    // Metric names become directory names: empty, '/' and ".." are unsafe.
    const char* bad[] = {"", "a/b", "..", "a..b", "/abs"};
    for (const char* name : bad) {
        TEST_ASSERT_FALSE_MESSAGE(storage.storeSensorReading(name, 100, 1.0f),
                                  name);
        TEST_ASSERT_TRUE_MESSAGE(
            storage.getSensorReadings(name, 0, UINT32_MAX).empty(), name);
    }
    // A rejected name must not consume a metric-directory slot.
    TEST_ASSERT_TRUE(listDir(dir.path() + "/hist").empty());
}

// --- T018: MockDataStorage contract conformance (FR-012) ----------------

void test_mock_holds_range_query_contract(void)
{
    MockDataStorage mock;

    TEST_ASSERT_TRUE(mock.storeSensorReading("soil_moisture", 100, 1.0f));
    TEST_ASSERT_TRUE(mock.storeSensorReading("soil_moisture", 200, 2.0f));
    TEST_ASSERT_TRUE(mock.storeSensorReading("soil_moisture", 300, 3.0f));

    const auto mid = mock.getSensorReadings("soil_moisture", 100, 200);
    TEST_ASSERT_EQUAL_size_t(2, mid.size());
    TEST_ASSERT_EQUAL_UINT32(100, mid[0].epoch);
    TEST_ASSERT_EQUAL_UINT32(200, mid[1].epoch);

    TEST_ASSERT_TRUE(mock.getSensorReadings("env_humidity", 0, 1000).empty());
    TEST_ASSERT_TRUE(mock.getSensorReadings("soil_moisture", 300, 100).empty());
    TEST_ASSERT_FALSE(mock.storeSensorReading("a/b", 100, 1.0f));
    TEST_ASSERT_FALSE(mock.storeSensorReading("", 100, 1.0f));
}

void test_mock_holds_bounds_and_event_contract(void)
{
    MockDataStorage mock;

    // kMaxMetrics distinct metrics accepted, one more rejected.
    for (std::size_t i = 0; i < IDataStorage::kMaxMetrics; ++i) {
        TEST_ASSERT_TRUE(
            mock.storeSensorReading("metric_" + std::to_string(i), 100, 1.0f));
    }
    TEST_ASSERT_FALSE(mock.storeSensorReading("metric_extra", 100, 1.0f));
    TEST_ASSERT_TRUE(mock.storeSensorReading("metric_0", 200, 2.0f));

    // Over-long event detail truncated, never rejected; newest-first reads.
    const std::string longDetail(IDataStorage::kEventDetailMaxLen + 80, 'x');
    TEST_ASSERT_TRUE(
        mock.storeEvent(10, IDataStorage::kCategoryPump, longDetail));
    TEST_ASSERT_TRUE(mock.storeEvent(20, IDataStorage::kCategoryFailsafe, "b"));
    const auto events = mock.getEvents(10);
    TEST_ASSERT_EQUAL_size_t(2, events.size());
    TEST_ASSERT_EQUAL_UINT32(20, events[0].epoch);
    TEST_ASSERT_EQUAL_size_t(IDataStorage::kEventDetailMaxLen,
                             events[1].detail.size());
    TEST_ASSERT_EQUAL_size_t(1, mock.getEvents(1).size());

    // Injected stats returned verbatim; failWrites fails both stores.
    mock.stats = StorageStats{4096, 1024};
    TEST_ASSERT_EQUAL_UINT32(4096, mock.getStorageStats().totalBytes);
    TEST_ASSERT_EQUAL_UINT32(1024, mock.getStorageStats().usedBytes);
    mock.failWrites = true;
    TEST_ASSERT_FALSE(mock.storeSensorReading("metric_0", 300, 3.0f));
    TEST_ASSERT_FALSE(mock.storeEvent(30, IDataStorage::kCategoryReset, "c"));
}

// --- getStorageStats provider paths (FR-008) -----------------------------

void test_storage_stats_provider_paths(void)
{
    TempDir dir;

    // No provider injected -> zeros (the absent/failing-provider contract).
    LittleFsDataStorage noProvider(dir.path());
    const StorageStats none = noProvider.getStorageStats();
    TEST_ASSERT_EQUAL_UINT32(0, none.totalBytes);
    TEST_ASSERT_EQUAL_UINT32(0, none.usedBytes);

    // Provider returning known total/used -> those values verbatim.
    LittleFsDataStorage withProvider(
        dir.path(), [](uint32_t& total, uint32_t& used) {
            total = 983040;
            used = 12288;
            return true;
        });
    const StorageStats stats = withProvider.getStorageStats();
    TEST_ASSERT_EQUAL_UINT32(983040, stats.totalBytes);
    TEST_ASSERT_EQUAL_UINT32(12288, stats.usedBytes);
}

// --- T019: bounding (FR-010, SC-004) -------------------------------------

/// Append `count` records spaced `stepS` seconds from `firstEpoch`,
/// value = record index. Asserts once on the aggregate failure count
/// (per-append asserts would dominate the endurance runtime).
void appendSeries(LittleFsDataStorage& storage, const std::string& metric,
                  uint32_t firstEpoch, std::size_t count, uint32_t stepS)
{
    std::size_t failures = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!storage.storeSensorReading(
                metric, firstEpoch + static_cast<uint32_t>(i) * stepS,
                static_cast<float>(i))) {
            ++failures;
        }
    }
    TEST_ASSERT_EQUAL_size_t(0, failures);
}

void test_chunk_seals_at_1024_records(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());
    const std::string metric = "soil_moisture";
    const uint32_t base = 1000;

    appendSeries(storage, metric, base, kRecordsPerChunk, 1);
    TEST_ASSERT_EQUAL_size_t(1, listDir(metricDirOf(dir, metric)).size());

    // Record 1025 seals the chunk at 8 KiB and opens a successor.
    TEST_ASSERT_TRUE(storage.storeSensorReading(
        metric, base + static_cast<uint32_t>(kRecordsPerChunk), 9.0f));
    TEST_ASSERT_EQUAL_size_t(2, listDir(metricDirOf(dir, metric)).size());

    // All records remain retrievable across the chunk boundary.
    const auto all = storage.getSensorReadings(metric, 0, UINT32_MAX);
    TEST_ASSERT_EQUAL_size_t(kRecordsPerChunk + 1, all.size());
    TEST_ASSERT_EQUAL_FLOAT(9.0f, all.back().value);
}

void test_non_monotonic_epoch_bumps_chunk_name(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());
    const std::string metric = "soil_moisture";
    const uint32_t base = 1000;

    // Seal the first chunk (1024 records) so a second chunk is opened by
    // the next append; the sealed chunk's first epoch is `base`.
    appendSeries(storage, metric, base, kRecordsPerChunk, 1);
    TEST_ASSERT_EQUAL_size_t(1, listDir(metricDirOf(dir, metric)).size());

    // The successor chunk's name = its first record's epoch. Feed an epoch
    // EARLIER than the sealed chunk's first epoch: the chunk name must be
    // bumped past the sealed one (LittleFsDataStorage.cpp:268-272) so names
    // stay strictly increasing — no collision with the existing chunk.
    const uint32_t earlier = base - 500;
    TEST_ASSERT_TRUE(storage.storeSensorReading(metric, earlier, 7.0f));

    // Exactly two distinct chunk files exist (no name collision).
    const auto chunks = listDir(metricDirOf(dir, metric));
    TEST_ASSERT_EQUAL_size_t(2, chunks.size());
    TEST_ASSERT_TRUE(chunks[0] != chunks[1]);

    // The earlier-epoch record is still retrievable over a range covering it.
    const auto hit = storage.getSensorReadings(metric, 0, base - 1);
    TEST_ASSERT_EQUAL_size_t(1, hit.size());
    TEST_ASSERT_EQUAL_UINT32(earlier, hit[0].epoch);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, hit[0].value);
}

void test_ring_evicts_oldest_chunk(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());
    const std::string metric = "soil_moisture";
    const uint32_t base = 1000000;

    // Fill the ring exactly: 10 chunks x 1024 records, nothing evicted.
    appendSeries(storage, metric, base, kMetricCapacity, 1);
    TEST_ASSERT_EQUAL_size_t(LittleFsDataStorage::kHistoryMaxChunksPerMetric,
                             listDir(metricDirOf(dir, metric)).size());

    // One more record needs an 11th chunk -> the oldest chunk is removed.
    const uint32_t next = base + static_cast<uint32_t>(kMetricCapacity);
    TEST_ASSERT_TRUE(storage.storeSensorReading(metric, next, -1.0f));
    TEST_ASSERT_EQUAL_size_t(LittleFsDataStorage::kHistoryMaxChunksPerMetric,
                             listDir(metricDirOf(dir, metric)).size());

    // The first chunk's records are gone, everything newer is intact.
    TEST_ASSERT_TRUE(
        storage
            .getSensorReadings(metric, base,
                               base + static_cast<uint32_t>(kRecordsPerChunk) - 1)
            .empty());
    const auto rest = storage.getSensorReadings(metric, 0, UINT32_MAX);
    TEST_ASSERT_EQUAL_size_t(kMetricCapacity - kRecordsPerChunk + 1,
                             rest.size());
    TEST_ASSERT_EQUAL_UINT32(base + static_cast<uint32_t>(kRecordsPerChunk),
                             rest.front().epoch);
    TEST_ASSERT_EQUAL_UINT32(next, rest.back().epoch);
}

void test_retention_30_days_at_default_interval(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());
    const std::string metric = "env_temperature";
    const uint32_t base = 1700000000;

    // 30 days at the 5-min default log interval = 8640 records; the
    // 10-chunk ring (10240 records) must hold them all (FR-010).
    constexpr std::size_t k30Days = 30u * 24u * 3600u / kLogIntervalS;
    appendSeries(storage, metric, base, k30Days, kLogIntervalS);

    const auto all = storage.getSensorReadings(metric, 0, UINT32_MAX);
    TEST_ASSERT_EQUAL_size_t(k30Days, all.size());
    TEST_ASSERT_EQUAL_UINT32(base, all.front().epoch);  // nothing evicted
}

void test_endurance_ten_times_the_bound(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());
    const std::string metric = "soil_moisture";
    const uint32_t base = 1000;

    // SC-004: 10x the per-metric bound (100 chunk-fulls) without a write
    // failure or budget overrun.
    constexpr std::size_t kAppends = 10 * kMetricCapacity;
    appendSeries(storage, metric, base, kAppends, 1);

    TEST_ASSERT_EQUAL_size_t(LittleFsDataStorage::kHistoryMaxChunksPerMetric,
                             listDir(metricDirOf(dir, metric)).size());

    // The newest records are intact after ~90 evictions.
    const uint32_t last = base + static_cast<uint32_t>(kAppends) - 1;
    const auto tail = storage.getSensorReadings(metric, last - 99, last);
    TEST_ASSERT_EQUAL_size_t(100, tail.size());
    TEST_ASSERT_EQUAL_UINT32(last, tail.back().epoch);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(kAppends - 1),
                            tail.back().value);
}

void test_eleventh_distinct_metric_rejected(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());

    for (std::size_t i = 0; i < IDataStorage::kMaxMetrics; ++i) {
        TEST_ASSERT_TRUE(
            storage.storeSensorReading("metric_" + std::to_string(i), 100, 1.0f));
    }
    TEST_ASSERT_FALSE(storage.storeSensorReading("metric_extra", 100, 1.0f));
    TEST_ASSERT_TRUE(
        storage.getSensorReadings("metric_extra", 0, UINT32_MAX).empty());
    TEST_ASSERT_EQUAL_size_t(IDataStorage::kMaxMetrics,
                             listDir(dir.path() + "/hist").size());

    // Existing metrics keep accepting appends at the metric cap.
    TEST_ASSERT_TRUE(storage.storeSensorReading("metric_0", 200, 2.0f));
}

// --- T020: torn tails (research D5, contract invariant 2) ----------------

/// Path of the single chunk file of `metric` (asserts exactly one exists).
std::string singleChunkPath(const TempDir& dir, const std::string& metric)
{
    const std::string metricDir = metricDirOf(dir, metric);
    const auto chunks = listDir(metricDir);
    TEST_ASSERT_EQUAL_size_t(1, chunks.size());
    return metricDir + "/" + chunks[0];
}

void test_torn_tail_truncated_on_read(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());
    const std::string metric = "soil_moisture";

    TEST_ASSERT_TRUE(storage.storeSensorReading(metric, 100, 1.0f));
    TEST_ASSERT_TRUE(storage.storeSensorReading(metric, 200, 2.0f));
    TEST_ASSERT_TRUE(storage.storeSensorReading(metric, 300, 3.0f));

    // Simulate a power loss mid-append: a partial trailing record.
    const std::string chunk = singleChunkPath(dir, metric);
    appendGarbage(chunk, 5);
    TEST_ASSERT_EQUAL_INT(
        5, static_cast<int>(
               sizeOf(chunk) % LittleFsDataStorage::kHistoryRecordBytes));

    // The torn tail is logically truncated; earlier records are intact.
    const auto all = storage.getSensorReadings(metric, 0, UINT32_MAX);
    TEST_ASSERT_EQUAL_size_t(3, all.size());
    TEST_ASSERT_EQUAL_UINT32(100, all[0].epoch);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, all[0].value);
    TEST_ASSERT_EQUAL_UINT32(300, all[2].epoch);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, all[2].value);
}

void test_torn_tail_repaired_before_append(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());
    const std::string metric = "soil_moisture";

    TEST_ASSERT_TRUE(storage.storeSensorReading(metric, 100, 1.0f));
    TEST_ASSERT_TRUE(storage.storeSensorReading(metric, 200, 2.0f));
    const std::string chunk = singleChunkPath(dir, metric);
    appendGarbage(chunk, 3);

    // The next append truncates the torn tail so committed records stay
    // 8-byte aligned and parseable.
    TEST_ASSERT_TRUE(storage.storeSensorReading(metric, 300, 3.0f));
    TEST_ASSERT_EQUAL_INT(
        0, static_cast<int>(
               sizeOf(chunk) % LittleFsDataStorage::kHistoryRecordBytes));

    const auto all = storage.getSensorReadings(metric, 0, UINT32_MAX);
    TEST_ASSERT_EQUAL_size_t(3, all.size());
    TEST_ASSERT_EQUAL_UINT32(200, all[1].epoch);
    TEST_ASSERT_EQUAL_UINT32(300, all[2].epoch);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, all[2].value);
}

// --- T023: event log (FR-011, storeEvent/getEvents contract) -------------

std::string eventFileOf(const TempDir& dir, int index)
{
    return dir.path() + "/events/" + std::to_string(index) + ".log";
}

std::vector<uint8_t> readAll(const std::string& path)
{
    std::vector<uint8_t> bytes;
    FILE* file = std::fopen(path.c_str(), "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(file, path.c_str());
    uint8_t buf[256];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), file)) > 0) {
        bytes.insert(bytes.end(), buf, buf + n);
    }
    TEST_ASSERT_EQUAL_INT(0, std::fclose(file));
    return bytes;
}

void appendBytes(const std::string& path, const uint8_t* bytes,
                 std::size_t count)
{
    FILE* file = std::fopen(path.c_str(), "ab");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_size_t(count, std::fwrite(bytes, 1, count, file));
    TEST_ASSERT_EQUAL_INT(0, std::fclose(file));
}

/// 9-byte detail encoding `index`: every framed record is then exactly
/// 16 bytes, so one event file holds exactly 1024 records and the
/// rotation boundary lands on a precise event index.
constexpr std::size_t kFixedDetailBytes = 9;
constexpr std::size_t kFixedRecordBytes =
    LittleFsDataStorage::kEventHeaderBytes + kFixedDetailBytes;  // 16
constexpr std::size_t kEventsPerFile =
    LittleFsDataStorage::kEventFileMaxBytes / kFixedRecordBytes;  // 1024
static_assert(kEventsPerFile * kFixedRecordBytes ==
                  LittleFsDataStorage::kEventFileMaxBytes,
              "fixed-size events must fill an event file exactly");

std::string fixedDetail(std::size_t index)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%09lu",
                  static_cast<unsigned long>(index));
    return std::string(buf, kFixedDetailBytes);
}

/// Append fixed-size events with epoch = firstEpoch + index and detail =
/// fixedDetail(index), for index in [firstIndex, firstIndex + count).
/// Asserts once on the aggregate failure count (per-append asserts would
/// dominate the rotation/burst runtime).
void appendEvents(LittleFsDataStorage& storage, uint32_t firstEpoch,
                  std::size_t firstIndex, std::size_t count)
{
    std::size_t failures = 0;
    for (std::size_t i = firstIndex; i < firstIndex + count; ++i) {
        if (!storage.storeEvent(firstEpoch + static_cast<uint32_t>(i),
                                IDataStorage::kCategoryPump, fixedDetail(i))) {
            ++failures;
        }
    }
    TEST_ASSERT_EQUAL_size_t(0, failures);
}

void test_event_framing_round_trip(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());

    TEST_ASSERT_TRUE(storage.storeEvent(
        0x11223344u, IDataStorage::kCategoryConnectivity, "abc"));

    // On-disk frame (data-model.md): marker 0xE7, uint32 LE epoch,
    // uint8 category, uint8 detail_len, detail bytes. Fresh log -> 0.log.
    const auto raw = readAll(eventFileOf(dir, 0));
    const uint8_t expected[] = {0xE7, 0x44, 0x33, 0x22, 0x11,
                                0x03, 0x03, 'a',  'b',  'c'};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), raw.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, raw.data(), sizeof(expected));

    const auto events = storage.getEvents(10);
    TEST_ASSERT_EQUAL_size_t(1, events.size());
    TEST_ASSERT_EQUAL_UINT32(0x11223344u, events[0].epoch);
    TEST_ASSERT_EQUAL_UINT8(IDataStorage::kCategoryConnectivity,
                            events[0].category);
    TEST_ASSERT_EQUAL_STRING("abc", events[0].detail.c_str());
}

void test_get_events_newest_first_with_max_count(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());

    // No data at all (not even the /events directory) -> empty, no error.
    TEST_ASSERT_TRUE(storage.getEvents(10).empty());

    TEST_ASSERT_TRUE(storage.storeEvent(100, IDataStorage::kCategoryPump, "a"));
    TEST_ASSERT_TRUE(
        storage.storeEvent(200, IDataStorage::kCategoryFailsafe, "b"));
    TEST_ASSERT_TRUE(
        storage.storeEvent(300, IDataStorage::kCategoryConnectivity, "c"));
    TEST_ASSERT_TRUE(storage.storeEvent(400, IDataStorage::kCategoryOta, "d"));
    TEST_ASSERT_TRUE(
        storage.storeEvent(500, IDataStorage::kCategoryReset, "e"));

    const auto top = storage.getEvents(3);
    TEST_ASSERT_EQUAL_size_t(3, top.size());
    TEST_ASSERT_EQUAL_UINT32(500, top[0].epoch);
    TEST_ASSERT_EQUAL_UINT8(IDataStorage::kCategoryReset, top[0].category);
    TEST_ASSERT_EQUAL_STRING("e", top[0].detail.c_str());
    TEST_ASSERT_EQUAL_UINT32(400, top[1].epoch);
    TEST_ASSERT_EQUAL_UINT32(300, top[2].epoch);

    const auto all = storage.getEvents(100);
    TEST_ASSERT_EQUAL_size_t(5, all.size());
    for (std::size_t i = 1; i < all.size(); ++i) {
        TEST_ASSERT_TRUE(all[i - 1].epoch > all[i].epoch);
    }

    TEST_ASSERT_TRUE(storage.getEvents(0).empty());
}

void test_event_detail_truncated_not_rejected(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());

    // Over-long detail: truncated to 120 bytes, event still recorded.
    const std::string longDetail(IDataStorage::kEventDetailMaxLen + 80, 'x');
    TEST_ASSERT_TRUE(
        storage.storeEvent(100, IDataStorage::kCategoryFailsafe, longDetail));

    // The on-disk detail_len byte is the truncated length.
    const auto raw = readAll(eventFileOf(dir, 0));
    TEST_ASSERT_EQUAL_size_t(LittleFsDataStorage::kEventHeaderBytes +
                                 IDataStorage::kEventDetailMaxLen,
                             raw.size());
    TEST_ASSERT_EQUAL_UINT8(IDataStorage::kEventDetailMaxLen, raw[6]);

    // An exactly-120-byte detail is kept whole.
    const std::string maxDetail(IDataStorage::kEventDetailMaxLen, 'y');
    TEST_ASSERT_TRUE(
        storage.storeEvent(200, IDataStorage::kCategoryPump, maxDetail));

    const auto events = storage.getEvents(10);
    TEST_ASSERT_EQUAL_size_t(2, events.size());
    TEST_ASSERT_EQUAL_STRING(maxDetail.c_str(), events[0].detail.c_str());
    TEST_ASSERT_EQUAL_size_t(IDataStorage::kEventDetailMaxLen,
                             events[1].detail.size());
    TEST_ASSERT_EQUAL_STRING(
        longDetail.substr(0, IDataStorage::kEventDetailMaxLen).c_str(),
        events[1].detail.c_str());
}

void test_unknown_event_category_passthrough(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());

    // PR-08 may extend the enum: unknown values pass through verbatim.
    TEST_ASSERT_TRUE(storage.storeEvent(100, 0xCC, "future"));
    TEST_ASSERT_TRUE(storage.storeEvent(200, 0, "zero"));

    const auto events = storage.getEvents(10);
    TEST_ASSERT_EQUAL_size_t(2, events.size());
    TEST_ASSERT_EQUAL_UINT8(0, events[0].category);
    TEST_ASSERT_EQUAL_UINT8(0xCC, events[1].category);
    TEST_ASSERT_EQUAL_STRING("future", events[1].detail.c_str());
}

void test_event_rotation_drops_oldest_never_newest(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());
    const uint32_t base = 1000000;

    // Fill 0.log exactly (1024 records x 16 bytes); 1.log not yet created.
    appendEvents(storage, base, 0, kEventsPerFile);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LittleFsDataStorage::kEventFileMaxBytes),
        static_cast<int>(sizeOf(eventFileOf(dir, 0))));
    TEST_ASSERT_EQUAL_size_t(1, listDir(dir.path() + "/events").size());

    // The next append would exceed the cap -> switches to 1.log; the full
    // file is left intact (nothing dropped yet).
    appendEvents(storage, base, kEventsPerFile, 1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kFixedRecordBytes),
                          static_cast<int>(sizeOf(eventFileOf(dir, 1))));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LittleFsDataStorage::kEventFileMaxBytes),
        static_cast<int>(sizeOf(eventFileOf(dir, 0))));

    // Fill 1.log; the following append truncates 0.log (the oldest half)
    // and starts over there.
    appendEvents(storage, base, kEventsPerFile + 1, kEventsPerFile - 1);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LittleFsDataStorage::kEventFileMaxBytes),
        static_cast<int>(sizeOf(eventFileOf(dir, 1))));
    appendEvents(storage, base, 2 * kEventsPerFile, 1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kFixedRecordBytes),
                          static_cast<int>(sizeOf(eventFileOf(dir, 0))));

    // The oldest half (indices 0..1023) is gone; everything newer is
    // intact and newest-first, the newest record always retained.
    const auto events = storage.getEvents(4 * kEventsPerFile);
    TEST_ASSERT_EQUAL_size_t(kEventsPerFile + 1, events.size());
    TEST_ASSERT_EQUAL_UINT32(base + 2 * static_cast<uint32_t>(kEventsPerFile),
                             events.front().epoch);
    TEST_ASSERT_EQUAL_UINT32(base + static_cast<uint32_t>(kEventsPerFile),
                             events.back().epoch);
    for (std::size_t i = 1; i < events.size(); ++i) {
        TEST_ASSERT_TRUE(events[i - 1].epoch > events[i].epoch);
    }
}

void test_event_burst_stays_within_budget(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());
    const uint32_t base = 2000;

    // Burst of 3000 events with every detail length 0..120: several
    // rotations; no append may fail (contract: never rejected at the
    // bound) and the two files stay within the 32 KiB total budget.
    constexpr std::size_t kBurst = 3000;
    std::size_t failures = 0;
    for (std::size_t i = 0; i < kBurst; ++i) {
        const std::string detail(i % (IDataStorage::kEventDetailMaxLen + 1),
                                 'e');
        if (!storage.storeEvent(base + static_cast<uint32_t>(i),
                                IDataStorage::kCategoryPump, detail)) {
            ++failures;
        }
    }
    TEST_ASSERT_EQUAL_size_t(0, failures);

    const long size0 = sizeOf(eventFileOf(dir, 0));
    const long size1 = sizeOf(eventFileOf(dir, 1));
    TEST_ASSERT_TRUE(
        size0 <= static_cast<long>(LittleFsDataStorage::kEventFileMaxBytes));
    TEST_ASSERT_TRUE(
        size1 <= static_cast<long>(LittleFsDataStorage::kEventFileMaxBytes));
    TEST_ASSERT_TRUE(
        size0 + size1 <=
        2 * static_cast<long>(LittleFsDataStorage::kEventFileMaxBytes));

    // The newest record survived every rotation.
    const auto newest = storage.getEvents(1);
    TEST_ASSERT_EQUAL_size_t(1, newest.size());
    TEST_ASSERT_EQUAL_UINT32(base + static_cast<uint32_t>(kBurst) - 1,
                             newest[0].epoch);
}

void test_event_torn_tail_skipped_and_repaired(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());

    TEST_ASSERT_TRUE(storage.storeEvent(100, IDataStorage::kCategoryPump,
                                        "one"));
    TEST_ASSERT_TRUE(storage.storeEvent(200, IDataStorage::kCategoryReset,
                                        "two"));
    const std::string active = eventFileOf(dir, 0);
    const long valid = sizeOf(active);

    // Power loss mid-append: marker written, rest of the header torn off.
    const uint8_t torn[] = {LittleFsDataStorage::kEventMarker, 0xAA, 0xBB};
    appendBytes(active, torn, sizeof(torn));

    auto events = storage.getEvents(10);
    TEST_ASSERT_EQUAL_size_t(2, events.size());
    TEST_ASSERT_EQUAL_UINT32(200, events[0].epoch);
    TEST_ASSERT_EQUAL_UINT32(100, events[1].epoch);

    // The next append repairs the tail: the file is the valid prefix plus
    // the new frame, and every record parses.
    TEST_ASSERT_TRUE(storage.storeEvent(300, IDataStorage::kCategoryOta,
                                        "three"));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(valid + LittleFsDataStorage::kEventHeaderBytes + 5),
        static_cast<int>(sizeOf(active)));
    events = storage.getEvents(10);
    TEST_ASSERT_EQUAL_size_t(3, events.size());
    TEST_ASSERT_EQUAL_UINT32(300, events[0].epoch);
    TEST_ASSERT_EQUAL_STRING("three", events[0].detail.c_str());
    TEST_ASSERT_EQUAL_UINT32(100, events[2].epoch);
}

void test_event_torn_detail_length_mismatch_skipped(void)
{
    TempDir dir;
    LittleFsDataStorage storage(dir.path());

    TEST_ASSERT_TRUE(storage.storeEvent(100, IDataStorage::kCategoryPump,
                                        "ok"));

    // Complete header claiming 9 detail bytes, but only 3 present
    // (length mismatch at the end of the file).
    const uint8_t torn[] = {LittleFsDataStorage::kEventMarker,
                            0x01, 0x00, 0x00, 0x00,
                            IDataStorage::kCategoryPump, 9,
                            'a', 'b', 'c'};
    appendBytes(eventFileOf(dir, 0), torn, sizeof(torn));

    const auto events = storage.getEvents(10);
    TEST_ASSERT_EQUAL_size_t(1, events.size());
    TEST_ASSERT_EQUAL_UINT32(100, events[0].epoch);
    TEST_ASSERT_EQUAL_STRING("ok", events[0].detail.c_str());
}

void test_event_active_file_detected_after_restart(void)
{
    TempDir dir;
    const uint32_t base = 5000000;

    {
        // First life: rotate into 1.log and leave 5 records there.
        LittleFsDataStorage first(dir.path());
        appendEvents(first, base, 0, kEventsPerFile + 5);
    }

    // Restart: a new instance must derive the active file (1.log) from
    // the files alone and append there — no spurious rotation that would
    // truncate the full 0.log.
    LittleFsDataStorage second(dir.path());
    appendEvents(second, base, kEventsPerFile + 5, 1);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LittleFsDataStorage::kEventFileMaxBytes),
        static_cast<int>(sizeOf(eventFileOf(dir, 0))));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(6 * kFixedRecordBytes),
                          static_cast<int>(sizeOf(eventFileOf(dir, 1))));

    const auto events = second.getEvents(2);
    TEST_ASSERT_EQUAL_size_t(2, events.size());
    TEST_ASSERT_EQUAL_UINT32(
        base + static_cast<uint32_t>(kEventsPerFile) + 5, events[0].epoch);
    TEST_ASSERT_EQUAL_UINT32(
        base + static_cast<uint32_t>(kEventsPerFile) + 4, events[1].epoch);
}

void test_mock_event_bound_and_category_passthrough(void)
{
    MockDataStorage mock;

    // Unknown category passes through the mock verbatim too.
    TEST_ASSERT_TRUE(mock.storeEvent(1, 0xCC, "future"));
    TEST_ASSERT_EQUAL_UINT8(0xCC, mock.getEvents(1)[0].category);

    // At the bound the mock evicts oldest and always keeps the newest.
    for (std::size_t i = 0; i < MockDataStorage::kMaxEvents + 8; ++i) {
        TEST_ASSERT_TRUE(mock.storeEvent(100 + static_cast<uint32_t>(i),
                                         IDataStorage::kCategoryPump, "e"));
    }
    TEST_ASSERT_EQUAL_size_t(MockDataStorage::kMaxEvents, mock.events.size());
    const auto newest = mock.getEvents(1);
    TEST_ASSERT_EQUAL_size_t(1, newest.size());
    TEST_ASSERT_EQUAL_UINT32(100 + MockDataStorage::kMaxEvents + 7,
                             newest[0].epoch);
}

// --- T028: LockedDataStorage decorator (FR-013) --------------------------
// Delegates the full contract path unchanged (the wrapper adds task-level
// mutex serialization; see LockedDataStorage.h — same mechanism PR-02's
// LockedWaterPump test established). The instrumented mock proves every
// call reached the wrapped storage.

void test_locked_data_storage_delegates_full_contract(void)
{
    MockDataStorage inner;
    inner.stats = StorageStats{983040, 12288};
    LockedDataStorage storage(inner);

    // storeSensorReading: accepted appends and the rejection paths pass
    // through unchanged.
    TEST_ASSERT_TRUE(storage.storeSensorReading("soil_moisture", 100, 1.0f));
    TEST_ASSERT_TRUE(storage.storeSensorReading("soil_moisture", 200, 2.0f));
    TEST_ASSERT_TRUE(storage.storeSensorReading("soil_moisture", 300, 3.0f));
    TEST_ASSERT_FALSE(storage.storeSensorReading("bad/metric", 400, 4.0f));
    TEST_ASSERT_EQUAL(3, inner.acceptedWrites);
    TEST_ASSERT_EQUAL(1, inner.rejectedWrites);

    // getSensorReadings: inclusive chronological range, empty on t0 > t1.
    const auto mid = storage.getSensorReadings("soil_moisture", 200, 300);
    TEST_ASSERT_EQUAL_size_t(2, mid.size());
    TEST_ASSERT_EQUAL_UINT32(200, mid[0].epoch);
    TEST_ASSERT_EQUAL_UINT32(300, mid[1].epoch);
    TEST_ASSERT_TRUE(storage.getSensorReadings("soil_moisture", 300, 200).empty());

    // storeEvent: detail truncation happens behind the wrapper.
    const std::string longDetail(IDataStorage::kEventDetailMaxLen + 30, 'd');
    TEST_ASSERT_TRUE(storage.storeEvent(500, IDataStorage::kCategoryPump,
                                        "pump started"));
    TEST_ASSERT_TRUE(storage.storeEvent(600, IDataStorage::kCategoryFailsafe,
                                        longDetail));

    // getEvents: newest-first with maxCount.
    const auto events = storage.getEvents(2);
    TEST_ASSERT_EQUAL_size_t(2, events.size());
    TEST_ASSERT_EQUAL_UINT32(600, events[0].epoch);
    TEST_ASSERT_EQUAL_size_t(IDataStorage::kEventDetailMaxLen,
                             events[0].detail.size());
    TEST_ASSERT_EQUAL_UINT32(500, events[1].epoch);
    TEST_ASSERT_EQUAL_STRING("pump started", events[1].detail.c_str());

    // getStorageStats: injected stats come back verbatim.
    const StorageStats stats = storage.getStorageStats();
    TEST_ASSERT_EQUAL_UINT32(983040, stats.totalBytes);
    TEST_ASSERT_EQUAL_UINT32(12288, stats.usedBytes);

    // A persistence failure surfaces through the wrapper unchanged.
    inner.failWrites = true;
    TEST_ASSERT_FALSE(storage.storeSensorReading("soil_moisture", 700, 7.0f));
    TEST_ASSERT_FALSE(storage.storeEvent(700, IDataStorage::kCategoryPump, "x"));
}

// The wrapped LittleFsDataStorage keeps its contract behind the wrapper
// (real-store spot check: on-disk round-trip through the mutex).
void test_locked_data_storage_over_real_storage(void)
{
    TempDir dir;
    LittleFsDataStorage inner(dir.path());
    LockedDataStorage storage(inner);

    TEST_ASSERT_TRUE(storage.storeSensorReading("env_temperature", 100, 21.5f));
    TEST_ASSERT_TRUE(storage.storeEvent(200, IDataStorage::kCategoryReset,
                                        "boot"));

    const auto readings =
        storage.getSensorReadings("env_temperature", 0, UINT32_MAX);
    TEST_ASSERT_EQUAL_size_t(1, readings.size());
    TEST_ASSERT_EQUAL_FLOAT(21.5f, readings[0].value);

    const auto events = storage.getEvents(10);
    TEST_ASSERT_EQUAL_size_t(1, events.size());
    TEST_ASSERT_EQUAL_STRING("boot", events[0].detail.c_str());
}

}  // namespace

void run_data_storage_tests(void)
{
    // T018 — range-query semantics + mock conformance.
    RUN_TEST(test_query_is_chronological_and_inclusive);
    RUN_TEST(test_query_empty_never_an_error);
    RUN_TEST(test_unsafe_metric_names_rejected);
    RUN_TEST(test_mock_holds_range_query_contract);
    RUN_TEST(test_mock_holds_bounds_and_event_contract);
    RUN_TEST(test_storage_stats_provider_paths);
    // T019 — bounding: sealing, ring eviction, retention, endurance, cap.
    RUN_TEST(test_chunk_seals_at_1024_records);
    RUN_TEST(test_non_monotonic_epoch_bumps_chunk_name);
    RUN_TEST(test_ring_evicts_oldest_chunk);
    RUN_TEST(test_retention_30_days_at_default_interval);
    RUN_TEST(test_endurance_ten_times_the_bound);
    RUN_TEST(test_eleventh_distinct_metric_rejected);
    // T020 — torn-tail handling.
    RUN_TEST(test_torn_tail_truncated_on_read);
    RUN_TEST(test_torn_tail_repaired_before_append);
    // T023 — event log: framing, retrieval, rotation, budget, torn tails.
    RUN_TEST(test_event_framing_round_trip);
    RUN_TEST(test_get_events_newest_first_with_max_count);
    RUN_TEST(test_event_detail_truncated_not_rejected);
    RUN_TEST(test_unknown_event_category_passthrough);
    RUN_TEST(test_event_rotation_drops_oldest_never_newest);
    RUN_TEST(test_event_burst_stays_within_budget);
    RUN_TEST(test_event_torn_tail_skipped_and_repaired);
    RUN_TEST(test_event_torn_detail_length_mismatch_skipped);
    RUN_TEST(test_event_active_file_detected_after_restart);
    RUN_TEST(test_mock_event_bound_and_category_passthrough);
    // T028 — Locked* decorator (FR-013).
    RUN_TEST(test_locked_data_storage_delegates_full_contract);
    RUN_TEST(test_locked_data_storage_over_real_storage);
}
