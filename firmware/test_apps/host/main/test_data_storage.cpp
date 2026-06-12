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
 * and the data-model.md history layout (tasks T018-T020).
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

}  // namespace

void run_data_storage_tests(void)
{
    // T018 — range-query semantics + mock conformance.
    RUN_TEST(test_query_is_chronological_and_inclusive);
    RUN_TEST(test_query_empty_never_an_error);
    RUN_TEST(test_unsafe_metric_names_rejected);
    RUN_TEST(test_mock_holds_range_query_contract);
    RUN_TEST(test_mock_holds_bounds_and_event_contract);
    // T019 — bounding: sealing, ring eviction, retention, endurance, cap.
    RUN_TEST(test_chunk_seals_at_1024_records);
    RUN_TEST(test_ring_evicts_oldest_chunk);
    RUN_TEST(test_retention_30_days_at_default_interval);
    RUN_TEST(test_endurance_ten_times_the_bound);
    RUN_TEST(test_eleventh_distinct_metric_rejected);
    // T020 — torn-tail handling.
    RUN_TEST(test_torn_tail_truncated_on_read);
    RUN_TEST(test_torn_tail_repaired_before_append);
}
