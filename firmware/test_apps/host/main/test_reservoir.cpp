// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_reservoir.cpp
 * @brief Host suite for the pure ReservoirController (feature 011, US2).
 *
 * Drives the REAL ReservoirController logic over two MockLevelSensor (the low
 * and high marks) + MockWaterPump (real WaterPump enforcement over
 * FakeTimeProvider) + MockDataStorage + FakeWallClock + EventLogger. Registered
 * via run_reservoir_tests() from the shared Unity runner (test_main.cpp); the
 * process exit code equals the failure count (CI gate).
 *
 * Coverage maps to tasks.md T009 (SC-004): all five level truth-table rows
 * (either/both invalid -> no action; wet/wet full ensure-stopped; wet/dry
 * sufficient -> no action; dry/dry -> start fill; dry/wet implausible -> no
 * action), stop-on-high-wet while running, the max-fill abort at the 300 s cap
 * (StopReason::MaxRuntimeForced), the post-abort cooldown (blocks then allows a
 * new auto fill; a normal high-wet stop does not arm it; a manual fill bypasses
 * it), the manual-fill refusal when already full, and the feature gate
 * (disabled -> pump forced off + all logic skipped).
 */

#include <cstdint>

#include "unity.h"

#include "actuators/testing/FakeTimeProvider.h"
#include "actuators/testing/MockWaterPump.h"
#include "control/ReservoirController.h"
#include "events/EventLogger.h"
#include "sensors/testing/MockLevelSensor.h"
#include "storage/testing/MockDataStorage.h"
#include "time/testing/FakeWallClock.h"

namespace {

// ---------------------------------------------------------------------------
// Fixture: fresh collaborators + controller per test. The two level marks
// start INVALID (the mock's construction state, mirroring the real sensor's
// settle/warm-up); each test scripts a coherent valid state as needed.
// ---------------------------------------------------------------------------
struct Fixture {
    FakeTimeProvider clock;
    MockLevelSensor low;
    MockLevelSensor high;
    MockWaterPump pump{"reservoir", clock};
    MockDataStorage storage;
    FakeWallClock wallClock;
    EventLogger events{storage, wallClock};
    ReservoirController controller{low, high, pump, clock, events};

    Fixture() { TEST_ASSERT_TRUE(pump.initialize()); }
};

/// Number of output ON transitions recorded by the real pump — a proxy for
/// "how many fills were started" (each successful runFor drives applyOutput
/// true exactly once; stops drive false and are not counted).
int onTransitions(const MockWaterPump& pump)
{
    int count = 0;
    for (bool on : pump.outputCalls) {
        if (on) {
            ++count;
        }
    }
    return count;
}

// ===========================================================================
// Level truth table (auto; enabled + auto-level, pump not running)
// ===========================================================================

// Row: low invalid -> no action (invalid is never treated as "water absent").
void test_invalid_low_no_action(void)
{
    Fixture f;
    f.low.scriptInvalid();
    f.high.scriptValidState(false);
    f.controller.tick(/*enabled=*/true, /*autoLevelControl=*/true);

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(0, onTransitions(f.pump));
}

// Row: high invalid -> no action.
void test_invalid_high_no_action(void)
{
    Fixture f;
    f.low.scriptValidState(false);
    f.high.scriptInvalid();
    f.controller.tick(true, true);

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(0, onTransitions(f.pump));
}

// Row: both marks invalid -> no action.
void test_both_invalid_no_action(void)
{
    Fixture f;
    f.low.scriptInvalid();
    f.high.scriptInvalid();
    f.controller.tick(true, true);

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(0, onTransitions(f.pump));
}

// Row: wet/wet = full -> ensure stopped (no fill started; pump stays off).
void test_full_wet_wet_ensures_stopped(void)
{
    Fixture f;
    f.low.scriptValidState(true);
    f.high.scriptValidState(true);
    f.controller.tick(true, true);

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(0, onTransitions(f.pump));
}

// Row: wet/dry = sufficient water -> no action.
void test_sufficient_wet_dry_no_action(void)
{
    Fixture f;
    f.low.scriptValidState(true);
    f.high.scriptValidState(false);
    f.controller.tick(true, true);

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(0, onTransitions(f.pump));
}

// Row: dry/dry = low water -> start a fill.
void test_low_dry_dry_starts_fill(void)
{
    Fixture f;
    f.low.scriptValidState(false);
    f.high.scriptValidState(false);
    f.controller.tick(true, true);

    TEST_ASSERT_TRUE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(1, onTransitions(f.pump));
}

// Row: dry/wet = physically implausible -> no action.
void test_implausible_low_dry_high_wet_no_action(void)
{
    Fixture f;
    f.low.scriptValidState(false);
    f.high.scriptValidState(true);
    f.controller.tick(true, true);

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(0, onTransitions(f.pump));
}

// ===========================================================================
// Running safety (manual + auto)
// ===========================================================================

// A fill in progress stops immediately when the high mark reads wet.
void test_stop_on_high_wet_while_running(void)
{
    Fixture f;
    f.low.scriptValidState(false);
    f.high.scriptValidState(false);
    f.controller.tick(true, true);  // dry/dry -> start fill
    TEST_ASSERT_TRUE(f.pump.isRunning());

    f.clock.advance(5000);
    f.high.scriptValidState(true);  // high mark reached
    f.controller.tick(true, true);

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StopReason::Commanded),
                          static_cast<int>(f.pump.getLastStopReason()));
}

// The fill aborts at the hard 300 s max-fill cap when the high mark never trips
// (the pump self-stops in update() with StopReason::MaxRuntimeForced).
void test_max_fill_abort_at_cap(void)
{
    Fixture f;
    f.low.scriptValidState(false);
    f.high.scriptValidState(false);
    f.controller.tick(true, true);  // start fill
    TEST_ASSERT_TRUE(f.pump.isRunning());

    f.clock.advance(299'999);
    f.controller.tick(true, true);  // 1 ms before the cap: still running
    TEST_ASSERT_TRUE(f.pump.isRunning());

    f.clock.advance(1);
    f.controller.tick(true, true);  // at the cap: aborted
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StopReason::MaxRuntimeForced),
                          static_cast<int>(f.pump.getLastStopReason()));
}

// ===========================================================================
// Post-abort cooldown (FR-012a)
// ===========================================================================

// After a max-runtime abort with the water still dry/dry, no new AUTOMATIC
// fill starts before the cooldown elapses; a fill DOES start once it does.
void test_cooldown_blocks_then_allows_auto_refill(void)
{
    Fixture f;
    f.low.scriptValidState(false);
    f.high.scriptValidState(false);
    f.controller.tick(true, true);  // start fill

    // Never reaches high: the fill aborts at the cap and arms the cooldown.
    f.clock.advance(300'000);
    f.controller.tick(true, true);
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StopReason::MaxRuntimeForced),
                          static_cast<int>(f.pump.getLastStopReason()));
    // The abort tick itself must not re-slam the pump (still one ON so far).
    TEST_ASSERT_EQUAL_INT(1, onTransitions(f.pump));

    // 1 ms before the cooldown elapses, still dry/dry: no new fill.
    f.clock.advance(ReservoirController::kReservoirRefillCooldownMs - 1);
    f.controller.tick(true, true);
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(1, onTransitions(f.pump));

    // Exactly at the cooldown: a new auto fill starts.
    f.clock.advance(1);
    f.controller.tick(true, true);
    TEST_ASSERT_TRUE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(2, onTransitions(f.pump));
}

// A normal high-wet stop does NOT arm the cooldown: a later dry/dry starts a
// fill immediately.
void test_normal_high_wet_stop_does_not_arm_cooldown(void)
{
    Fixture f;
    f.low.scriptValidState(false);
    f.high.scriptValidState(false);
    f.controller.tick(true, true);  // start fill
    TEST_ASSERT_TRUE(f.pump.isRunning());

    // High mark trips wet: running safety stops it (Commanded, not an abort).
    f.clock.advance(1000);
    f.high.scriptValidState(true);
    f.controller.tick(true, true);
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StopReason::Commanded),
                          static_cast<int>(f.pump.getLastStopReason()));

    // Water drains back to dry/dry: a new fill starts at once (no cooldown).
    f.clock.advance(1000);
    f.high.scriptValidState(false);
    f.controller.tick(true, true);
    TEST_ASSERT_TRUE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(2, onTransitions(f.pump));
}

// A manual fill bypasses the post-abort cooldown.
void test_manual_fill_bypasses_cooldown(void)
{
    Fixture f;
    f.low.scriptValidState(false);
    f.high.scriptValidState(false);
    f.controller.tick(true, true);  // start auto fill
    f.clock.advance(300'000);
    f.controller.tick(true, true);  // abort at the cap -> arm the cooldown
    TEST_ASSERT_FALSE(f.pump.isRunning());

    // Within the cooldown an auto tick refuses to refill...
    f.controller.tick(true, true);
    TEST_ASSERT_FALSE(f.pump.isRunning());

    // ...but an explicit manual fill bypasses the cooldown and starts.
    TEST_ASSERT_TRUE(f.controller.startManualFill(60));
    TEST_ASSERT_TRUE(f.pump.isRunning());
}

// ===========================================================================
// Manual fill + feature gate
// ===========================================================================

// A manual fill is refused when the reservoir is already full (high mark wet).
void test_manual_fill_refused_when_full(void)
{
    Fixture f;
    f.low.scriptValidState(true);
    f.high.scriptValidState(true);  // full

    TEST_ASSERT_FALSE(f.controller.startManualFill(60));
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(0, onTransitions(f.pump));
}

// Auto level control OFF (manual mode) while the feature is enabled: the
// dry/dry row that would normally start a fill is suppressed, but an explicit
// manual fill still works. Guards the tick(enabled=true, autoLevelControl=false)
// gate a regression would otherwise drop unnoticed.
void test_auto_level_off_suppresses_fill_but_manual_works(void)
{
    Fixture f;
    f.low.scriptValidState(false);
    f.high.scriptValidState(false);  // dry/dry: would normally start a fill

    // Auto level control off (manual mode): no auto fill despite dry/dry.
    f.controller.tick(/*enabled=*/true, /*autoLevelControl=*/false);
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(0, onTransitions(f.pump));

    // A manual fill still works while auto level control is off.
    TEST_ASSERT_TRUE(f.controller.startManualFill(60));
    TEST_ASSERT_TRUE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(1, onTransitions(f.pump));
}

// Feature disabled (FR-013): the pump is forced OFF and ALL logic is skipped,
// even when the marks read dry/dry (which would otherwise start a fill).
void test_feature_disabled_forces_off_and_skips_logic(void)
{
    Fixture f;
    // A manual fill is running...
    TEST_ASSERT_TRUE(f.controller.startManualFill(60));
    TEST_ASSERT_TRUE(f.pump.isRunning());

    // ...disabling the feature forces the pump off, ignoring the dry/dry marks.
    f.low.scriptValidState(false);
    f.high.scriptValidState(false);
    f.controller.tick(/*enabled=*/false, /*autoLevelControl=*/true);
    TEST_ASSERT_FALSE(f.pump.isRunning());

    // A further disabled tick never starts a fill despite the dry/dry marks.
    f.clock.advance(1000);
    f.controller.tick(false, true);
    TEST_ASSERT_FALSE(f.pump.isRunning());
    // Only the manual ON transition ever happened.
    TEST_ASSERT_EQUAL_INT(1, onTransitions(f.pump));
}

}  // namespace

void run_reservoir_tests(void)
{
    // Level truth table
    RUN_TEST(test_invalid_low_no_action);
    RUN_TEST(test_invalid_high_no_action);
    RUN_TEST(test_both_invalid_no_action);
    RUN_TEST(test_full_wet_wet_ensures_stopped);
    RUN_TEST(test_sufficient_wet_dry_no_action);
    RUN_TEST(test_low_dry_dry_starts_fill);
    RUN_TEST(test_implausible_low_dry_high_wet_no_action);

    // Running safety
    RUN_TEST(test_stop_on_high_wet_while_running);
    RUN_TEST(test_max_fill_abort_at_cap);

    // Post-abort cooldown (FR-012a)
    RUN_TEST(test_cooldown_blocks_then_allows_auto_refill);
    RUN_TEST(test_normal_high_wet_stop_does_not_arm_cooldown);
    RUN_TEST(test_manual_fill_bypasses_cooldown);

    // Manual fill + feature gate
    RUN_TEST(test_manual_fill_refused_when_full);
    RUN_TEST(test_auto_level_off_suppresses_fill_but_manual_works);
    RUN_TEST(test_feature_disabled_forces_off_and_skips_logic);
}
