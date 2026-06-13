// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_water_pump.cpp
 * @brief Host tests for the WaterPump state machine (linux preview target).
 *
 * Tests the REAL enforcement logic (WaterPump base class) via
 * MockWaterPump (records applyOutput transitions) and FakeTimeProvider
 * (manual clock). Registered via run_water_pump_tests() from the shared
 * Unity runner (test_main.cpp); the process exit code equals the failure
 * count and is the CI gate.
 *
 * Coverage maps to the invariants in
 * specs/002-pump-gpio-board/contracts/iwaterpump.md.
 */

#include <vector>

#include "unity.h"

#include "actuators/LockedWaterPump.h"
#include "actuators/testing/FakeTimeProvider.h"
#include "actuators/testing/MockWaterPump.h"

namespace {

constexpr int64_t kMaxRunTimeMs = WaterPump::kDefaultMaxRunTimeMs;  // 300 000

/// Fresh pump + clock per test; initialize() is part of the fixture.
struct Fixture {
    FakeTimeProvider clock;
    MockWaterPump pump{"plant", clock};

    Fixture() { TEST_ASSERT_TRUE(pump.initialize()); }
};

}  // namespace

// --------------------------------------------------------------------------
// Duration self-stop at the exact boundary (SC-003)
// --------------------------------------------------------------------------
static void test_duration_self_stop_at_exact_boundary(void)
{
    Fixture f;
    TEST_ASSERT_TRUE(f.pump.runFor(10));
    TEST_ASSERT_TRUE(f.pump.isRunning());

    // One millisecond before the boundary: still running.
    f.clock.advance(9999);
    f.pump.update();
    TEST_ASSERT_TRUE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT64(9999, f.pump.getCurrentRunTimeMs());

    // Exactly at the boundary: stopped with DurationElapsed.
    f.clock.advance(1);
    f.pump.update();
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL(static_cast<int>(StopReason::DurationElapsed),
                      static_cast<int>(f.pump.getLastStopReason()));
    TEST_ASSERT_EQUAL_INT64(0, f.pump.getCurrentRunTimeMs());
}

// --------------------------------------------------------------------------
// Max-runtime forced stop (invariant 3, HIL item 4)
// --------------------------------------------------------------------------
static void test_max_runtime_forced_stop(void)
{
    Fixture f;
    // 300 s is the maximum accepted duration; at the 300 s boundary the
    // max-runtime cap wins and the stop is reported as forced.
    TEST_ASSERT_TRUE(f.pump.runFor(300));

    f.clock.advance(kMaxRunTimeMs - 1);
    f.pump.update();
    TEST_ASSERT_TRUE(f.pump.isRunning());

    f.clock.advance(1);
    f.pump.update();
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL(static_cast<int>(StopReason::MaxRuntimeForced),
                      static_cast<int>(f.pump.getLastStopReason()));
}

// --------------------------------------------------------------------------
// Rejected starts cause no output change and no state change (invariant 4)
// --------------------------------------------------------------------------
static void test_reject_zero_duration(void)
{
    Fixture f;
    const size_t callsBefore = f.pump.outputCalls.size();

    TEST_ASSERT_FALSE(f.pump.runFor(0));

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL(callsBefore, f.pump.outputCalls.size());
    TEST_ASSERT_EQUAL(static_cast<int>(StopReason::None),
                      static_cast<int>(f.pump.getLastStopReason()));
}

static void test_reject_duration_over_max(void)
{
    Fixture f;
    const size_t callsBefore = f.pump.outputCalls.size();

    TEST_ASSERT_FALSE(f.pump.runFor(301));  // no silent clamping

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL(callsBefore, f.pump.outputCalls.size());
}

static void test_reject_start_while_running(void)
{
    Fixture f;
    TEST_ASSERT_TRUE(f.pump.runFor(10));
    const size_t callsBefore = f.pump.outputCalls.size();

    f.clock.advance(4000);
    TEST_ASSERT_FALSE(f.pump.runFor(5));

    // No output change, and the running clock was NOT restarted.
    TEST_ASSERT_EQUAL(callsBefore, f.pump.outputCalls.size());
    TEST_ASSERT_TRUE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT64(4000, f.pump.getCurrentRunTimeMs());

    // The original schedule still applies: self-stop at 10 s, not 4+5 s.
    f.clock.advance(6000);
    f.pump.update();
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL(static_cast<int>(StopReason::DurationElapsed),
                      static_cast<int>(f.pump.getLastStopReason()));
}

// --------------------------------------------------------------------------
// Stop when stopped is a successful no-op
// --------------------------------------------------------------------------
static void test_stop_when_stopped_noop(void)
{
    Fixture f;
    const size_t callsBefore = f.pump.outputCalls.size();

    TEST_ASSERT_TRUE(f.pump.stop());

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL(callsBefore, f.pump.outputCalls.size());
    TEST_ASSERT_EQUAL(static_cast<int>(StopReason::None),
                      static_cast<int>(f.pump.getLastStopReason()));
}

// --------------------------------------------------------------------------
// Paired output transitions across a full cycle (invariant 1 + 2)
// --------------------------------------------------------------------------
static void test_paired_output_transitions_full_cycle(void)
{
    Fixture f;
    TEST_ASSERT_TRUE(f.pump.runFor(10));
    f.clock.advance(3000);
    TEST_ASSERT_TRUE(f.pump.stop());
    TEST_ASSERT_EQUAL(static_cast<int>(StopReason::Commanded),
                      static_cast<int>(f.pump.getLastStopReason()));

    // initialize() OFF first, then exactly one ON and exactly one OFF.
    const std::vector<bool> expected = {false, true, false};
    TEST_ASSERT_TRUE(f.pump.outputCalls == expected);
}

// --------------------------------------------------------------------------
// Accumulated runtime across two runs
// --------------------------------------------------------------------------
static void test_accumulated_runtime_across_runs(void)
{
    Fixture f;

    // Run 1: full 10 s timed run (self-stop).
    TEST_ASSERT_TRUE(f.pump.runFor(10));
    f.clock.advance(10'000);
    f.pump.update();
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT64(10'000, f.pump.getAccumulatedRunTimeMs());

    // Run 2: 3 s of a 5 s run, then commanded stop.
    f.clock.advance(60'000);  // idle time must not count
    TEST_ASSERT_TRUE(f.pump.runFor(5));
    f.clock.advance(3000);
    TEST_ASSERT_TRUE(f.pump.stop());

    TEST_ASSERT_EQUAL_INT64(13'000, f.pump.getAccumulatedRunTimeMs());
}

// --------------------------------------------------------------------------
// Enforcement happens within a single update() poll, however late it comes
// --------------------------------------------------------------------------
static void test_enforcement_within_one_poll(void)
{
    Fixture f;
    TEST_ASSERT_TRUE(f.pump.runFor(300));

    // Polling stalls far past the cap; the very next update() must stop the
    // pump (forced) — no second poll needed.
    f.clock.advance(kMaxRunTimeMs + 100'000);
    f.pump.update();

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL(static_cast<int>(StopReason::MaxRuntimeForced),
                      static_cast<int>(f.pump.getLastStopReason()));

    // Output ended OFF with exactly paired transitions.
    const std::vector<bool> expected = {false, true, false};
    TEST_ASSERT_TRUE(f.pump.outputCalls == expected);
}

// --------------------------------------------------------------------------
// LockedWaterPump decorator delegates the full contract path unchanged
// (the wrapper adds task-level mutex serialization; see LockedWaterPump.h)
// --------------------------------------------------------------------------
static void test_locked_wrapper_delegates_full_cycle(void)
{
    FakeTimeProvider clock;
    MockWaterPump inner("plant", clock);
    LockedWaterPump pump(inner);

    TEST_ASSERT_TRUE(pump.initialize());
    TEST_ASSERT_TRUE(pump.isAvailable());
    TEST_ASSERT_EQUAL_STRING("plant", pump.getName().c_str());
    TEST_ASSERT_EQUAL(0, pump.getLastError());

    // Contract checks behave identically through the wrapper.
    TEST_ASSERT_FALSE(pump.runFor(0));
    TEST_ASSERT_TRUE(pump.runFor(10));
    TEST_ASSERT_FALSE(pump.runFor(5));  // already running -> rejected
    TEST_ASSERT_TRUE(pump.isRunning());

    clock.advance(4000);
    pump.update();
    TEST_ASSERT_TRUE(pump.isRunning());
    TEST_ASSERT_EQUAL_INT64(4000, pump.getCurrentRunTimeMs());

    TEST_ASSERT_TRUE(pump.stop());
    TEST_ASSERT_FALSE(pump.isRunning());
    TEST_ASSERT_EQUAL(static_cast<int>(StopReason::Commanded),
                      static_cast<int>(pump.getLastStopReason()));
    TEST_ASSERT_EQUAL_INT64(4000, pump.getAccumulatedRunTimeMs());

    // Output transitions reached the wrapped pump exactly paired.
    const std::vector<bool> expected = {false, true, false};
    TEST_ASSERT_TRUE(inner.outputCalls == expected);
}

void run_water_pump_tests(void)
{
    RUN_TEST(test_duration_self_stop_at_exact_boundary);
    RUN_TEST(test_max_runtime_forced_stop);
    RUN_TEST(test_reject_zero_duration);
    RUN_TEST(test_reject_duration_over_max);
    RUN_TEST(test_reject_start_while_running);
    RUN_TEST(test_stop_when_stopped_noop);
    RUN_TEST(test_paired_output_transitions_full_cycle);
    RUN_TEST(test_accumulated_runtime_across_runs);
    RUN_TEST(test_enforcement_within_one_poll);
    RUN_TEST(test_locked_wrapper_delegates_full_cycle);
}
