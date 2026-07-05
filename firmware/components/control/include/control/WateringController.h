// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file WateringController.h
 * @brief Pure automatic watering logic: pulsed watering + soak gate + fail-safe.
 *
 * Feature 011 (PR-11), User Story 1. All watering-decision and safety-condition
 * logic lives here as pure C++17 over the injected interfaces + clocks, so it is
 * exercised on the IDF linux preview target against the mocks/fakes
 * (test_watering_controller.cpp). This class MUST NOT include esp_* / esp_timer
 * or make any hardware/network call: monotonic time comes from ITimeProvider,
 * wall-clock time from IWallClock, and every threshold/duration is read from
 * IConfigStore each tick (runtime-tunable). Normative contract:
 * specs/011-watering-controller-host-tests/contracts/watering-controller.md;
 * decision tables + constants: .../data-model.md.
 *
 * Scope note: this US1 slice implements the automatic path (tick()). The manual
 * override (startManual/stop bookkeeping) and periodic data-logging are US3 and
 * are only left as clean seams here — not implemented.
 *
 * Concurrency: the controller is unsynchronized and single-writer (its own
 * watchdog-registered task). On-target US3 wires the sensor through a
 * LockedSoilSensor snapshot so read()+availability come from one locked
 * acquisition; the pure US1 logic drives the injected ISoilSensor directly.
 */

#ifndef WATERINGSYSTEM_CONTROL_WATERINGCONTROLLER_H
#define WATERINGSYSTEM_CONTROL_WATERINGCONTROLLER_H

#include <cstdint>

#include "events/EventLogger.h"
#include "interfaces/IConfigStore.h"
#include "interfaces/IDataStorage.h"
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
    WateringController(ISoilSensor& soil, IWaterPump& plant, IConfigStore& config,
                       IDataStorage& storage, ITimeProvider& clock,
                       IWallClock& wallClock, EventLogger& events)
        : soil_(soil),
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
     * @brief One automatic evaluation. Non-blocking; call at a fixed cadence.
     *
     * Order (safety first, soak gate last): pump self-stop/cap enforcement →
     * enabled gate → fail-safe (unavailable/stale/invalid) → gate-on-read →
     * watering decision (stop-at-high / start-burst-if-soak-elapsed).
     */
    void tick();

private:
    ISoilSensor& soil_;
    IWaterPump& plant_;
    IConfigStore& config_;
    IDataStorage& storage_;   ///< US3 data-logging seam (unused in US1)
    ITimeProvider& clock_;
    IWallClock& wallClock_;   ///< US3 data-logging seam (unused in US1)
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
};

#endif /* WATERINGSYSTEM_CONTROL_WATERINGCONTROLLER_H */
