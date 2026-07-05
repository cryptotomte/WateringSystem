// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ReservoirController.cpp
 * @brief Reservoir auto-fill state machine implementation (pure C++, tested).
 *
 * Pure: no esp_* / esp_timer includes, no hardware or network calls. All time
 * is injected (ITimeProvider). See ReservoirController.h for the invariants and
 * the data-model for the level truth table and the post-abort cooldown.
 */

#include "control/ReservoirController.h"

void ReservoirController::tick(bool enabled, bool autoLevelControl)
{
    // Snapshot the running state BEFORE update() so a max-runtime abort that
    // happens inside update() this tick is observable as a running->stopped
    // edge (armCooldownOnAbortEdge below).
    const bool wasRunning = fillPump_.isRunning();
    const int64_t now = clock_.nowMs();

    // Actuator layer first: enforce the hard 300 s max-fill cap (self-stop).
    fillPump_.update();

    // Arm the post-abort cooldown on a max-runtime abort edge. This runs before
    // the high-wet stop below, so a normal Commanded stop never arms it.
    armCooldownOnAbortEdge(wasRunning, now);

    // Feature gate (FR-013): reservoir disabled / board lacks the pump -> force
    // the pump OFF and skip ALL reservoir logic.
    if (!enabled) {
        fillPump_.stop();
        return;
    }

    // Running safety (manual + auto): the high mark reading wet stops the fill
    // immediately, however it was started. While a fill is in progress (or was
    // just stopped this tick) the truth table is never evaluated, so nothing
    // re-starts on the same tick.
    if (fillPump_.isRunning()) {
        if (highMark_.isValid() && highMark_.isWaterPresent()) {
            fillPump_.stop();
        }
        return;
    }

    // Automatic level control only (manual mode leaves the reservoir to the
    // operator via startManualFill()).
    if (!autoLevelControl) {
        return;
    }
    evaluateAuto(now);
}

bool ReservoirController::startManualFill(int durationS)
{
    // Refuse a manual fill when the reservoir is already full (high mark wet).
    if (highMark_.isValid() && highMark_.isWaterPresent()) {
        return false;
    }
    // Bypasses the post-abort cooldown; the pump's runFor() enforces the
    // 1..300 s bound (no silent clamping) and the 300 s cap while it runs.
    return fillPump_.runFor(durationS);
}

void ReservoirController::stop()
{
    fillPump_.stop();
}

void ReservoirController::armCooldownOnAbortEdge(bool wasRunning, int64_t now)
{
    if (wasRunning && !fillPump_.isRunning() &&
        fillPump_.getLastStopReason() == StopReason::MaxRuntimeForced) {
        lastAbortMs_ = now;
    }
}

void ReservoirController::evaluateAuto(int64_t now)
{
    // An invalid mark is NEVER "water absent" (FR-012): take no action while
    // either mark is not-yet-valid/invalid.
    if (!lowMark_.isValid() || !highMark_.isValid()) {
        return;
    }

    const bool lowWet = lowMark_.isWaterPresent();
    const bool highWet = highMark_.isWaterPresent();

    if (highWet) {
        // wet/wet = full -> ensure stopped (a no-op here: the pump is not
        // running, running safety already handled any in-flight fill).
        // dry/wet = physically implausible -> no action.
        if (lowWet) {
            fillPump_.stop();
        }
        return;
    }

    // High mark dry from here on.
    if (lowWet) {
        // wet/dry = sufficient water -> no action.
        return;
    }

    // dry/dry = low water -> start a fill, unless the post-abort cooldown is
    // still active (FR-012a) — do not re-slam the pump after a max-runtime
    // abort while the water still reads low.
    if (lastAbortMs_ != 0 &&
        (now - lastAbortMs_) < kReservoirRefillCooldownMs) {
        return;
    }
    fillPump_.runFor(kReservoirFillDurationS);
}
