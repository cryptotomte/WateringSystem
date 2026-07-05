// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_event_logger.cpp
 * @brief Host suite for the EventLogger producers (feature 008 US2).
 *
 * Registered by test_main.cpp via run_event_logger_tests(). Drives the pure
 * EventLogger over MockDataStorage + FakeWallClock (no IDF, no real clock).
 * Each producer must write exactly ONE event carrying the expected category,
 * the exact deterministic detail string (contracts/event-logger.md) and the
 * FakeWallClock epoch; a failing store increments droppedEvents() without
 * crashing; resetReasonName() maps the ESP_RST_* integer values.
 *
 * Coverage maps to specs/008-sntp-watchdog-logging/contracts/event-logger.md.
 */

#include <cstdint>
#include <vector>

#include "unity.h"

#include "events/EventLogger.h"
#include "interfaces/IDataStorage.h"
#include "storage/testing/MockDataStorage.h"
#include "time/testing/FakeWallClock.h"

namespace {

/// A fixed, plausible epoch (2021-01-01T00:00:00Z) so "is set" is irrelevant —
/// the logger stamps every event with clock.nowEpoch() regardless.
constexpr uint32_t kFixedEpoch = 1609459200u;

void test_log_reset_writes_one_reset_event(void)
{
    MockDataStorage store;
    FakeWallClock clock(kFixedEpoch);
    EventLogger logger(store, clock);

    logger.logReset(6 /* ESP_RST_TASK_WDT */);

    TEST_ASSERT_EQUAL_size_t(1u, store.events.size());
    TEST_ASSERT_EQUAL_UINT32(kFixedEpoch, store.events[0].epoch);
    TEST_ASSERT_EQUAL_UINT8(IDataStorage::kCategoryReset,
                            store.events[0].category);
    TEST_ASSERT_EQUAL_STRING("reset=TASK_WDT", store.events[0].detail.c_str());
    TEST_ASSERT_EQUAL_UINT32(0u, logger.droppedEvents());
}

void test_log_wifi_writes_one_connectivity_event(void)
{
    MockDataStorage store;
    FakeWallClock clock(kFixedEpoch);
    EventLogger logger(store, clock);

    logger.logWifi("Connected");

    TEST_ASSERT_EQUAL_size_t(1u, store.events.size());
    TEST_ASSERT_EQUAL_UINT32(kFixedEpoch, store.events[0].epoch);
    TEST_ASSERT_EQUAL_UINT8(IDataStorage::kCategoryConnectivity,
                            store.events[0].category);
    TEST_ASSERT_EQUAL_STRING("wifi=Connected", store.events[0].detail.c_str());
}

void test_log_pump_start_writes_one_pump_event(void)
{
    MockDataStorage store;
    FakeWallClock clock(kFixedEpoch);
    EventLogger logger(store, clock);

    logger.logPumpStart("plant", "unknown");

    TEST_ASSERT_EQUAL_size_t(1u, store.events.size());
    TEST_ASSERT_EQUAL_UINT32(kFixedEpoch, store.events[0].epoch);
    TEST_ASSERT_EQUAL_UINT8(IDataStorage::kCategoryPump,
                            store.events[0].category);
    TEST_ASSERT_EQUAL_STRING("pump=plant start cause=unknown",
                             store.events[0].detail.c_str());
}

void test_log_pump_stop_writes_one_pump_event(void)
{
    MockDataStorage store;
    FakeWallClock clock(kFixedEpoch);
    EventLogger logger(store, clock);

    logger.logPumpStop("reservoir", "unknown");

    TEST_ASSERT_EQUAL_size_t(1u, store.events.size());
    TEST_ASSERT_EQUAL_UINT32(kFixedEpoch, store.events[0].epoch);
    TEST_ASSERT_EQUAL_UINT8(IDataStorage::kCategoryPump,
                            store.events[0].category);
    TEST_ASSERT_EQUAL_STRING("pump=reservoir stop cause=unknown",
                             store.events[0].detail.c_str());
}

void test_log_failsafe_writes_one_failsafe_event(void)
{
    MockDataStorage store;
    FakeWallClock clock(kFixedEpoch);
    EventLogger logger(store, clock);

    // Detail is passed verbatim (producer is PR-11); no reformatting.
    logger.logFailsafe("failsafe=soil-invalid pump=plant");

    TEST_ASSERT_EQUAL_size_t(1u, store.events.size());
    TEST_ASSERT_EQUAL_UINT32(kFixedEpoch, store.events[0].epoch);
    TEST_ASSERT_EQUAL_UINT8(IDataStorage::kCategoryFailsafe,
                            store.events[0].category);
    TEST_ASSERT_EQUAL_STRING("failsafe=soil-invalid pump=plant",
                             store.events[0].detail.c_str());
    TEST_ASSERT_EQUAL_UINT32(0u, logger.droppedEvents());
}

void test_log_ota_writes_one_ota_event(void)
{
    MockDataStorage store;
    FakeWallClock clock(kFixedEpoch);
    EventLogger logger(store, clock);

    // Detail is passed verbatim (producer is PR-13); no reformatting.
    logger.logOta("ota=begin");

    TEST_ASSERT_EQUAL_size_t(1u, store.events.size());
    TEST_ASSERT_EQUAL_UINT32(kFixedEpoch, store.events[0].epoch);
    TEST_ASSERT_EQUAL_UINT8(IDataStorage::kCategoryOta,
                            store.events[0].category);
    TEST_ASSERT_EQUAL_STRING("ota=begin", store.events[0].detail.c_str());
    TEST_ASSERT_EQUAL_UINT32(0u, logger.droppedEvents());
}

void test_write_failure_increments_dropped_and_never_crashes(void)
{
    MockDataStorage store;
    FakeWallClock clock(kFixedEpoch);
    EventLogger logger(store, clock);

    store.failWrites = true;

    // ALL SIX producer paths are exercised; none may throw and none may store.
    logger.logReset(1);
    logger.logWifi("Reconnecting");
    logger.logPumpStart("plant", "unknown");
    logger.logPumpStop("plant", "unknown");
    logger.logFailsafe("failsafe=soil-invalid pump=plant");
    logger.logOta("ota=begin");

    TEST_ASSERT_EQUAL_size_t(0u, store.events.size());
    TEST_ASSERT_EQUAL_UINT32(6u, logger.droppedEvents());
}

void test_multi_event_stamps_at_call_time(void)
{
    // Proves emit() stamps with clock.nowEpoch() at CALL time, not at logger
    // construction: two events logged around a clock step carry each step's
    // epoch, and getEvents() returns them newest-first.
    constexpr uint32_t kE = kFixedEpoch;
    MockDataStorage store;
    FakeWallClock clock(kE);
    EventLogger logger(store, clock);

    logger.logWifi("Connecting");
    clock.setEpoch(kE + 3600u);
    logger.logWifi("Connected");

    TEST_ASSERT_EQUAL_size_t(2u, store.events.size());
    TEST_ASSERT_EQUAL_UINT32(kE, store.events[0].epoch);
    TEST_ASSERT_EQUAL_UINT32(kE + 3600u, store.events[1].epoch);

    // Retrieval is newest-first (monotonic epochs).
    std::vector<EventRecord> recent = store.getEvents(10);
    TEST_ASSERT_EQUAL_size_t(2u, recent.size());
    TEST_ASSERT_EQUAL_UINT32(kE + 3600u, recent[0].epoch);
    TEST_ASSERT_EQUAL_STRING("wifi=Connected", recent[0].detail.c_str());
    TEST_ASSERT_EQUAL_UINT32(kE, recent[1].epoch);
    TEST_ASSERT_EQUAL_STRING("wifi=Connecting", recent[1].detail.c_str());
}

void test_reset_reason_name_mapping(void)
{
    // Total over the ESP_RST_* integer values (esp_system.h, IDF v6: 0..15);
    // any other value maps to "UNKNOWN".
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", resetReasonName(0));     // ESP_RST_UNKNOWN
    TEST_ASSERT_EQUAL_STRING("POWERON", resetReasonName(1));     // ESP_RST_POWERON
    TEST_ASSERT_EQUAL_STRING("EXT", resetReasonName(2));         // ESP_RST_EXT
    TEST_ASSERT_EQUAL_STRING("SW", resetReasonName(3));          // ESP_RST_SW
    TEST_ASSERT_EQUAL_STRING("PANIC", resetReasonName(4));       // ESP_RST_PANIC
    TEST_ASSERT_EQUAL_STRING("INT_WDT", resetReasonName(5));     // ESP_RST_INT_WDT
    TEST_ASSERT_EQUAL_STRING("TASK_WDT", resetReasonName(6));    // ESP_RST_TASK_WDT
    TEST_ASSERT_EQUAL_STRING("WDT", resetReasonName(7));         // ESP_RST_WDT
    TEST_ASSERT_EQUAL_STRING("DEEPSLEEP", resetReasonName(8));   // ESP_RST_DEEPSLEEP
    TEST_ASSERT_EQUAL_STRING("BROWNOUT", resetReasonName(9));    // ESP_RST_BROWNOUT
    TEST_ASSERT_EQUAL_STRING("SDIO", resetReasonName(10));       // ESP_RST_SDIO
    TEST_ASSERT_EQUAL_STRING("USB", resetReasonName(11));        // ESP_RST_USB
    TEST_ASSERT_EQUAL_STRING("JTAG", resetReasonName(12));       // ESP_RST_JTAG
    TEST_ASSERT_EQUAL_STRING("EFUSE", resetReasonName(13));      // ESP_RST_EFUSE
    TEST_ASSERT_EQUAL_STRING("PWR_GLITCH", resetReasonName(14)); // ESP_RST_PWR_GLITCH
    TEST_ASSERT_EQUAL_STRING("CPU_LOCKUP", resetReasonName(15)); // ESP_RST_CPU_LOCKUP
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", resetReasonName(999));   // out of range
}

}  // namespace

void run_event_logger_tests(void)
{
    RUN_TEST(test_log_reset_writes_one_reset_event);
    RUN_TEST(test_log_wifi_writes_one_connectivity_event);
    RUN_TEST(test_log_pump_start_writes_one_pump_event);
    RUN_TEST(test_log_pump_stop_writes_one_pump_event);
    RUN_TEST(test_log_failsafe_writes_one_failsafe_event);
    RUN_TEST(test_log_ota_writes_one_ota_event);
    RUN_TEST(test_write_failure_increments_dropped_and_never_crashes);
    RUN_TEST(test_multi_event_stamps_at_call_time);
    RUN_TEST(test_reset_reason_name_mapping);
}
