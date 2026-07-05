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

void test_write_failure_increments_dropped_and_never_crashes(void)
{
    MockDataStorage store;
    FakeWallClock clock(kFixedEpoch);
    EventLogger logger(store, clock);

    store.failWrites = true;

    // Every producer path is exercised; none may throw and none may store.
    logger.logReset(1);
    logger.logWifi("Reconnecting");
    logger.logPumpStart("plant", "unknown");
    logger.logPumpStop("plant", "unknown");

    TEST_ASSERT_EQUAL_size_t(0u, store.events.size());
    TEST_ASSERT_EQUAL_UINT32(4u, logger.droppedEvents());
}

void test_reset_reason_name_mapping(void)
{
    TEST_ASSERT_EQUAL_STRING("POWERON", resetReasonName(1));   // ESP_RST_POWERON
    TEST_ASSERT_EQUAL_STRING("PANIC", resetReasonName(4));     // ESP_RST_PANIC
    TEST_ASSERT_EQUAL_STRING("TASK_WDT", resetReasonName(6));  // ESP_RST_TASK_WDT
    TEST_ASSERT_EQUAL_STRING("BROWNOUT", resetReasonName(9));  // ESP_RST_BROWNOUT
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", resetReasonName(999)); // out of range
}

}  // namespace

void run_event_logger_tests(void)
{
    RUN_TEST(test_log_reset_writes_one_reset_event);
    RUN_TEST(test_log_wifi_writes_one_connectivity_event);
    RUN_TEST(test_log_pump_start_writes_one_pump_event);
    RUN_TEST(test_log_pump_stop_writes_one_pump_event);
    RUN_TEST(test_write_failure_increments_dropped_and_never_crashes);
    RUN_TEST(test_reset_reason_name_mapping);
}
