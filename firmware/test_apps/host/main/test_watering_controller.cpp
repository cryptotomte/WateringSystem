// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_watering_controller.cpp
 * @brief Host suite for the pure WateringController (feature 011, US1 + US3).
 *
 * Drives the REAL WateringController logic over MockSoilSensor +
 * MockEnvironmentalSensor + MockWaterPump (real WaterPump enforcement over
 * FakeTimeProvider) + MockConfigStore + MockDataStorage + FakeWallClock +
 * EventLogger. Registered via run_watering_controller_tests() from the shared
 * Unity runner (test_main.cpp); the process exit code equals the failure count
 * (CI gate).
 *
 * Coverage maps to tasks.md T006 (automatic + soak gate), T007 (fail-safe) and
 * T011 (manual override + periodic data-logging): start-at-low,
 * no-start/allow-restart across the soak pause, stop-at-high, runtime config
 * change, disabled -> no action, gate-on-read (placeholder + transient failed
 * read), fail-safe unavailable/stale/invalid stops, fail-safe never delayed by
 * the soak gate, graceful degradation (sensor always failing -> never waters,
 * no crash), manual override (bypasses fail-safe, 300 s cap, lower clamp,
 * auto-runs stay automatic, stop() clears the override) and data-logging
 * (cadence, epoch timestamp, NPK >= 0 filter, time-not-set gate, independence
 * from the fail-safe path).
 */

#include <cstdint>

#include "unity.h"

#include "actuators/testing/FakeTimeProvider.h"
#include "actuators/testing/MockWaterPump.h"
#include "control/WateringController.h"
#include "events/EventLogger.h"
#include "interfaces/IDataStorage.h"
#include "sensors/testing/MockEnvironmentalSensor.h"
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
    MockEnvironmentalSensor env;
    MockWaterPump pump{"plant", clock};
    MockConfigStore config;
    MockDataStorage storage;
    FakeWallClock wallClock;
    EventLogger events{storage, wallClock};
    WateringController controller{soil,    env,       pump,      config,
                                  storage, clock,     wallClock, events};

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

/// Total sensor readings persisted across all metrics (a proxy for "how many
/// data-log values were written"). Independent of the event log.
int sensorReadingCount(const MockDataStorage& storage)
{
    int count = 0;
    for (const auto& entry : storage.history) {
        count += static_cast<int>(entry.second.size());
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

// ===========================================================================
// T011 — manual override + periodic data-logging (US3)
// ===========================================================================

// FR-007/008: a manual run is an explicit operator override — it keeps running
// even when the soil sensor is failing/unavailable (exempt from the automatic
// fail-safe). The wall clock is left unset so no data-logging noise interferes.
void test_manual_run_bypasses_sensor_failure(void)
{
    Fixture f;
    setSensor(f, /*readOk=*/false, /*available=*/false, /*moisture=*/20.0f);

    TEST_ASSERT_TRUE(f.controller.startManual(60));
    TEST_ASSERT_TRUE(f.pump.isRunning());

    // A tick that would fail-safe an automatic run leaves the manual run alone.
    f.clock.advance(1000);
    f.controller.tick();
    TEST_ASSERT_TRUE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(0, failsafeEventCount(f.storage));
}

// FR-008: an over-long manual duration is clamped to the 300 s cap — the run is
// bounded, stopping at 300 s with MaxRuntimeForced (not earlier).
void test_manual_run_capped_at_300s(void)
{
    Fixture f;
    setSensor(f, false, false, 20.0f);

    TEST_ASSERT_TRUE(f.controller.startManual(9999));  // clamps to 300
    TEST_ASSERT_TRUE(f.pump.isRunning());

    // Well before the cap the run is still going (proves it was not clamped low).
    f.clock.advance(250'000);
    f.controller.tick();
    TEST_ASSERT_TRUE(f.pump.isRunning());

    // At exactly 300 s the pump self-stops and the override clears next tick.
    f.clock.advance(50'000);  // total 300 s
    f.controller.tick();
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StopReason::MaxRuntimeForced),
                          static_cast<int>(f.pump.getLastStopReason()));
}

// FR-008: a below-range manual duration is clamped up to 1 s — startManual
// succeeds and starts a bounded 1 s run.
void test_manual_run_lower_clamp(void)
{
    Fixture f;
    setSensor(f, false, false, 20.0f);

    TEST_ASSERT_TRUE(f.controller.startManual(0));  // clamps to 1
    TEST_ASSERT_TRUE(f.pump.isRunning());

    f.clock.advance(1000);  // the 1 s run self-stops
    f.controller.tick();
    TEST_ASSERT_FALSE(f.pump.isRunning());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StopReason::DurationElapsed),
                          static_cast<int>(f.pump.getLastStopReason()));
}

// FR-007: an automatically-started burst is NOT flagged manual — the fail-safe
// still applies to it (the mirror of the manual-bypass case).
void test_auto_run_is_not_flagged_manual(void)
{
    Fixture f;
    f.config.stored.wateringDurationS = 300;  // keep the burst running
    setSensor(f, true, true, 20.0f);
    f.controller.tick();  // automatic burst starts
    TEST_ASSERT_TRUE(f.pump.isRunning());

    f.clock.advance(1000);
    setSensor(f, /*readOk=*/false, /*available=*/false, /*moisture=*/20.0f);
    f.controller.tick();

    TEST_ASSERT_FALSE(f.pump.isRunning());  // fail-safe stopped it
    TEST_ASSERT_EQUAL_INT(1, failsafeEventCount(f.storage));
}

// FR-010: stop() clears the manual override so a later tick resumes automatic
// evaluation (proved by an automatic burst starting afterwards).
void test_stop_clears_manual_override(void)
{
    Fixture f;
    setSensor(f, false, false, 20.0f);
    TEST_ASSERT_TRUE(f.controller.startManual(60));
    TEST_ASSERT_TRUE(f.pump.isRunning());

    f.controller.stop();
    TEST_ASSERT_FALSE(f.pump.isRunning());

    // Override cleared: dry + valid + enabled + soak elapsed -> automatic burst.
    f.clock.advance(1000);
    setSensor(f, /*readOk=*/true, /*available=*/true, /*moisture=*/20.0f);
    f.controller.tick();
    TEST_ASSERT_TRUE(f.pump.isRunning());
    // Two ON transitions total: the manual run + the fresh automatic burst
    // (proving the override was cleared and automatic evaluation resumed).
    TEST_ASSERT_EQUAL_INT(2, onTransitions(f.pump));
}

// FR-014: with time set + env/soil valid, the first eligible tick logs one
// env+soil batch; a tick inside the interval logs nothing new; a tick past the
// interval logs the next batch.
//
// All three NPK channels are scripted >= 0, so the batch spans the full,
// maximal metric set: 3 env + 4 soil-base (moisture, temperature, ph, ec) +
// 3 NPK = exactly 10 distinct metrics, which fits the store's kMaxMetrics=10
// cap with no silent drop (soil_humidity is not logged — it duplicates
// soil_moisture).
void test_data_log_cadence(void)
{
    Fixture f;
    f.config.stored.dataLogIntervalMs = 60'000;
    f.wallClock.setEpoch(1'700'000'000);  // time set
    f.env.scriptSuccessfulRead(21.5f, 55.0f, 1013.0f);
    // Soil valid, moisture 40 (> low 30 -> no watering); all NPK >= 0.
    f.soil.scriptSuccessfulRead(40.0f, 18.0f, 40.0f, 6.5f, 1.2f, 3.0f, 5.0f,
                                8.0f);

    // First eligible tick logs one batch of 10.
    f.clock.advance(1000);
    f.controller.tick();
    TEST_ASSERT_EQUAL_INT(10, sensorReadingCount(f.storage));

    // Inside the interval: nothing new.
    f.clock.advance(59'999);
    f.controller.tick();
    TEST_ASSERT_EQUAL_INT(10, sensorReadingCount(f.storage));

    // Exactly at the interval: the next batch is logged (+10 -> 20).
    f.clock.advance(1);
    f.controller.tick();
    TEST_ASSERT_EQUAL_INT(20, sensorReadingCount(f.storage));
}

// FR-014: stored readings carry nowEpoch() as their timestamp, and an NPK
// channel < 0 is skipped while the >= 0 channels are logged.
void test_data_log_epoch_and_npk_filter(void)
{
    Fixture f;
    f.config.stored.dataLogIntervalMs = 60'000;
    const uint32_t kEpoch = 1'700'000'123;
    f.wallClock.setEpoch(kEpoch);
    f.env.scriptSuccessfulRead(21.5f, 55.0f, 1013.0f);
    // Nitrogen negative -> skipped; phosphorus + potassium >= 0 -> logged.
    f.soil.scriptSuccessfulRead(40.0f, 18.0f, 40.0f, 6.5f, 1.2f, -1.0f, 5.0f,
                                8.0f);

    f.clock.advance(1000);
    f.controller.tick();

    // One NPK channel negative -> filtered out: 3 env + 4 soil-base + 2 NPK
    // = 9 metrics logged this tick, none dropped by the store.
    TEST_ASSERT_EQUAL_INT(9, sensorReadingCount(f.storage));
    TEST_ASSERT_EQUAL_INT(
        0, static_cast<int>(
               f.storage.getSensorReadings("soil_nitrogen", 0, UINT32_MAX)
                   .size()));
    // soil_humidity is never logged (it duplicates soil_moisture).
    TEST_ASSERT_EQUAL_INT(
        0, static_cast<int>(
               f.storage.getSensorReadings("soil_humidity", 0, UINT32_MAX)
                   .size()));
    const auto phos =
        f.storage.getSensorReadings("soil_phosphorus", 0, UINT32_MAX);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(phos.size()));
    TEST_ASSERT_EQUAL_INT(
        1, static_cast<int>(
               f.storage.getSensorReadings("soil_potassium", 0, UINT32_MAX)
                   .size()));

    // Epoch on both an env and a soil reading equals nowEpoch().
    TEST_ASSERT_EQUAL_UINT32(kEpoch, phos[0].epoch);
    const auto temp =
        f.storage.getSensorReadings("env_temperature", 0, UINT32_MAX);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(temp.size()));
    TEST_ASSERT_EQUAL_UINT32(kEpoch, temp[0].epoch);
}

// FR-014: while the wall clock is unset nothing is logged (no bogus 1970
// epoch), regardless of the interval; logging resumes once time is set.
void test_data_log_gated_on_time_set(void)
{
    Fixture f;
    f.config.stored.dataLogIntervalMs = 60'000;
    f.env.scriptSuccessfulRead(21.5f, 55.0f, 1013.0f);
    // All NPK >= 0 -> the full 10 distinct metrics, fits the store cap.
    f.soil.scriptSuccessfulRead(40.0f, 18.0f, 40.0f, 6.5f, 1.2f, 3.0f, 5.0f,
                                8.0f);

    // Time not set: no logging even after more than a full interval.
    f.clock.advance(1000);
    f.controller.tick();
    f.clock.advance(120'000);
    f.controller.tick();
    TEST_ASSERT_EQUAL_INT(0, sensorReadingCount(f.storage));

    // Time set: the next eligible tick logs a full batch of 10.
    f.wallClock.setEpoch(1'700'000'000);
    f.clock.advance(1000);
    f.controller.tick();
    TEST_ASSERT_EQUAL_INT(10, sensorReadingCount(f.storage));
}

// FR-014: data-logging runs before the fail-safe early return — while the soil
// sensor is unavailable (fail-safe path) but env is valid and time is set, env
// metrics are still logged and soil metrics are skipped that tick.
void test_data_log_runs_on_failsafe_path(void)
{
    Fixture f;
    f.config.stored.dataLogIntervalMs = 60'000;
    f.wallClock.setEpoch(1'700'000'000);
    f.env.scriptSuccessfulRead(21.5f, 55.0f, 1013.0f);
    // Soil read fails + unavailable -> fail-safe path, soilValid == false.
    setSensor(f, /*readOk=*/false, /*available=*/false, /*moisture=*/20.0f);

    f.clock.advance(1000);
    f.controller.tick();

    TEST_ASSERT_EQUAL_INT(3, sensorReadingCount(f.storage));  // env only
    TEST_ASSERT_EQUAL_INT(
        1, static_cast<int>(
               f.storage.getSensorReadings("env_temperature", 0, UINT32_MAX)
                   .size()));
    TEST_ASSERT_EQUAL_INT(
        0, static_cast<int>(
               f.storage.getSensorReadings("soil_moisture", 0, UINT32_MAX)
                   .size()));
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

    // T011 — manual override + periodic data-logging (US3)
    RUN_TEST(test_manual_run_bypasses_sensor_failure);
    RUN_TEST(test_manual_run_capped_at_300s);
    RUN_TEST(test_manual_run_lower_clamp);
    RUN_TEST(test_auto_run_is_not_flagged_manual);
    RUN_TEST(test_stop_clears_manual_override);
    RUN_TEST(test_data_log_cadence);
    RUN_TEST(test_data_log_epoch_and_npk_filter);
    RUN_TEST(test_data_log_gated_on_time_set);
    RUN_TEST(test_data_log_runs_on_failsafe_path);
}
