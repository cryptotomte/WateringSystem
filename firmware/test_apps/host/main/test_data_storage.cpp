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

}  // namespace

void run_data_storage_tests(void)
{
    // T018 — range-query semantics + mock conformance.
    RUN_TEST(test_query_is_chronological_and_inclusive);
    RUN_TEST(test_query_empty_never_an_error);
    RUN_TEST(test_unsafe_metric_names_rejected);
    RUN_TEST(test_mock_holds_range_query_contract);
    RUN_TEST(test_mock_holds_bounds_and_event_contract);
}
