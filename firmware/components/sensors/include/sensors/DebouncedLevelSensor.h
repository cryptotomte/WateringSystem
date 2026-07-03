// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file DebouncedLevelSensor.h
 * @brief Pure C++ level-sensor logic: settle gating, debounce, polarity.
 *
 * Implements ILevelSensor over an injected raw-input source
 * (IDigitalInput — GpioLevelSensor on target, a scripted input in host
 * tests) and an injected ITimeProvider (never esp_timer directly — the
 * WaterPump time-injection pattern). ALL policy lives here: the
 * SETTLING → WARMUP → TRACKING state machine
 * (specs/006-level-sensors-ina226/data-model.md), the stability-window
 * debounce (any raw flip restarts the window — research.md R2) and the
 * board-owned polarity mapping (FW-5; the active-low flag is passed in
 * from board.h at the wiring site, keeping this class board-agnostic).
 * It contains NO hardware access, so it is compiled and unit-tested on
 * the IDF linux preview target (constitution II).
 *
 * Concurrency: unsynchronized by design (host-testable); cross-task
 * consumers (main-loop update() + console REPL) wrap it in
 * LockedLevelSensor and access it only through the wrapper.
 */

#ifndef WATERINGSYSTEM_SENSORS_DEBOUNCEDLEVELSENSOR_H
#define WATERINGSYSTEM_SENSORS_DEBOUNCEDLEVELSENSOR_H

#include <cstdint>

#include "interfaces/IDigitalInput.h"
#include "interfaces/ILevelSensor.h"
#include "interfaces/ITimeProvider.h"

/**
 * @brief ILevelSensor with settle gating, warm-up and debounce over a raw
 * digital input.
 *
 * State machine (data-model.md):
 *
 *   CONSTRUCT / notifyPowerOn() ──▶ SETTLING
 *   SETTLING ──▶ WARMUP     when settleMs has elapsed (settleMs = 0: on
 *                           the first update())
 *   WARMUP   ──▶ TRACKING   when the raw input has held one value for a
 *                           full debounce window (isValid() becomes true)
 *   TRACKING: a raw flip opens a stability window; the reported state
 *             changes only once the new value has held for debounceMs;
 *             every flip restarts the window.
 *   markFaulted() ──▶ FAULTED   from any state (latched: update() does not
 *                           progress, isValid() stays false); only
 *                           notifyPowerOn() — a deliberate re-arm — leaves
 *                           it, back to SETTLING.
 *
 * All time is measured from the update() stream: the settle window starts
 * at the FIRST update() after construction/notifyPowerOn() (never at the
 * event itself — "no update ⇒ no state change", ILevelSensor contract; the
 * slight lateness is in the safe direction). A window of N ms is complete
 * on the first update() where at least N ms have elapsed since it opened.
 * Sampled-system caveat: the raw value only has to be identical at the
 * samples the owner takes — with a starved poll cadence a window can
 * complete from just two samples spanning it, not a continuously observed
 * hold.
 */
class DebouncedLevelSensor : public ILevelSensor {
public:
    /**
     * @brief Construct the sensor over an injected input and clock.
     *
     * Starts in SETTLING (gated — even with settleMs = 0 a full debounce
     * warm-up must complete before isValid()).
     *
     * @param input      Raw input source; must outlive this object.
     * @param time       Monotonic clock; must outlive this object.
     * @param activeLow  Board polarity (FW-5): true when a LOW pin level
     *                   means water present (rev2's 2N7002 inverter); pass
     *                   BOARD_LEVEL_ACTIVE_LOW from board.h.
     * @param debounceMs Stability window before a reported state change
     *                   (BOARD_LEVEL_DEBOUNCE_MS). Negative values are
     *                   clamped to 0 (document-and-clamp — never
     *                   already-in-the-past window math).
     * @param settleMs   Invalidity window after a power-on event
     *                   (BOARD_LEVEL_SETTLE_MS; 0 = no settle gating
     *                   beyond the debounce warm-up). Negative values are
     *                   clamped to 0.
     */
    DebouncedLevelSensor(IDigitalInput& input, ITimeProvider& time,
                         bool activeLow, int64_t debounceMs,
                         int64_t settleMs);

    ~DebouncedLevelSensor() override = default;

    DebouncedLevelSensor(const DebouncedLevelSensor&) = delete;
    DebouncedLevelSensor& operator=(const DebouncedLevelSensor&) = delete;

    // ILevelSensor
    void update() override;
    bool isValid() override;
    bool isWaterPresent() override;
    bool rawState() override;
    void notifyPowerOn() override;

    /**
     * @brief Latch the sensor in a FAULTED state: isValid() is pinned
     * false and update() no longer progresses the state machine.
     *
     * Deliberately NOT part of ILevelSensor (the interface surface stays
     * unchanged): faulting is a wiring-site decision — app_main calls this
     * on the concrete object when the raw input's GPIO init failed, so an
     * unconfigured, floating pin can never debounce its way to a "valid"
     * reading. Recovery is a deliberate re-arm only: notifyPowerOn()
     * returns the sensor to Settling (PR-14 rail control, or an operator
     * power cycle — which reboots and re-runs the GPIO init anyway).
     */
    void markFaulted();

private:
    enum class State { Settling, Warmup, Tracking, Faulted };

    /// Sentinel: the settle window has not started yet (armed, waiting for
    /// the first update()).
    static constexpr int64_t kNotStarted = -1;

    IDigitalInput& input_;
    ITimeProvider& time_;
    const bool activeLow_;
    const int64_t debounceMs_;
    const int64_t settleMs_;

    State state_ = State::Settling;
    int64_t settleStartMs_ = kNotStarted;  ///< first update() after arming
    bool raw_ = false;           ///< last sampled pin level (rawState())
    bool candidateRaw_ = false;  ///< value the stability window is timing
    int64_t windowStartMs_ = 0;  ///< when candidateRaw_ last changed
    bool stableRaw_ = false;     ///< debounced pin level (valid iff TRACKING)
};

#endif /* WATERINGSYSTEM_SENSORS_DEBOUNCEDLEVELSENSOR_H */
