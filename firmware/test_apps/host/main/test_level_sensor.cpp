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
 * settle gating (the 500 ms rev2 case incl. notifyPowerOn() re-arm,
 * window-opens-at-first-update, chatter inside the settle window),
 * polarity equivalence (both board polarities produce identical logical
 * results for the same physical scenario), chatter collapsing to a single
 * transition, update-stream purity (time without update() changes
 * nothing), the invalid ⇒ isWaterPresent()-false structural guarantee,
 * the markFaulted() latch (GPIO-init-failure wiring path) and the
 * LockedLevelSensor delegation check. T021 (US4) adds the
 * MockLevelSensor consumer-style tests (SC-006: all four PR-11
 * truth-table states across two instances, with coherent validity), and
 * T022 the per-board fail-direction truths (docs/parity-checklist.md §3,
 * "Pull-up + active-HIGH consequence" item, pinned as host-tested
 * constants).
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
#include "sensors/testing/MockLevelSensor.h"

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
    // rawState() is polarity-INDEPENDENT diagnostics — the one place board
    // polarity must NOT be absorbed: the pin is electrically HIGH, so
    // rawState() reads true even though active LOW maps HIGH to "water
    // absent" (and the sensor is not even valid yet).
    TEST_ASSERT_TRUE(sensor.rawState());
    TEST_ASSERT_FALSE(sensor.isWaterPresent());

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
    // Active LOW: raw HIGH = water absent — while rawState() still reports
    // the electrical level unmapped.
    TEST_ASSERT_FALSE(sensor.isWaterPresent());
    TEST_ASSERT_TRUE(sensor.rawState());
}

void test_settle_window_opens_at_first_update_after_construction(void)
{
    ScriptedInput input;
    input.level = true;
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/true,
                                kDebounceMs, kSettleMs);

    // Dead time between CONSTRUCTION and the owner's first poll must not
    // be consumed by the settle window: it opens at the first update()
    // (same contract the notifyPowerOn() re-arm test pins for that case).
    time.advance(10'000);
    sensor.update();  // settle window opens HERE, not at construction
    step(sensor, time, kSettleMs - 1);
    TEST_ASSERT_FALSE(sensor.isValid());
    step(sensor, time, 1);  // settle done, warm-up opens
    TEST_ASSERT_FALSE(sensor.isValid());
    step(sensor, time, kDebounceMs);
    TEST_ASSERT_TRUE(sensor.isValid());
}

void test_chatter_during_settle_validity_at_exactly_settle_plus_debounce(void)
{
    ScriptedInput input;
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/false,
                                kDebounceMs, kSettleMs);

    // Raw chatters every 100 ms THROUGHOUT the settle window: pre-settle
    // flips neither restart settling nor pre-latch the debounce candidate.
    sensor.update();  // settle window opens at t=0
    for (int i = 1; i <= 4; ++i) {  // flips observed at t=100..400
        input.level = (i % 2) != 0;
        step(sensor, time, 100);
        TEST_ASSERT_FALSE(sensor.isValid());
    }

    // Stable from before the settle boundary: warm-up opens at t=500 on
    // the then-current raw value, so validity arrives at EXACTLY
    // settle + debounce (t=800) — any earlier candidate latch or settle
    // restart would move this boundary.
    input.level = true;
    step(sensor, time, 100);  // t=500: settle elapses, warm-up opens
    TEST_ASSERT_FALSE(sensor.isValid());
    step(sensor, time, kDebounceMs - 1);  // t=799: one short
    TEST_ASSERT_FALSE(sensor.isValid());
    step(sensor, time, 1);  // t=800 = settle + debounce exactly
    TEST_ASSERT_TRUE(sensor.isValid());
    TEST_ASSERT_TRUE(sensor.isWaterPresent());
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
// Invalid ⇒ isWaterPresent() false, structurally (ILevelSensor contract:
// never a stale or phantom value). The active-LOW (rev2) config is the
// sharp case: a naive polarity map of the constructed stableRaw_ = false
// would read !false = "water present" — a phantom.
// ---------------------------------------------------------------------------

void test_invalid_reads_false_active_low(void)
{
    ScriptedInput input;
    input.level = true;  // raw HIGH = dry on rev2
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/true,
                                kDebounceMs, kSettleMs);

    // Freshly constructed and during settle: false, never the phantom.
    TEST_ASSERT_FALSE(sensor.isValid());
    TEST_ASSERT_FALSE(sensor.isWaterPresent());
    sensor.update();  // settle window opens
    step(sensor, time, kSettleMs - 1);
    TEST_ASSERT_FALSE(sensor.isValid());
    TEST_ASSERT_FALSE(sensor.isWaterPresent());

    // Reach TRACKING with water PRESENT (raw LOW on rev2).
    input.level = false;
    step(sensor, time, 1);           // settle done, warm-up opens on LOW
    step(sensor, time, kDebounceMs); // warm-up complete
    TEST_ASSERT_TRUE(sensor.isValid());
    TEST_ASSERT_TRUE(sensor.isWaterPresent());

    // After notifyPowerOn(): invalid again, and the stale "water present"
    // must NOT leak — false until re-validated.
    sensor.notifyPowerOn();
    TEST_ASSERT_FALSE(sensor.isValid());
    TEST_ASSERT_FALSE(sensor.isWaterPresent());
    sensor.update();  // settle window re-opens
    step(sensor, time, kSettleMs - 1);
    TEST_ASSERT_FALSE(sensor.isWaterPresent());
    step(sensor, time, 1);
    step(sensor, time, kDebounceMs - 1);
    TEST_ASSERT_FALSE(sensor.isWaterPresent());  // still warming up

    // Re-validated (raw held LOW throughout): the true reading returns.
    step(sensor, time, 1);
    TEST_ASSERT_TRUE(sensor.isValid());
    TEST_ASSERT_TRUE(sensor.isWaterPresent());
}

// ---------------------------------------------------------------------------
// markFaulted(): the GPIO-init-failure latch (wiring-site concept)
// ---------------------------------------------------------------------------

void test_faulted_sensor_never_becomes_valid(void)
{
    ScriptedInput input;
    input.level = true;  // perfectly stable input — must not matter
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/false,
                                kDebounceMs, /*settleMs=*/0);
    sensor.markFaulted();

    // However long and stable the update stream, a faulted sensor stays
    // invalid (a floating unconfigured pin must never debounce its way to
    // isValid()) and its logical state reads false.
    sensor.update();
    for (int i = 0; i < 20; ++i) {
        step(sensor, time, 100);  // 2 s total ≫ settle + debounce
        TEST_ASSERT_FALSE(sensor.isValid());
        TEST_ASSERT_FALSE(sensor.isWaterPresent());
    }
    // rawState() diagnostics still follow the pin even while faulted.
    TEST_ASSERT_TRUE(sensor.rawState());
}

void test_notify_power_on_clears_fault(void)
{
    ScriptedInput input;
    input.level = true;
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/false,
                                kDebounceMs, /*settleMs=*/0);
    sensor.markFaulted();
    step(sensor, time, 1000);
    TEST_ASSERT_FALSE(sensor.isValid());

    // notifyPowerOn() is the deliberate re-arm (PR-14 rail control / an
    // operator power cycle): the fault clears and the normal settle +
    // warm-up sequence runs from the next update().
    sensor.notifyPowerOn();
    TEST_ASSERT_FALSE(sensor.isValid());  // re-armed, not instantly valid
    sensor.update();                      // warm-up opens (settle 0)
    step(sensor, time, kDebounceMs - 1);
    TEST_ASSERT_FALSE(sensor.isValid());
    step(sensor, time, 1);
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

// ---------------------------------------------------------------------------
// Fail direction per board (T022, docs/parity-checklist.md §3, "Pull-up +
// active-HIGH consequence" item):
// a disconnected sensor input is pulled HIGH by the pull-up (internal on
// both boards, external 10 kΩ additionally on rev2 — research R4). The
// resulting LOGICAL state differs per board polarity, and BOTH directions
// fail safe for their pump topology — these tests pin the documented
// truths so a future polarity or pull change trips loudly.
// ---------------------------------------------------------------------------

void test_fail_direction_rev1_disconnected_reads_water_present(void)
{
    // rev1 (two-pump node, active HIGH — non-inverting TXS0108E path):
    // pulled-HIGH = "water present" at every mark ⇒ the reservoir looks
    // full, so the FILL pump stays off. Fails toward "do not pump"
    // (checklist §3 "Pull-up + active-HIGH consequence" item; legacy
    // src/main.cpp:231-233 behavior preserved).
    ScriptedInput input;
    input.level = true;  // disconnected: the pull-up pins the raw input HIGH
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/false,
                                kDebounceMs, /*settleMs=*/0);

    sensor.update();
    step(sensor, time, kDebounceMs);
    TEST_ASSERT_TRUE(sensor.isValid());
    TEST_ASSERT_TRUE(sensor.isWaterPresent());
}

void test_fail_direction_rev2_disconnected_reads_water_absent(void)
{
    // rev2 (single-pump DRAWING node, active LOW — 2N7002 inverter):
    // pulled-HIGH = "water absent" at every mark ⇒ the node believes the
    // reservoir is empty and PR-11's controller will not run the plant
    // pump dry. The opposite direction of rev1 — and exactly as safe,
    // because the pump topology is opposite: rev1 fills a reservoir
    // (phantom water = no overfill), rev2 draws from one (phantom
    // emptiness = no dry run). Checklist §3 "Pull-up + active-HIGH
    // consequence" item.
    ScriptedInput input;
    input.level = true;  // disconnected: the pull-up pins the raw input HIGH
    FakeTimeProvider time;
    DebouncedLevelSensor sensor(input, time, /*activeLow=*/true,
                                kDebounceMs, kSettleMs);

    sensor.update();
    step(sensor, time, kSettleMs);   // rev2 settle gate (FW-3)
    step(sensor, time, kDebounceMs); // debounce warm-up
    TEST_ASSERT_TRUE(sensor.isValid());
    TEST_ASSERT_FALSE(sensor.isWaterPresent());
}

// ---------------------------------------------------------------------------
// MockLevelSensor (T021): SC-006 consumer-style tests — two instances
// express all four PR-11 truth-table states with coherent validity.
// ---------------------------------------------------------------------------

void test_mock_level_sensor_expresses_pr11_truth_table(void)
{
    // The four reservoir states PR-11's controller decides on (legacy
    // truth table, docs/parity-checklist.md §3 / src/main.cpp:533-550),
    // scripted exactly as a consumer will: gate on validity first, then
    // read both logical states.
    MockLevelSensor low;
    MockLevelSensor high;

    struct Row {
        bool lowWet;
        bool highWet;
    };
    // both wet (full) | low-only wet (sufficient) | both dry (start fill)
    // | low dry + high wet (physically invalid — REPORTED as-is, this
    // layer never masks it; interpreting it is PR-11's job).
    const Row rows[] = {
        {true, true}, {true, false}, {false, false}, {false, true}};

    for (const Row& row : rows) {
        low.scriptValidState(row.lowWet);
        high.scriptValidState(row.highWet);
        TEST_ASSERT_TRUE(low.isValid());
        TEST_ASSERT_TRUE(high.isValid());
        TEST_ASSERT_EQUAL(row.lowWet, low.isWaterPresent());
        TEST_ASSERT_EQUAL(row.highWet, high.isWaterPresent());
    }
}

void test_mock_level_sensor_invalid_is_coherent(void)
{
    MockLevelSensor sensor;

    // Freshly constructed: not yet valid (the real sensor's settle/warm-up
    // start) and the meaningless logical state reads false, never true.
    TEST_ASSERT_FALSE(sensor.isValid());
    TEST_ASSERT_FALSE(sensor.isWaterPresent());

    // A valid wet reading that goes invalid must NEVER leave a stale
    // "water present" behind — the consistency the helpers enforce
    // (PR-04 lesson: incoherent mock states hide consumer bugs).
    sensor.scriptValidState(true);
    TEST_ASSERT_TRUE(sensor.isWaterPresent());
    sensor.scriptInvalid();
    TEST_ASSERT_FALSE(sensor.isValid());
    TEST_ASSERT_FALSE(sensor.isWaterPresent());

    // notifyPowerOn() mirrors the real settle re-arm: invalidates and is
    // counted; update() only counts (scripted state is poll-stable).
    sensor.scriptValidState(false);
    sensor.notifyPowerOn();
    TEST_ASSERT_FALSE(sensor.isValid());
    TEST_ASSERT_EQUAL_INT(1, sensor.notifyPowerOnCalls);
    sensor.update();
    sensor.update();
    TEST_ASSERT_EQUAL_INT(2, sensor.updateCalls);
    TEST_ASSERT_FALSE(sensor.isValid());  // updates never revalidate a mock
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
    RUN_TEST(test_settle_window_opens_at_first_update_after_construction);
    RUN_TEST(test_chatter_during_settle_validity_at_exactly_settle_plus_debounce);
    RUN_TEST(test_notify_power_on_rearms_settle);
    RUN_TEST(test_invalid_reads_false_active_low);
    RUN_TEST(test_faulted_sensor_never_becomes_valid);
    RUN_TEST(test_notify_power_on_clears_fault);
    RUN_TEST(test_polarity_equivalence);
    RUN_TEST(test_chatter_single_transition);
    RUN_TEST(test_raw_state_is_undebounced);
    RUN_TEST(test_locked_level_sensor_delegates);
    RUN_TEST(test_fail_direction_rev1_disconnected_reads_water_present);
    RUN_TEST(test_fail_direction_rev2_disconnected_reads_water_absent);
    RUN_TEST(test_mock_level_sensor_expresses_pr11_truth_table);
    RUN_TEST(test_mock_level_sensor_invalid_is_coherent);
}
