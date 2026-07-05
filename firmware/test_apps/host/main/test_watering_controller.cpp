// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_watering_controller.cpp
 * @brief Host suite for the pure WateringController (feature 011, US1).
 *
 * Drives the REAL WateringController logic over MockSoilSensor +
 * MockWaterPump (real WaterPump enforcement over FakeTimeProvider) +
 * MockConfigStore + MockDataStorage + FakeWallClock + EventLogger. Registered
 * via run_watering_controller_tests() from the shared Unity runner
 * (test_main.cpp); the process exit code equals the failure count (CI gate).
 *
 * Coverage maps to tasks.md T006 (automatic + soak gate) and T007 (fail-safe):
 * start-at-low, no-start/allow-restart across the soak pause, stop-at-high,
 * runtime config change, disabled -> no action, gate-on-read (placeholder +
 * transient failed read), fail-safe unavailable/stale/invalid stops, fail-safe
 * never delayed by the soak gate, and graceful degradation (sensor always
 * failing -> never waters, no crash). The manual override + periodic
 * data-logging branches (US3) land later (T011).
 */

#include <cstdint>

#include "unity.h"

#include "actuators/testing/FakeTimeProvider.h"
#include "actuators/testing/MockWaterPump.h"
#include "control/WateringController.h"
#include "events/EventLogger.h"
#include "interfaces/IDataStorage.h"
#include "sensors/testing/MockSoilSensor.h"
#include "storage/testing/MockConfigStore.h"
#include "storage/testing/MockDataStorage.h"
#include "time/testing/FakeWallClock.h"

namespace {

// ---------------------------------------------------------------------------
// Fixture: fresh collaborators + controller per test, with deterministic
// config that mirrors the store defaults (low 30 %, high 55 %, burst 20 s,
// soak 300 s, enabled).
// ---------------------------------------------------------------------------
struct Fixture {
    FakeTimeProvider clock;
    MockSoilSensor soil;
    MockWaterPump pump{"plant", clock};
    MockConfigStore config;
    MockDataStorage storage;
    FakeWallClock wallClock;
    EventLogger events{storage, wallClock};
    WateringController controller{soil,  pump,      config, storage,
                                  clock, wallClock, events};

    Fixture()
    {
        TEST_ASSERT_TRUE(pump.initialize());
        config.stored.moistureThresholdLow = 30.0f;
        config.stored.moistureThresholdHigh = 55.0f;
        config.stored.wateringDurationS = 20;
        config.stored.minWateringIntervalS = 300;
        config.stored.wateringEnabled = 1;
    }
};

/// Number of output ON transitions recorded by the real pump — a proxy for
/// "how many bursts were started" (each successful runFor drives applyOutput
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

/// Number of persisted fail-safe events (category kCategoryFailsafe).
int failsafeEventCount(const MockDataStorage& storage)
{
    int count = 0;
    for (const auto& event : storage.events) {
        if (event.category == IDataStorage::kCategoryFailsafe) {
            ++count;
        }
    }
    return count;
}

/// Script the sensor's next tick outcome via the plain public fields (no FIFO
/// script): read() returns @p readOk, isAvailable() returns @p available and
/// the getters serve @p moisture. A failed read leaves the value in place
/// (mirroring the last-good contract); the controller must ignore it.
void setSensor(Fixture& f, bool readOk, bool available, float moisture)
{
    f.soil.readResult = readOk;
    f.soil.isAvailableResult = available;
    f.soil.moisture = moisture;
}

// ===========================================================================
// T006 — automatic + soak-gate branches
// ===========================================================================

// Scenario 1: enabled + not running + valid moisture <= low + soak elapsed
// (first burst) -> start a burst.
void test_starts_burst_when_dry_and_enabled(void)
{
    Fixture f;
    setSensor(f, /*readOk=*/true, /*available=*/true, /*moisture=*/20.0f);
    f.controller.tick();

    TEST_ASSERT_TRUE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(1, onTransitions(f.pump));
}

// Disabled automatic watering -> no automatic action at all.
void test_no_start_when_disabled(void)
{
    Fixture f;
    f.config.stored.wateringEnabled = 0;
    setSensor(f, true, true, 20.0f);
    f.controller.tick();

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(0, onTransitions(f.pump));
}

// Scenario 4: running + moisture >= high -> stop.
void test_stops_at_high_threshold(void)
{
    Fixture f;
    setSensor(f, true, true, 20.0f);
    f.controller.tick();
    TEST_ASSERT_TRUE(f.pump.isRunning());

    f.clock.advance(1000);
    setSensor(f, true, true, 60.0f);  // >= high 55
    f.controller.tick();

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StopReason::Commanded),
                          static_cast<int>(f.pump.getLastStopReason()));
}

// Scenarios 2 + 3: after a burst ends (here by the pump's own duration
// self-stop), no new burst starts until the soak pause elapses (assert at
// soak-1 ms), then a burst starts at exactly the soak pause and repeats while
// still dry.
void test_soak_pause_blocks_then_allows_restart(void)
{
    Fixture f;  // burst 20 s, soak 300 s
    setSensor(f, true, true, 20.0f);
    f.controller.tick();  // burst 1 starts
    TEST_ASSERT_TRUE(f.pump.isRunning());

    // Advance the full burst duration: the pump self-stops (DurationElapsed);
    // the controller records the burst end as the soak-gate origin.
    f.clock.advance(20'000);
    setSensor(f, true, true, 20.0f);  // still dry
    f.controller.tick();
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(1, onTransitions(f.pump));  // no immediate restart

    // 1 ms before the soak pause elapses: still no new burst.
    f.clock.advance(299'999);
    setSensor(f, true, true, 20.0f);
    f.controller.tick();
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(1, onTransitions(f.pump));

    // Exactly at the soak pause: the next burst starts (still dry).
    f.clock.advance(1);
    setSensor(f, true, true, 20.0f);
    f.controller.tick();
    TEST_ASSERT_TRUE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(2, onTransitions(f.pump));
}

// A runtime config change (lower threshold) is picked up on the next tick.
void test_config_change_picked_up_next_tick(void)
{
    Fixture f;                        // low 30
    setSensor(f, true, true, 40.0f);  // 40 > 30 -> no start
    f.controller.tick();
    TEST_ASSERT_FALSE(f.pump.isRunning());

    f.config.stored.moistureThresholdLow = 50.0f;  // now 40 <= 50
    f.clock.advance(1000);
    setSensor(f, true, true, 40.0f);
    f.controller.tick();
    TEST_ASSERT_TRUE(f.pump.isRunning());
}

// Scenario 7 (FR-004): before the first successful read the controller does not
// act on the sensor's placeholder value (here 0 %, which is <= low).
void test_no_action_before_first_successful_read(void)
{
    Fixture f;
    setSensor(f, /*readOk=*/false, /*available=*/true, /*moisture=*/0.0f);
    f.controller.tick();

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(0, onTransitions(f.pump));
    // Pump was not running, so no fail-safe event is logged.
    TEST_ASSERT_EQUAL_INT(0, failsafeEventCount(f.storage));
}

// FR-004: a transient read failure within the staleness window (after a prior
// valid read) is gated on the read result — no action, not a fail-safe.
void test_gate_on_transient_failed_read(void)
{
    Fixture f;
    setSensor(f, true, true, 40.0f);  // valid read, 40 > low -> no start
    f.controller.tick();
    TEST_ASSERT_FALSE(f.pump.isRunning());

    f.clock.advance(1000);  // still well within the 30 s window
    setSensor(f, /*readOk=*/false, /*available=*/true, /*moisture=*/20.0f);
    f.controller.tick();

    TEST_ASSERT_FALSE(f.pump.isRunning());  // did not act on the failed read
    TEST_ASSERT_EQUAL_INT(0, onTransitions(f.pump));
    TEST_ASSERT_EQUAL_INT(0, failsafeEventCount(f.storage));
}

// Threshold comparisons are inclusive: moisture == low starts, == high stops.
void test_boundaries_low_and_high_inclusive(void)
{
    Fixture f;
    f.config.stored.wateringDurationS = 300;  // avoid an in-test self-stop

    setSensor(f, true, true, 30.0f);  // == low
    f.controller.tick();
    TEST_ASSERT_TRUE(f.pump.isRunning());

    f.clock.advance(1000);
    setSensor(f, true, true, 55.0f);  // == high
    f.controller.tick();
    TEST_ASSERT_FALSE(f.pump.isRunning());
}

// ===========================================================================
// T007 — fail-safe branches (Constitution I)
// ===========================================================================

// Scenario 5: automatic + running, sensor becomes unavailable -> emergency
// stop + logged fail-safe + no watering decision.
void test_failsafe_unavailable_stops_running_pump(void)
{
    Fixture f;
    f.config.stored.wateringDurationS = 300;  // keep the burst running
    setSensor(f, true, true, 20.0f);
    f.controller.tick();
    TEST_ASSERT_TRUE(f.pump.isRunning());

    f.clock.advance(1000);
    setSensor(f, /*readOk=*/false, /*available=*/false, /*moisture=*/20.0f);
    f.controller.tick();

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(1, failsafeEventCount(f.storage));
    TEST_ASSERT_EQUAL_UINT32(0, f.events.droppedEvents());
}

// Scenario 5: data stale beyond the 30 s window -> emergency stop.
void test_failsafe_stale_stops_running_pump(void)
{
    Fixture f;
    f.config.stored.wateringDurationS = 300;
    setSensor(f, true, true, 20.0f);
    f.controller.tick();
    TEST_ASSERT_TRUE(f.pump.isRunning());

    // Reads keep failing while the sensor still probes available; once the last
    // valid read ages past 30 s the controller fails safe.
    f.clock.advance(30'001);
    setSensor(f, /*readOk=*/false, /*available=*/true, /*moisture=*/20.0f);
    f.controller.tick();

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(1, failsafeEventCount(f.storage));
}

// Scenario 5: a successful read with out-of-range moisture (both bounds)
// -> emergency stop.
void test_failsafe_invalid_moisture_stops_running_pump(void)
{
    {
        Fixture f;
        f.config.stored.wateringDurationS = 300;
        setSensor(f, true, true, 20.0f);
        f.controller.tick();
        TEST_ASSERT_TRUE(f.pump.isRunning());

        f.clock.advance(1000);
        setSensor(f, /*readOk=*/true, /*available=*/true, /*moisture=*/150.0f);
        f.controller.tick();
        TEST_ASSERT_FALSE(f.pump.isRunning());
        TEST_ASSERT_EQUAL_INT(1, failsafeEventCount(f.storage));
    }
    {
        Fixture f;
        f.config.stored.wateringDurationS = 300;
        setSensor(f, true, true, 20.0f);
        f.controller.tick();
        TEST_ASSERT_TRUE(f.pump.isRunning());

        f.clock.advance(1000);
        setSensor(f, /*readOk=*/true, /*available=*/true, /*moisture=*/-5.0f);
        f.controller.tick();
        TEST_ASSERT_FALSE(f.pump.isRunning());
        TEST_ASSERT_EQUAL_INT(1, failsafeEventCount(f.storage));
    }
}

// Scenario 6 (FR-006): a fail-safe stop is applied immediately and is never
// delayed by the soak gate. The pump is running with the soak-gate origin
// already armed (a prior burst ended) and the soil reads dry — the exact state
// where a mis-ordered controller might "keep running"; the fail-safe must still
// stop the pump on the same tick.
void test_failsafe_not_delayed_by_soak(void)
{
    Fixture f;
    f.config.stored.wateringDurationS = 300;   // no self-stop confound
    f.config.stored.minWateringIntervalS = 1;  // 1 s soak so a 2nd burst runs

    setSensor(f, true, true, 20.0f);
    f.controller.tick();  // burst 1 running

    f.clock.advance(2000);
    setSensor(f, true, true, 60.0f);  // >= high -> stop, arm the soak origin
    f.controller.tick();
    TEST_ASSERT_FALSE(f.pump.isRunning());

    f.clock.advance(2000);            // soak (1 s) elapsed
    setSensor(f, true, true, 20.0f);  // dry -> burst 2 runs
    f.controller.tick();
    TEST_ASSERT_TRUE(f.pump.isRunning());

    // Running, dry, soak origin set: trigger a fail-safe -> immediate stop.
    f.clock.advance(1000);
    setSensor(f, /*readOk=*/false, /*available=*/false, /*moisture=*/20.0f);
    f.controller.tick();

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_TRUE(failsafeEventCount(f.storage) >= 1);
}

// During a pending soak pause (pump stopped, dry soil) a fail-safe condition is
// handled on the fail-safe path and no watering is started.
void test_failsafe_during_pending_soak_takes_no_action(void)
{
    Fixture f;  // burst 20 s, soak 300 s
    setSensor(f, true, true, 20.0f);
    f.controller.tick();  // burst 1 running

    f.clock.advance(20'000);
    setSensor(f, true, true, 20.0f);
    f.controller.tick();  // self-stop; soak pending, pump stopped
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(1, onTransitions(f.pump));

    f.clock.advance(1000);  // still inside the soak window
    setSensor(f, /*readOk=*/false, /*available=*/false, /*moisture=*/20.0f);
    f.controller.tick();

    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(1, onTransitions(f.pump));  // no new burst
    // Pump was not running, so no fail-safe event is logged.
    TEST_ASSERT_EQUAL_INT(0, failsafeEventCount(f.storage));
}

// FR-015 graceful degradation: a sensor whose reads always fail (and probes
// unavailable) never causes watering and never crashes the controller.
void test_graceful_degradation_never_waters(void)
{
    Fixture f;
    for (int i = 0; i < 5; ++i) {
        setSensor(f, /*readOk=*/false, /*available=*/false, /*moisture=*/20.0f);
        f.clock.advance(5000);
        f.controller.tick();
        TEST_ASSERT_FALSE(f.pump.isRunning());
    }
    TEST_ASSERT_EQUAL_INT(0, onTransitions(f.pump));
    TEST_ASSERT_EQUAL_UINT32(0, f.events.droppedEvents());
}

}  // namespace

void run_watering_controller_tests(void)
{
    // T006 — automatic + soak gate
    RUN_TEST(test_starts_burst_when_dry_and_enabled);
    RUN_TEST(test_no_start_when_disabled);
    RUN_TEST(test_stops_at_high_threshold);
    RUN_TEST(test_soak_pause_blocks_then_allows_restart);
    RUN_TEST(test_config_change_picked_up_next_tick);
    RUN_TEST(test_no_action_before_first_successful_read);
    RUN_TEST(test_gate_on_transient_failed_read);
    RUN_TEST(test_boundaries_low_and_high_inclusive);

    // T007 — fail-safe
    RUN_TEST(test_failsafe_unavailable_stops_running_pump);
    RUN_TEST(test_failsafe_stale_stops_running_pump);
    RUN_TEST(test_failsafe_invalid_moisture_stops_running_pump);
    RUN_TEST(test_failsafe_not_delayed_by_soak);
    RUN_TEST(test_failsafe_during_pending_soak_takes_no_action);
    RUN_TEST(test_graceful_degradation_never_waters);
}
