// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ReservoirController.h
 * @brief Pure reservoir auto-fill state machine (level truth table + safety).
 *
 * Feature 011 (PR-11), User Story 2. Board-independent: the whole state
 * machine is host-tested regardless of which board wires the fill pump — the
 * BOARD_HAS_RESERVOIR_PUMP capability flag gates only the construction/wiring
 * in app_main, never this logic (FR-013). Composed from TWO independent
 * ILevelSensor marks (low + high), one IWaterPump (the fill pump), an injected
 * ITimeProvider (monotonic time for the post-abort cooldown) and the pure
 * EventLogger. Normative contract:
 * specs/011-watering-controller-host-tests/contracts/reservoir-controller.md;
 * decision table + constants: .../data-model.md.
 *
 * This class MUST NOT include esp_* / esp_timer or make any hardware/network
 * call: monotonic time comes from ITimeProvider and every input is an injected
 * interface, so it is exercised on the IDF linux preview target against the
 * mocks/fakes (test_reservoir.cpp).
 *
 * Concurrency: the controller is unsynchronized and single-writer (its own
 * watchdog-registered task). On-target US3 drives the shared level sensors and
 * pump through the Locked* wrappers; the pure US2 logic drives the injected
 * interfaces directly.
 */

#ifndef WATERINGSYSTEM_CONTROL_RESERVOIRCONTROLLER_H
#define WATERINGSYSTEM_CONTROL_RESERVOIRCONTROLLER_H

#include <cstdint>

#include "events/EventLogger.h"
#include "interfaces/ILevelSensor.h"
#include "interfaces/ITimeProvider.h"
#include "interfaces/IWaterPump.h"

/**
 * @brief Reservoir auto-fill with running safety and a post-abort cooldown.
 *
 * Invariants (host-tested):
 *  1. An invalid level sensor is NEVER treated as "water absent": if either
 *     mark is not-yet-valid/invalid the controller takes no automatic action
 *     (FR-012).
 *  2. Running safety is unconditional: whenever the fill pump runs (auto or
 *     manual), the high mark reading wet stops it immediately, and the pump's
 *     own update() aborts it at the hard 300 s max-fill cap.
 *  3. After a fill ends by the max-runtime abort (StopReason::MaxRuntimeForced)
 *     no new AUTOMATIC fill starts until kReservoirRefillCooldownMs has elapsed
 *     — even while the water still reads low — preventing an endless 300 s
 *     cycle on a stuck high sensor or an empty source (FR-012a). A normal
 *     high-wet stop does NOT arm the cooldown; a manual fill bypasses it.
 *  4. The feature gate forces the pump OFF and skips all logic when the
 *     reservoir feature is disabled / absent on the board (FR-013).
 */
class ReservoirController {
public:
    /// Cooldown after a max-runtime abort before another AUTOMATIC fill may
    /// start (FR-012a). Documented constant, tunable; a manual fill bypasses
    /// it and a normal high-wet stop never arms it.
    static constexpr int64_t kReservoirRefillCooldownMs = 60'000;

    /// Automatic fill duration, in seconds. Deliberately equal to the pump's
    /// hard max-runtime cap (WaterPump::kDefaultMaxRunTimeMs == 300 s): an
    /// auto fill runs until the high mark trips (running safety stops it early)
    /// or, if the high mark never trips, until the cap aborts it as
    /// StopReason::MaxRuntimeForced. The IWaterPump interface does not expose
    /// its cap, so the duration is pinned here to match it (contract: "fill
    /// duration + the 300 s cap come from the pump").
    static constexpr int kReservoirFillDurationS = 300;

    /**
     * @brief Construct over injected collaborators (references; must outlive).
     *
     * Constructing with sensors that fail to initialize is safe: no work runs
     * in the constructor and tick() simply takes no action while a mark is
     * invalid (FR-012/FR-015).
     */
    ReservoirController(ILevelSensor& lowMark, ILevelSensor& highMark,
                        IWaterPump& fillPump, ITimeProvider& clock,
                        EventLogger& events)
        : lowMark_(lowMark),
          highMark_(highMark),
          fillPump_(fillPump),
          clock_(clock),
          events_(events)
    {
    }

    ReservoirController(const ReservoirController&) = delete;
    ReservoirController& operator=(const ReservoirController&) = delete;

    /**
     * @brief One reservoir evaluation. Non-blocking; call at a fixed cadence.
     *
     * Order (safety first): fill-pump self-stop/cap enforcement → arm the
     * post-abort cooldown on a max-runtime abort edge → feature gate (force off
     * when disabled) → running safety (stop on high-wet) → when enabled + auto
     * and the pump is not running, evaluate the level truth table (respecting
     * the cooldown for the dry/dry start).
     *
     * @param enabled          reservoir feature enabled (board has the pump).
     * @param autoLevelControl automatic level control active (else manual only).
     */
    void tick(bool enabled, bool autoLevelControl);

    /**
     * @brief Start a manual fill (explicit operator override).
     *
     * Refused when the reservoir is already full (the high mark reads wet).
     * Bypasses the post-abort cooldown. The duration is bounded by the pump's
     * runFor() contract (1..300 s; out-of-range requests are rejected, no
     * silent clamping); running safety and the 300 s cap still apply while it
     * runs.
     *
     * @param durationS requested fill duration in seconds.
     * @return true if the fill was started, false if refused/rejected.
     */
    bool startManualFill(int durationS);

    /// Stop the fill pump (any mode).
    void stop();

private:
    /// Arm the post-abort cooldown when the fill pump self-stopped at the hard
    /// max-runtime cap during this tick's update() — a running->stopped edge
    /// with StopReason::MaxRuntimeForced. A normal Commanded/high-wet stop is
    /// issued AFTER this check, so it is never mistaken for an abort.
    void armCooldownOnAbortEdge(bool wasRunning, int64_t now);

    /// Evaluate the level truth table (auto mode, pump not running).
    void evaluateAuto(int64_t now);

    ILevelSensor& lowMark_;
    ILevelSensor& highMark_;
    IWaterPump& fillPump_;
    ITimeProvider& clock_;
    EventLogger& events_;  ///< US3 event-logging seam (unused in US2)

    /// Monotonic time of the last max-runtime abort (0 = none). While
    /// now - lastAbortMs_ < kReservoirRefillCooldownMs, an automatic fill is
    /// suppressed even when the water reads low (FR-012a).
    int64_t lastAbortMs_ = 0;
};

#endif /* WATERINGSYSTEM_CONTROL_RESERVOIRCONTROLLER_H */
