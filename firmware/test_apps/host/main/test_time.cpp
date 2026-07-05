// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_time.cpp
 * @brief Host suite for the pure time logic (feature 008, T007).
 *
 * Registered by test_main.cpp via run_time_tests(). Exercises
 * TimeService::isPlausibleEpoch() and the Swedish CET/CEST local-time
 * formatter, including the DST spring-forward boundary. The process timezone
 * is set to the Swedish rule once at suite start (mirrors
 * SntpClient::applyTimezone() on target) so the +ZZZZ offset in the formatted
 * string is deterministic.
 */

#include <cstdlib>
#include <ctime>
#include <string>

#include "unity.h"

#include "time/TimeService.h"

namespace {

/// True iff @p s ends with @p suffix.
bool ends_with(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void test_plausible_epoch_threshold(void)
{
    // 0/1970 is an un-synced boot value; the 2020-01-01Z threshold is plausible.
    TEST_ASSERT_FALSE(TimeService::isPlausibleEpoch(0));
    TEST_ASSERT_FALSE(
        TimeService::isPlausibleEpoch(TimeService::kMinPlausibleEpoch - 1));
    TEST_ASSERT_TRUE(TimeService::isPlausibleEpoch(1577836800u));
    TEST_ASSERT_TRUE(
        TimeService::isPlausibleEpoch(TimeService::kMinPlausibleEpoch));
}

void test_format_winter_and_summer_offsets(void)
{
    // 2024-01-01 00:00:00Z -> 01:00 CET (+0100).
    TEST_ASSERT_TRUE(ends_with(TimeService::formatLocal(1704067200u), "+0100"));
    // 2024-07-01 00:00:00Z -> 02:00 CEST (+0200).
    TEST_ASSERT_TRUE(ends_with(TimeService::formatLocal(1719792000u), "+0200"));
}

void test_format_dst_spring_boundary(void)
{
    // Spring-forward is the last Sunday of March (2024-03-31) at 02:00 local
    // CET == 01:00 UTC, jumping to 03:00 CEST.
    // 2024-03-31 01:00:00Z — at/after the switch -> CEST (+0200).
    TEST_ASSERT_TRUE(ends_with(TimeService::formatLocal(1711846800u), "+0200"));
    // 2024-03-31 00:00:00Z — before the switch -> CET (+0100).
    TEST_ASSERT_TRUE(ends_with(TimeService::formatLocal(1711843200u), "+0100"));
}

void test_format_dst_autumn_boundary(void)
{
    // Fall-back is the last Sunday of October (2024-10-27); the M10.5.0/3 rule
    // switches at 03:00 local CEST == 01:00 UTC, dropping to 02:00 CET.
    // 2024-10-27 00:00:00Z — before the switch -> still CEST (+0200).
    TEST_ASSERT_TRUE(ends_with(TimeService::formatLocal(1729987200u), "+0200"));
    // 2024-10-27 01:00:00Z — at the fall-back instant -> CET (+0100).
    TEST_ASSERT_TRUE(ends_with(TimeService::formatLocal(1729990800u), "+0100"));
}

}  // namespace

void run_time_tests(void)
{
    // Swedish timezone rule (mirrors SntpClient::applyTimezone()); set once so
    // localtime_r renders the correct CET/CEST offset in the assertions above.
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    RUN_TEST(test_plausible_epoch_threshold);
    RUN_TEST(test_format_winter_and_summer_offsets);
    RUN_TEST(test_format_dst_spring_boundary);
    RUN_TEST(test_format_dst_autumn_boundary);
}
