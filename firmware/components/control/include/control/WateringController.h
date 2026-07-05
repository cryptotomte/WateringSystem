// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file WateringController.h
 * @brief Pure automatic watering logic: pulsed watering + soak gate + fail-safe.
 *
 * Feature 011 (PR-11), User Stories 1 + 3. All watering-decision and
 * safety-condition logic lives here as pure C++17 over the injected interfaces +
 * clocks, so it is exercised on the IDF linux preview target against the
 * mocks/fakes (test_watering_controller.cpp). This class MUST NOT include esp_* /
 * esp_timer or make any hardware/network call: monotonic time comes from
 * ITimeProvider, wall-clock time from IWallClock, and every threshold/duration is
 * read from IConfigStore each tick (runtime-tunable). Normative contract:
 * specs/011-watering-controller-host-tests/contracts/watering-controller.md;
 * decision tables + constants: .../data-model.md.
 *
 * Scope note: the US1 slice implements the automatic path (thresholds, soak gate,
 * fail-safe). The US3 slice adds the manual override (startManual/stop, exempt
 * from the automatic fail-safe) and the periodic env+soil data-logging that runs
 * every tick regardless of mode. On-target soil access is wired through a
 * LockedSoilSensor snapshot; the pure logic drives the injected sensors directly.
 *
 * Concurrency: the controller is unsynchronized and single-writer (its own
 * watchdog-registered task). tick() drives one blocking read() (controller-as-
 * reader) and then consumes one non-blocking snapshot() for the availability +
 * values it decides on — no second bus probe; on-target the sensor is a
 * LockedSoilSensor so that snapshot is copied under a single lock.
 */

#ifndef WATERINGSYSTEM_CONTROL_WATERINGCONTROLLER_H
#define WATERINGSYSTEM_CONTROL_WATERINGCONTROLLER_H

#include <cstdint>

#include "events/EventLogger.h"
#include "interfaces/IConfigStore.h"
#include "interfaces/IDataStorage.h"
#include "interfaces/IEnvironmentalSensor.h"
#include "interfaces/ISoilSensor.h"
#include "interfaces/ITimeProvider.h"
#include "interfaces/IWallClock.h"
#include "interfaces/IWaterPump.h"

/**
 * @brief Pulsed automatic watering with an enforced soak pause and fail-safe.
 *
 * Invariants (host-tested):
 *  1. A fail-safe stop (sensor unavailable / stale / out-of-range moisture) is
 *     applied BEFORE the soak gate and is never delayed by it (FR-005/006).
 *  2. Automatic decisions gate on a successful, in-range read result, never on
 *     the sensor's placeholder/last-good values (FR-004).
 *  3. After a burst ends (self-stop at the configured duration or a stop at the
 *     high threshold) no new automatic burst starts until the soak pause has
 *     elapsed, even while the soil still reads dry (FR-003).
 */
class WateringController {
public:
    /// Staleness window: a valid soil read older than this fails safe (§2).
    static constexpr int64_t kStalenessMs = 30'000;

    /// Valid moisture range (percent); a read outside it is treated as invalid.
    static constexpr float kMoistureMinPct = 0.0f;
    static constexpr float kMoistureMaxPct = 100.0f;

    /**
     * @brief Construct over injected collaborators (references; must outlive).
     *
     * Constructing with a sensor that fails to initialize is safe: no work runs
     * in the constructor and tick() simply fails safe (FR-015).
     */
    WateringController(ISoilSensor& soil, IEnvironmentalSensor& env,
                       IWaterPump& plant, IConfigStore& config,
                       IDataStorage& storage, ITimeProvider& clock,
                       IWallClock& wallClock, EventLogger& events)
        : soil_(soil),
          env_(env),
          plant_(plant),
          config_(config),
          storage_(storage),
          clock_(clock),
          wallClock_(wallClock),
          events_(events)
    {
    }

    WateringController(const WateringController&) = delete;
    WateringController& operator=(const WateringController&) = delete;

    /**
     * @brief One evaluation. Non-blocking; call at a fixed cadence.
     *
     * Order (safety first, soak gate last): pump self-stop/cap enforcement →
     * burst-end detection → single soil read → periodic data-log (runs in every
     * mode, before any early return) → manual-override bypass → enabled gate →
     * fail-safe (unavailable/stale/invalid) → gate-on-read → watering decision
     * (stop-at-high / start-burst-if-soak-elapsed).
     */
    void tick();

    /**
     * @brief Operator manual override: run the plant pump for a bounded time.
     *
     * Clamps @p durationS to [1, 300] s, calls plant.runFor(clamped) and, on
     * success, flags the run manual so tick() exempts it from the automatic
     * fail-safe and the soak gate (FR-007/008). The reservoir cooldown is N/A
     * here (that gate lives in ReservoirController). Returns the pump result.
     */
    bool startManual(int durationS);

    /**
     * @brief Stop the plant pump and clear any manual override. Works in any
     * mode; a subsequent tick() resumes automatic evaluation (FR-010).
     */
    void stop();

private:
    /**
     * @brief Periodic env + soil telemetry log (FR-014). Runs every tick from
     * tick(), before any mode/fail-safe early return, so telemetry is recorded
     * even while watering is disabled or a fail-safe is active.
     *
     * Gated on IWallClock::isTimeSet() (never logs a bogus 1970 epoch) and on
     * the configured data-log interval (the first eligible log after time is set
     * fires immediately). Env readings are logged on a successful, available
     * read; soil readings are logged only when @p soilValid (the result of the
     * single soil read() this tick), with NPK included only when >= 0. Soil
     * metrics come from @p soil — the same coherent snapshot tick() decided on,
     * so the logged values match the decision values with no second read. All
     * writes carry IWallClock::nowEpoch(); storage self-bounds, so the store
     * result is ignored.
     */
    void maybeLogData(int64_t now, bool soilValid, const SoilSnapshot& soil);

    ISoilSensor& soil_;
    IEnvironmentalSensor& env_;  ///< periodic data-logging source (US3)
    IWaterPump& plant_;
    IConfigStore& config_;
    IDataStorage& storage_;
    ITimeProvider& clock_;
    IWallClock& wallClock_;
    EventLogger& events_;

    /// Monotonic time the last automatic burst ended — the soak-gate origin
    /// (0 = no burst has ended yet).
    int64_t lastBurstEndMs_ = 0;

    /// Monotonic time of the last successful, in-range soil read
    /// (0 = never — treated as stale until the first valid read).
    int64_t lastValidSoilMs_ = 0;

    /// True while an automatic burst is in progress; lets tick() detect the
    /// pump's own timed self-stop (duration/cap) as a burst end so the soak
    /// gate starts from the burst end regardless of how it stopped.
    bool burstActive_ = false;

    /// True while an operator manual run is active. A manual run is exempt from
    /// the automatic fail-safe and the soak gate; it clears on stop() or when
    /// the pump self-stops (its clamped run duration elapses, or the 300 s hard
    /// cap).
    bool manualRunActive_ = false;

    /// Monotonic time of the last data-log batch (0 = none yet — the first
    /// eligible log after the wall clock is set fires immediately).
    int64_t lastDataLogMs_ = 0;
};

#endif /* WATERINGSYSTEM_CONTROL_WATERINGCONTROLLER_H */
