// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_config_store.cpp
 * @brief Host tests for the IConfigStore contract (linux preview target).
 *
 * NvsConfigStore runs against the REAL nvs_flash implementation: the linux
 * esp_partition emulation provides the `nvs` partition from
 * partitions_host.csv (research.md D3). Every NVS test starts from an
 * erased partition (per-test isolation). MockConfigStore is held to the
 * same contract invariants (FR-012).
 *
 * Coverage maps to specs/003-nvs-littlefs-storage/contracts/IConfigStore.md
 * and the data-model.md item table (tasks T009-T012).
 */

#include <fcntl.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "unity.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "interfaces/IConfigStore.h"
#include "storage/NvsConfigStore.h"
#include "storage/testing/MockConfigStore.h"

namespace {

constexpr const char* kNamespace = "wscfg";

/// Erase + re-init the default NVS partition (per-test isolation).
/// nvs_flash_erase() deinitializes an initialized partition before erasing.
void resetNvs()
{
    TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_erase());
    TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_init());
}

uint32_t floatBits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// Raw writes bypass the store's validation to plant out-of-range STORED
// values (US1 scenario 3: shadowing, not write rejection).
void rawSetU32(const char* key, uint32_t value)
{
    nvs_handle_t handle = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_open(kNamespace, NVS_READWRITE, &handle));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_set_u32(handle, key, value));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(handle));
    nvs_close(handle);
}

void rawSetU8(const char* key, uint8_t value)
{
    nvs_handle_t handle = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_open(kNamespace, NVS_READWRITE, &handle));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_set_u8(handle, key, value));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(handle));
    nvs_close(handle);
}

void rawSetStr(const char* key, const std::string& value)
{
    nvs_handle_t handle = 0;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_open(kNamespace, NVS_READWRITE, &handle));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_set_str(handle, key, value.c_str()));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(handle));
    nvs_close(handle);
}

/// Assert that every item reads its compiled-in factory default.
void assertAllDefaults(const IConfigStore& store)
{
    TEST_ASSERT_EQUAL_FLOAT(IConfigStore::kDefaultMoistureThresholdLow,
                            store.getMoistureThresholdLow());
    TEST_ASSERT_EQUAL_FLOAT(IConfigStore::kDefaultMoistureThresholdHigh,
                            store.getMoistureThresholdHigh());
    TEST_ASSERT_EQUAL_UINT32(IConfigStore::kDefaultWateringDurationS,
                             store.getWateringDurationS());
    TEST_ASSERT_EQUAL_UINT32(IConfigStore::kDefaultMinWateringIntervalS,
                             store.getMinWateringIntervalS());
    TEST_ASSERT_EQUAL(IConfigStore::kDefaultWateringEnabled,
                      store.getWateringEnabled());
    TEST_ASSERT_EQUAL_UINT32(IConfigStore::kDefaultSensorReadIntervalMs,
                             store.getSensorReadIntervalMs());
    TEST_ASSERT_EQUAL_UINT32(IConfigStore::kDefaultDataLogIntervalMs,
                             store.getDataLogIntervalMs());
    TEST_ASSERT_EQUAL_STRING("", store.getWifiSsid().c_str());
    TEST_ASSERT_EQUAL_STRING("", store.getWifiPassword().c_str());
}

/// Write one non-default valid value per item.
void setAllNonDefaults(IConfigStore& store)
{
    TEST_ASSERT_TRUE(store.setMoistureThresholdLow(33.5f));
    TEST_ASSERT_TRUE(store.setMoistureThresholdHigh(66.25f));
    TEST_ASSERT_TRUE(store.setWateringDurationS(45));
    TEST_ASSERT_TRUE(store.setMinWateringIntervalS(600));
    TEST_ASSERT_TRUE(store.setWateringEnabled(false));
    TEST_ASSERT_TRUE(store.setSensorReadIntervalMs(2000));
    TEST_ASSERT_TRUE(store.setDataLogIntervalMs(120000));
    TEST_ASSERT_TRUE(store.setWifiCredentials("greenhouse", "round-trip-pw"));
}

/// Assert the values written by setAllNonDefaults().
void assertAllNonDefaults(const IConfigStore& store)
{
    TEST_ASSERT_EQUAL_FLOAT(33.5f, store.getMoistureThresholdLow());
    TEST_ASSERT_EQUAL_FLOAT(66.25f, store.getMoistureThresholdHigh());
    TEST_ASSERT_EQUAL_UINT32(45, store.getWateringDurationS());
    TEST_ASSERT_EQUAL_UINT32(600, store.getMinWateringIntervalS());
    TEST_ASSERT_FALSE(store.getWateringEnabled());
    TEST_ASSERT_EQUAL_UINT32(2000, store.getSensorReadIntervalMs());
    TEST_ASSERT_EQUAL_UINT32(120000, store.getDataLogIntervalMs());
    TEST_ASSERT_EQUAL_STRING("greenhouse", store.getWifiSsid().c_str());
    TEST_ASSERT_EQUAL_STRING("round-trip-pw",
                             store.getWifiPassword().c_str());
}

}  // namespace

// ---------------------------------------------------------------------------
// T009 — erased NVS: every item reads its factory default (FR-001/FR-002)
// ---------------------------------------------------------------------------
static void test_defaults_on_erased_nvs(void)
{
    resetNvs();
    NvsConfigStore store;
    assertAllDefaults(store);
}

// ---------------------------------------------------------------------------
// T010 — set/get round-trip per item (FR-003)
// ---------------------------------------------------------------------------
static void test_round_trip_all_items(void)
{
    resetNvs();
    NvsConfigStore store;
    setAllNonDefaults(store);
    assertAllNonDefaults(store);
}

// ---------------------------------------------------------------------------
// T010 — values survive an NVS deinit/init cycle (simulated restart) and
// are visible to a fresh store instance (US1 scenario 2)
// ---------------------------------------------------------------------------
static void test_persistence_across_reinit(void)
{
    resetNvs();
    {
        NvsConfigStore store;
        setAllNonDefaults(store);
    }

    TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_deinit());
    TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_init());

    NvsConfigStore store;
    assertAllNonDefaults(store);
}

// ---------------------------------------------------------------------------
// T011 — out-of-range writes rejected at every documented bound (FR-003)
// ---------------------------------------------------------------------------
static void test_set_rejects_out_of_range(void)
{
    resetNvs();
    NvsConfigStore store;

    // Moisture thresholds: 0–100 %, NaN never accepted.
    TEST_ASSERT_FALSE(store.setMoistureThresholdLow(-0.5f));
    TEST_ASSERT_FALSE(store.setMoistureThresholdLow(100.5f));
    TEST_ASSERT_FALSE(store.setMoistureThresholdLow(NAN));
    TEST_ASSERT_FALSE(store.setMoistureThresholdHigh(-0.5f));
    TEST_ASSERT_FALSE(store.setMoistureThresholdHigh(100.5f));
    TEST_ASSERT_FALSE(store.setMoistureThresholdHigh(NAN));

    // Watering duration: 1–300 s.
    TEST_ASSERT_FALSE(store.setWateringDurationS(0));
    TEST_ASSERT_FALSE(store.setWateringDurationS(301));

    // Soak pause: >= 1 s.
    TEST_ASSERT_FALSE(store.setMinWateringIntervalS(0));

    // Sensor read interval: >= 1000 ms.
    TEST_ASSERT_FALSE(store.setSensorReadIntervalMs(999));

    // Data log interval: >= 60000 ms.
    TEST_ASSERT_FALSE(store.setDataLogIntervalMs(59999));

    // Credentials: SSID <= 32 bytes, password <= 64 bytes.
    const std::string longSsid(33, 's');
    const std::string longPass(65, 'p');
    TEST_ASSERT_FALSE(store.setWifiCredentials(longSsid, "ok"));
    TEST_ASSERT_FALSE(store.setWifiCredentials("ok", longPass));

    // Nothing was stored: every item still reads its default.
    assertAllDefaults(store);
}

// ---------------------------------------------------------------------------
// T011 — boundary values are in range and accepted
// ---------------------------------------------------------------------------
static void test_boundary_values_accepted(void)
{
    resetNvs();
    NvsConfigStore store;

    TEST_ASSERT_TRUE(store.setMoistureThresholdLow(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, store.getMoistureThresholdLow());
    TEST_ASSERT_TRUE(store.setMoistureThresholdHigh(100.0f));
    TEST_ASSERT_EQUAL_FLOAT(100.0f, store.getMoistureThresholdHigh());

    TEST_ASSERT_TRUE(store.setWateringDurationS(1));
    TEST_ASSERT_EQUAL_UINT32(1, store.getWateringDurationS());
    TEST_ASSERT_TRUE(store.setWateringDurationS(300));
    TEST_ASSERT_EQUAL_UINT32(300, store.getWateringDurationS());

    TEST_ASSERT_TRUE(store.setMinWateringIntervalS(1));
    TEST_ASSERT_EQUAL_UINT32(1, store.getMinWateringIntervalS());

    TEST_ASSERT_TRUE(store.setSensorReadIntervalMs(1000));
    TEST_ASSERT_EQUAL_UINT32(1000, store.getSensorReadIntervalMs());

    TEST_ASSERT_TRUE(store.setDataLogIntervalMs(60000));
    TEST_ASSERT_EQUAL_UINT32(60000, store.getDataLogIntervalMs());

    // Exact-length credentials are accepted.
    const std::string maxSsid(32, 's');
    const std::string maxPass(64, 'p');
    TEST_ASSERT_TRUE(store.setWifiCredentials(maxSsid, maxPass));
    TEST_ASSERT_EQUAL_STRING(maxSsid.c_str(), store.getWifiSsid().c_str());
    TEST_ASSERT_EQUAL_STRING(maxPass.c_str(),
                             store.getWifiPassword().c_str());
}

// ---------------------------------------------------------------------------
// T011 — a rejected write leaves the previously stored value untouched
// ---------------------------------------------------------------------------
static void test_rejected_write_leaves_stored_value(void)
{
    resetNvs();
    NvsConfigStore store;

    TEST_ASSERT_TRUE(store.setMoistureThresholdLow(40.0f));
    TEST_ASSERT_FALSE(store.setMoistureThresholdLow(150.0f));
    TEST_ASSERT_EQUAL_FLOAT(40.0f, store.getMoistureThresholdLow());

    TEST_ASSERT_TRUE(store.setWateringDurationS(60));
    TEST_ASSERT_FALSE(store.setWateringDurationS(0));
    TEST_ASSERT_EQUAL_UINT32(60, store.getWateringDurationS());

    TEST_ASSERT_TRUE(store.setWifiCredentials("kept-ssid", "kept-pass"));
    TEST_ASSERT_FALSE(store.setWifiCredentials(std::string(33, 'x'), "new"));
    TEST_ASSERT_EQUAL_STRING("kept-ssid", store.getWifiSsid().c_str());
    TEST_ASSERT_EQUAL_STRING("kept-pass", store.getWifiPassword().c_str());
}

// ---------------------------------------------------------------------------
// T011 — out-of-range STORED values are shadowed by the defaults
// (US1 scenario 3, FR-002); a later valid write replaces the invalid entry
// ---------------------------------------------------------------------------
static void test_out_of_range_stored_values_shadowed(void)
{
    resetNvs();

    rawSetU32("moist_low", floatBits(150.0f));   // > 100 %
    rawSetU32("moist_high", floatBits(-1.0f));   // < 0 %
    rawSetU32("water_dur", 301);                 // > 300 s
    rawSetU32("soak_pause", 0);                  // < 1 s
    rawSetU8("water_en", 7);                     // neither 0 nor 1
    rawSetU32("read_iv", 999);                   // < 1000 ms
    rawSetU32("log_iv", 59999);                  // < 60000 ms
    rawSetStr("wifi_ssid", std::string(33, 'a'));  // > 32 bytes

    NvsConfigStore store;
    TEST_ASSERT_EQUAL_FLOAT(IConfigStore::kDefaultMoistureThresholdLow,
                            store.getMoistureThresholdLow());
    TEST_ASSERT_EQUAL_FLOAT(IConfigStore::kDefaultMoistureThresholdHigh,
                            store.getMoistureThresholdHigh());
    TEST_ASSERT_EQUAL_UINT32(IConfigStore::kDefaultWateringDurationS,
                             store.getWateringDurationS());
    TEST_ASSERT_EQUAL_UINT32(IConfigStore::kDefaultMinWateringIntervalS,
                             store.getMinWateringIntervalS());
    TEST_ASSERT_EQUAL(IConfigStore::kDefaultWateringEnabled,
                      store.getWateringEnabled());
    TEST_ASSERT_EQUAL_UINT32(IConfigStore::kDefaultSensorReadIntervalMs,
                             store.getSensorReadIntervalMs());
    TEST_ASSERT_EQUAL_UINT32(IConfigStore::kDefaultDataLogIntervalMs,
                             store.getDataLogIntervalMs());
    TEST_ASSERT_EQUAL_STRING("", store.getWifiSsid().c_str());

    // A NaN bit pattern is also shadowed, never returned.
    rawSetU32("moist_low", floatBits(NAN));
    TEST_ASSERT_EQUAL_FLOAT(IConfigStore::kDefaultMoistureThresholdLow,
                            store.getMoistureThresholdLow());

    // The invalid entry stays in place until the next VALID write replaces
    // it (data-model rule).
    TEST_ASSERT_TRUE(store.setMoistureThresholdLow(42.0f));
    TEST_ASSERT_EQUAL_FLOAT(42.0f, store.getMoistureThresholdLow());
}

// ---------------------------------------------------------------------------
// T012 — factory reset restores every default and removes credentials
// (FR-005, SC-003)
// ---------------------------------------------------------------------------
static void test_factory_reset_restores_defaults(void)
{
    resetNvs();
    NvsConfigStore store;
    setAllNonDefaults(store);

    TEST_ASSERT_TRUE(store.factoryReset());

    // The same instance stays usable; everything reads defaults again.
    assertAllDefaults(store);

    // The store accepts new writes after the reset.
    TEST_ASSERT_TRUE(store.setWateringDurationS(30));
    TEST_ASSERT_EQUAL_UINT32(30, store.getWateringDurationS());
}

// ---------------------------------------------------------------------------
// T012 — credential set/clear semantics (FR-004 storage side)
// ---------------------------------------------------------------------------
static void test_credential_set_and_clear(void)
{
    resetNvs();
    NvsConfigStore store;

    // Factory state: unconfigured = empty strings.
    TEST_ASSERT_EQUAL_STRING("", store.getWifiSsid().c_str());
    TEST_ASSERT_EQUAL_STRING("", store.getWifiPassword().c_str());

    TEST_ASSERT_TRUE(store.setWifiCredentials("my-network", "my-password"));
    TEST_ASSERT_EQUAL_STRING("my-network", store.getWifiSsid().c_str());
    TEST_ASSERT_EQUAL_STRING("my-password", store.getWifiPassword().c_str());

    TEST_ASSERT_TRUE(store.clearWifiCredentials());
    TEST_ASSERT_EQUAL_STRING("", store.getWifiSsid().c_str());
    TEST_ASSERT_EQUAL_STRING("", store.getWifiPassword().c_str());

    // Clearing the factory state is a successful no-op.
    TEST_ASSERT_TRUE(store.clearWifiCredentials());
}

// ---------------------------------------------------------------------------
// T012 — credential values never appear in log/diagnostic output (FR-004).
// stdout+stderr are redirected to a file around the credential operations;
// the captured bytes must not contain the secret values.
// ---------------------------------------------------------------------------
static void test_credentials_never_logged(void)
{
    resetNvs();
    NvsConfigStore store;

    char path[] = "/tmp/ws_log_capture_XXXXXX";
    const int captureFd = mkstemp(path);
    TEST_ASSERT_TRUE(captureFd >= 0);

    fflush(stdout);
    fflush(stderr);
    const int savedOut = dup(STDOUT_FILENO);
    const int savedErr = dup(STDERR_FILENO);
    TEST_ASSERT_TRUE(savedOut >= 0 && savedErr >= 0);
    dup2(captureFd, STDOUT_FILENO);
    dup2(captureFd, STDERR_FILENO);

    const std::string ssid = "SECRET-SSID-FOR-LOG-TEST";
    const std::string password = "secret-password-for-log-test";
    const bool setOk = store.setWifiCredentials(ssid, password);
    const std::string readSsid = store.getWifiSsid();
    const std::string readPass = store.getWifiPassword();
    // An over-long rejection must not log the input either.
    const bool rejected = store.setWifiCredentials(std::string(33, 'Z'),
                                                   password);
    const bool clearOk = store.clearWifiCredentials();

    fflush(stdout);
    fflush(stderr);
    dup2(savedOut, STDOUT_FILENO);
    dup2(savedErr, STDERR_FILENO);
    close(savedOut);
    close(savedErr);

    // Assertions only after stdout is restored (Unity output must be seen).
    TEST_ASSERT_TRUE(setOk);
    TEST_ASSERT_EQUAL_STRING(ssid.c_str(), readSsid.c_str());
    TEST_ASSERT_EQUAL_STRING(password.c_str(), readPass.c_str());
    TEST_ASSERT_FALSE(rejected);
    TEST_ASSERT_TRUE(clearOk);

    std::string captured;
    TEST_ASSERT_TRUE(lseek(captureFd, 0, SEEK_SET) == 0);
    char buf[256];
    ssize_t n = 0;
    while ((n = read(captureFd, buf, sizeof(buf))) > 0) {
        captured.append(buf, static_cast<size_t>(n));
    }
    close(captureFd);
    unlink(path);

    TEST_ASSERT_TRUE(captured.find(ssid) == std::string::npos);
    TEST_ASSERT_TRUE(captured.find(password) == std::string::npos);
    TEST_ASSERT_TRUE(captured.find("ZZZZ") == std::string::npos);
}

// ---------------------------------------------------------------------------
// T012 — MockConfigStore holds the same contract invariants (FR-012)
// ---------------------------------------------------------------------------
static void test_mock_defaults_roundtrip_rejection(void)
{
    MockConfigStore store;

    // Fresh mock = factory state.
    assertAllDefaults(store);

    // Round-trip.
    setAllNonDefaults(store);
    assertAllNonDefaults(store);

    // Every documented bound rejects; stored values stay untouched.
    TEST_ASSERT_FALSE(store.setMoistureThresholdLow(-0.5f));
    TEST_ASSERT_FALSE(store.setMoistureThresholdLow(100.5f));
    TEST_ASSERT_FALSE(store.setMoistureThresholdLow(NAN));
    TEST_ASSERT_FALSE(store.setMoistureThresholdHigh(100.5f));
    TEST_ASSERT_FALSE(store.setWateringDurationS(0));
    TEST_ASSERT_FALSE(store.setWateringDurationS(301));
    TEST_ASSERT_FALSE(store.setMinWateringIntervalS(0));
    TEST_ASSERT_FALSE(store.setSensorReadIntervalMs(999));
    TEST_ASSERT_FALSE(store.setDataLogIntervalMs(59999));
    TEST_ASSERT_FALSE(store.setWifiCredentials(std::string(33, 's'), "ok"));
    TEST_ASSERT_FALSE(store.setWifiCredentials("ok", std::string(65, 'p')));
    assertAllNonDefaults(store);

    // Instrumentation counted the rejections.
    TEST_ASSERT_EQUAL(11, store.rejectedWrites);
}

static void test_mock_shadowing_factory_reset_and_fail_writes(void)
{
    MockConfigStore store;

    // Injected out-of-range stored values are shadowed by defaults
    // (same invariant as the NVS store).
    store.stored.moistureThresholdLow = 150.0f;
    store.stored.wateringDurationS = 301;
    store.stored.wateringEnabled = 7;
    store.stored.sensorReadIntervalMs = 999;
    store.stored.wifiSsid = std::string(33, 'a');
    TEST_ASSERT_EQUAL_FLOAT(IConfigStore::kDefaultMoistureThresholdLow,
                            store.getMoistureThresholdLow());
    TEST_ASSERT_EQUAL_UINT32(IConfigStore::kDefaultWateringDurationS,
                             store.getWateringDurationS());
    TEST_ASSERT_EQUAL(IConfigStore::kDefaultWateringEnabled,
                      store.getWateringEnabled());
    TEST_ASSERT_EQUAL_UINT32(IConfigStore::kDefaultSensorReadIntervalMs,
                             store.getSensorReadIntervalMs());
    TEST_ASSERT_EQUAL_STRING("", store.getWifiSsid().c_str());

    // Factory reset restores the factory state.
    setAllNonDefaults(store);
    TEST_ASSERT_TRUE(store.factoryReset());
    TEST_ASSERT_EQUAL(1, store.factoryResets);
    assertAllDefaults(store);

    // Simulated persistence failure: write rejected, state untouched.
    TEST_ASSERT_TRUE(store.setWateringDurationS(45));
    store.failWrites = true;
    TEST_ASSERT_FALSE(store.setWateringDurationS(60));
    TEST_ASSERT_FALSE(store.setWateringEnabled(false));
    TEST_ASSERT_FALSE(store.setWifiCredentials("ssid", "pass"));
    TEST_ASSERT_FALSE(store.clearWifiCredentials());
    TEST_ASSERT_FALSE(store.factoryReset());
    store.failWrites = false;
    TEST_ASSERT_EQUAL_UINT32(45, store.getWateringDurationS());
}

void run_config_store_tests(void)
{
    RUN_TEST(test_defaults_on_erased_nvs);
    RUN_TEST(test_round_trip_all_items);
    RUN_TEST(test_persistence_across_reinit);
    RUN_TEST(test_set_rejects_out_of_range);
    RUN_TEST(test_boundary_values_accepted);
    RUN_TEST(test_rejected_write_leaves_stored_value);
    RUN_TEST(test_out_of_range_stored_values_shadowed);
    RUN_TEST(test_factory_reset_restores_defaults);
    RUN_TEST(test_credential_set_and_clear);
    RUN_TEST(test_credentials_never_logged);
    RUN_TEST(test_mock_defaults_roundtrip_rejection);
    RUN_TEST(test_mock_shadowing_factory_reset_and_fail_writes);
}
