// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_level_sensor.cpp
 * @brief Host tests for the DebouncedLevelSensor logic (linux target).
 *
 * Tests the REAL settle/debounce/polarity state machine
 * (DebouncedLevelSensor) via a scripted IDigitalInput + FakeTimeProvider.
 * Registered via run_level_sensor_tests() from the shared Unity runner
 * (test_main.cpp); the process exit code equals the failure count and is
 * the CI gate.
 *
 * Coverage maps to tasks.md T009 (US1): debounce boundary (change only
 * after a full window; every flip restarts it), warm-up not-yet-valid,
 * settle gating (the 500 ms rev2 case incl. notifyPowerOn() re-arm),
 * polarity equivalence (both board polarities produce identical logical
 * results for the same physical scenario), chatter collapsing to a single
 * transition, update-stream purity (time without update() changes
 * nothing) and the LockedLevelSensor delegation check. Fail-direction
 * truth tests (checklist line 97) arrive with T022.
 *
 * Timing convention (DebouncedLevelSensor contract): a window of N ms is
 * complete on the first update() where at least N ms have elapsed since
 * it opened — N-1 ms is inside the window, N ms is past it.
 */

#include <cstdint>

#include "unity.h"

#include "actuators/testing/FakeTimeProvider.h"
#include "interfaces/IDigitalInput.h"
#include "sensors/DebouncedLevelSensor.h"
#include "sensors/LockedLevelSensor.h"

namespace {

// Board-table values (data-model.md); the tests pin the policy against
// these, independent of which board the suite notionally runs on.
constexpr int64_t kDebounceMs = 300;
constexpr int64_t kSettleMs = 500;  ///< rev2 (FW-3); rev1 uses 0

/// Scripted raw input: the test sets `level`, the sensor samples it.
struct ScriptedInput : IDigitalInput {
    bool level = false;
    bool read() override { return level; }
};

/// Advance the fake clock by @p ms, then poll once (the owner's cadence).
void step(DebouncedLevelSensor& sensor, FakeTimeProvider& time, int64_t ms)
{
    time.advance(ms);
    sensor.update();
}

// ---------------------------------------------------------------------------
// Warm-up: not-yet-valid before the first stable window
// ---------------------------------------------------------------------------

void test_warmup_invalid_until_first_stable_window(void)
{
    ScriptedInput input;
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/false,
                                kDebounceMs, /*settleMs=*/0);

    // Before any update: nothing is known.
    TEST_ASSERT_FALSE(sensor.isValid());

    // First update (settle 0 → warm-up opens here on the current raw).
    input.level = true;
    sensor.update();
    TEST_ASSERT_FALSE(sensor.isValid());

    // One millisecond short of the window: still warming up.
    step(sensor, time, kDebounceMs - 1);
    TEST_ASSERT_FALSE(sensor.isValid());

    // Window complete: valid, and the logical state is the stable raw
    // (active HIGH: raw HIGH = water present).
    step(sensor, time, 1);
    TEST_ASSERT_TRUE(sensor.isValid());
    TEST_ASSERT_TRUE(sensor.isWaterPresent());
}

void test_warmup_flip_restarts_window(void)
{
    ScriptedInput input;
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/false,
                                kDebounceMs, /*settleMs=*/0);

    input.level = false;
    sensor.update();  // warm-up opens on raw=0
    step(sensor, time, 200);
    TEST_ASSERT_FALSE(sensor.isValid());

    // Flip 200 ms in: the warm-up window restarts on the new value.
    input.level = true;
    step(sensor, time, 0);
    step(sensor, time, kDebounceMs - 1);
    TEST_ASSERT_FALSE(sensor.isValid());
    step(sensor, time, 1);
    TEST_ASSERT_TRUE(sensor.isValid());
    TEST_ASSERT_TRUE(sensor.isWaterPresent());
}

// ---------------------------------------------------------------------------
// Update-stream purity: no update ⇒ no state change
// ---------------------------------------------------------------------------

void test_time_without_update_changes_nothing(void)
{
    ScriptedInput input;
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/false,
                                kDebounceMs, /*settleMs=*/0);

    input.level = true;
    sensor.update();  // warm-up opens

    // Hours pass without a single update(): the getters alone must never
    // advance the state machine (ILevelSensor contract).
    time.advance(3'600'000);
    TEST_ASSERT_FALSE(sensor.isValid());
    TEST_ASSERT_FALSE(sensor.isValid());  // repeated reads: still nothing

    // The next update() sees a raw value that held across the gap — the
    // window (opened at the first update) is long complete.
    sensor.update();
    TEST_ASSERT_TRUE(sensor.isValid());
}

// ---------------------------------------------------------------------------
// Tracking: debounce boundary + flip semantics (US1 core)
// ---------------------------------------------------------------------------

/// Drive a fresh active-HIGH sensor (settle 0) to TRACKING on @p raw.
void reach_tracking(DebouncedLevelSensor& sensor, ScriptedInput& input,
                    FakeTimeProvider& time, bool raw)
{
    input.level = raw;
    sensor.update();
    step(sensor, time, kDebounceMs);
    TEST_ASSERT_TRUE(sensor.isValid());
}

void test_tracking_change_only_after_full_window(void)
{
    ScriptedInput input;
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/false,
                                kDebounceMs, /*settleMs=*/0);
    reach_tracking(sensor, input, time, /*raw=*/false);
    TEST_ASSERT_FALSE(sensor.isWaterPresent());

    // Raw flips to water; poll at the 10 Hz owner cadence: the reported
    // state must hold until a full 300 ms window has passed (~3 samples).
    input.level = true;
    step(sensor, time, 0);  // flip observed, window opens
    for (int i = 0; i < 2; ++i) {
        step(sensor, time, 100);  // 100, 200 ms into the window
        TEST_ASSERT_TRUE(sensor.isValid());
        TEST_ASSERT_FALSE(sensor.isWaterPresent());
    }
    step(sensor, time, 99);  // 299 ms: one short
    TEST_ASSERT_FALSE(sensor.isWaterPresent());
    step(sensor, time, 1);  // 300 ms: transition fires
    TEST_ASSERT_TRUE(sensor.isValid());
    TEST_ASSERT_TRUE(sensor.isWaterPresent());
}

void test_tracking_flip_restarts_window(void)
{
    ScriptedInput input;
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/false,
                                kDebounceMs, /*settleMs=*/0);
    reach_tracking(sensor, input, time, /*raw=*/true);
    TEST_ASSERT_TRUE(sensor.isWaterPresent());

    // Water starts draining: raw drops, but bounces once 150 ms in and
    // drops again 50 ms later — EVERY flip restarts the window, so the
    // change fires a full 300 ms after the LAST flip only.
    input.level = false;
    step(sensor, time, 0);  // drop observed
    step(sensor, time, 150);
    input.level = true;  // bounce back
    step(sensor, time, 0);
    step(sensor, time, 50);
    input.level = false;  // final drop; window restarts HERE
    step(sensor, time, 0);
    step(sensor, time, kDebounceMs - 1);
    TEST_ASSERT_TRUE(sensor.isWaterPresent());  // still water: 299 ms
    step(sensor, time, 1);
    TEST_ASSERT_FALSE(sensor.isWaterPresent());  // dry after a full window
    TEST_ASSERT_TRUE(sensor.isValid());  // tracking never loses validity
}

void test_tracking_flip_back_cancels_pending_change(void)
{
    ScriptedInput input;
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/false,
                                kDebounceMs, /*settleMs=*/0);
    reach_tracking(sensor, input, time, /*raw=*/true);

    // A 200 ms dip that returns to the stable value must never surface —
    // no transition fires, and the returned-to state needs no new window.
    input.level = false;
    step(sensor, time, 0);
    step(sensor, time, 200);
    input.level = true;
    step(sensor, time, 0);
    for (int i = 0; i < 10; ++i) {
        step(sensor, time, 100);
        TEST_ASSERT_TRUE(sensor.isWaterPresent());
    }
}

// ---------------------------------------------------------------------------
// Settle gating (FW-3, rev2 case) + notifyPowerOn() re-arm
// ---------------------------------------------------------------------------

void test_settle_gating_500ms(void)
{
    ScriptedInput input;
    FakeTimeProvider time;
    // rev2 configuration: active LOW, 500 ms settle. Raw HIGH = dry.
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/true,
                                kDebounceMs, kSettleMs);

    input.level = true;
    sensor.update();  // settle window opens at the FIRST update
    TEST_ASSERT_FALSE(sensor.isValid());

    // 499 ms: still settling — the raw value is not even being timed yet.
    step(sensor, time, kSettleMs - 1);
    TEST_ASSERT_FALSE(sensor.isValid());

    // 500 ms: settle elapses, warm-up opens; validity needs a further
    // full debounce window on top (settle + warm-up stack).
    step(sensor, time, 1);
    TEST_ASSERT_FALSE(sensor.isValid());
    step(sensor, time, kDebounceMs - 1);
    TEST_ASSERT_FALSE(sensor.isValid());
    step(sensor, time, 1);
    TEST_ASSERT_TRUE(sensor.isValid());
    // Active LOW: raw HIGH = water absent.
    TEST_ASSERT_FALSE(sensor.isWaterPresent());
}

void test_notify_power_on_rearms_settle(void)
{
    ScriptedInput input;
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/true,
                                kDebounceMs, kSettleMs);

    // Reach TRACKING once (settle + warm-up).
    input.level = false;  // active LOW: water present
    sensor.update();
    step(sensor, time, kSettleMs);
    step(sensor, time, kDebounceMs);
    TEST_ASSERT_TRUE(sensor.isValid());
    TEST_ASSERT_TRUE(sensor.isWaterPresent());

    // Sensor rail power-cycles (PR-14 will do this for real): readings
    // are invalid IMMEDIATELY, before any further update.
    sensor.notifyPowerOn();
    TEST_ASSERT_FALSE(sensor.isValid());

    // The full settle + warm-up sequence is required again, measured from
    // the first update() after the power-on event.
    time.advance(10'000);  // dead time before the owner polls again
    sensor.update();       // settle window opens HERE
    step(sensor, time, kSettleMs - 1);
    TEST_ASSERT_FALSE(sensor.isValid());
    step(sensor, time, 1);  // settle done, warm-up opens
    TEST_ASSERT_FALSE(sensor.isValid());
    step(sensor, time, kDebounceMs);
    TEST_ASSERT_TRUE(sensor.isValid());
    TEST_ASSERT_TRUE(sensor.isWaterPresent());
}

// ---------------------------------------------------------------------------
// Polarity equivalence (FW-5): board polarity is fully absorbed
// ---------------------------------------------------------------------------

void test_polarity_equivalence(void)
{
    // The same physical scenario on both boards: water arrives, chatters
    // once, then stays. rev1 (active HIGH) sees raw = water; rev2 (active
    // LOW) sees raw = !water. Logical outputs must be identical stepwise.
    ScriptedInput inputHigh;
    ScriptedInput inputLow;
    FakeTimeProvider timeHigh;
    FakeTimeProvider timeLow;
    DebouncedLevelSensor rev1(inputHigh, timeHigh, /*activeLow=*/false,
                              kDebounceMs, /*settleMs=*/0);
    DebouncedLevelSensor rev2(inputLow, timeLow, /*activeLow=*/true,
                              kDebounceMs, /*settleMs=*/0);

    const bool water[] = {false, false, false, false, true,  false,
                          true,  true,  true,  true,  true,  true,
                          true,  true,  true,  true,  true,  true};
    for (bool present : water) {
        inputHigh.level = present;   // active HIGH: raw follows water
        inputLow.level = !present;   // active LOW: raw inverted (2N7002)
        timeHigh.advance(100);
        timeLow.advance(100);
        rev1.update();
        rev2.update();
        TEST_ASSERT_EQUAL(rev1.isValid(), rev2.isValid());
        if (rev1.isValid()) {
            TEST_ASSERT_EQUAL(rev1.isWaterPresent(), rev2.isWaterPresent());
        }
    }
    // Sanity: the scenario actually ended valid + water present.
    TEST_ASSERT_TRUE(rev1.isValid());
    TEST_ASSERT_TRUE(rev1.isWaterPresent());
}

// ---------------------------------------------------------------------------
// Chatter collapses to a single transition (SC-005)
// ---------------------------------------------------------------------------

void test_chatter_single_transition(void)
{
    ScriptedInput input;
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/false,
                                kDebounceMs, /*settleMs=*/0);
    reach_tracking(sensor, input, time, /*raw=*/false);

    // Water sloshing at a mark: the raw input toggles every 50 ms for a
    // second, then holds HIGH. Count reported transitions throughout.
    int transitions = 0;
    bool last = sensor.isWaterPresent();
    for (int i = 0; i < 20; ++i) {
        input.level = (i % 2) != 0;
        step(sensor, time, 50);
        if (sensor.isWaterPresent() != last) {
            ++transitions;
            last = sensor.isWaterPresent();
        }
    }
    TEST_ASSERT_EQUAL_INT(0, transitions);  // chatter never surfaces
    TEST_ASSERT_TRUE(sensor.isValid());     // and validity never drops

    input.level = true;  // slosh settles: water covers the mark
    for (int i = 0; i < 6; ++i) {
        step(sensor, time, 100);
        if (sensor.isWaterPresent() != last) {
            ++transitions;
            last = sensor.isWaterPresent();
        }
    }
    TEST_ASSERT_EQUAL_INT(1, transitions);  // exactly one clean transition
    TEST_ASSERT_TRUE(sensor.isWaterPresent());
}

// ---------------------------------------------------------------------------
// rawState() diagnostics: undebounced, straight from the last sample
// ---------------------------------------------------------------------------

void test_raw_state_is_undebounced(void)
{
    ScriptedInput input;
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/false,
                                kDebounceMs, /*settleMs=*/0);
    reach_tracking(sensor, input, time, /*raw=*/false);

    // The raw view follows the pin immediately while the logical view
    // still holds the debounced state.
    input.level = true;
    step(sensor, time, 100);
    TEST_ASSERT_TRUE(sensor.rawState());
    TEST_ASSERT_FALSE(sensor.isWaterPresent());
}

// ---------------------------------------------------------------------------
// LockedLevelSensor: pure delegation (decorator adds a mutex, nothing else)
// ---------------------------------------------------------------------------

void test_locked_level_sensor_delegates(void)
{
    ScriptedInput input;
    FakeTimeProvider time;
    DebouncedLevelSensor raw(input, time, /*activeLow=*/false, kDebounceMs,
                             /*settleMs=*/0);
    LockedLevelSensor sensor(raw);

    input.level = true;
    sensor.update();
    TEST_ASSERT_FALSE(sensor.isValid());
    time.advance(kDebounceMs);
    sensor.update();
    TEST_ASSERT_TRUE(sensor.isValid());
    TEST_ASSERT_TRUE(sensor.isWaterPresent());
    TEST_ASSERT_TRUE(sensor.rawState());

    sensor.notifyPowerOn();
    TEST_ASSERT_FALSE(sensor.isValid());
}

}  // namespace

void run_level_sensor_tests(void)
{
    RUN_TEST(test_warmup_invalid_until_first_stable_window);
    RUN_TEST(test_warmup_flip_restarts_window);
    RUN_TEST(test_time_without_update_changes_nothing);
    RUN_TEST(test_tracking_change_only_after_full_window);
    RUN_TEST(test_tracking_flip_restarts_window);
    RUN_TEST(test_tracking_flip_back_cancels_pending_change);
    RUN_TEST(test_settle_gating_500ms);
    RUN_TEST(test_notify_power_on_rearms_settle);
    RUN_TEST(test_polarity_equivalence);
    RUN_TEST(test_chatter_single_transition);
    RUN_TEST(test_raw_state_is_undebounced);
    RUN_TEST(test_locked_level_sensor_delegates);
}
