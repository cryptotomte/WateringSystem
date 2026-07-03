// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file DebouncedLevelSensor.cpp
 * @brief Settle/warm-up/debounce state machine implementation (pure C++).
 *
 * Builds on every target including linux (host-tested against a scripted
 * IDigitalInput + FakeTimeProvider). State machine normative in
 * specs/006-level-sensors-ina226/data-model.md.
 */

#include "sensors/DebouncedLevelSensor.h"

DebouncedLevelSensor::DebouncedLevelSensor(IDigitalInput& input,
                                           ITimeProvider& time,
                                           bool activeLow, int64_t debounceMs,
                                           int64_t settleMs)
    : input_(input),
      time_(time),
      activeLow_(activeLow),
      debounceMs_(debounceMs < 0 ? 0 : debounceMs),
      settleMs_(settleMs < 0 ? 0 : settleMs)
{
    // Negative windows are clamped to 0 (document-and-clamp, header
    // contract): a window that "completed in the past" must never exist.
    // Starts in SETTLING with the settle window armed but not started —
    // the first update() opens it (no time read in the constructor; all
    // state motion belongs to the update() stream).
}

void DebouncedLevelSensor::update()
{
    const int64_t now = time_.nowMs();
    const bool raw = input_.read();
    raw_ = raw;  // rawState() diagnostics: always the latest sample

    switch (state_) {
    case State::Settling:
        if (settleStartMs_ == kNotStarted) {
            settleStartMs_ = now;
        }
        if (now - settleStartMs_ >= settleMs_) {
            // Settle elapsed (immediately when settleMs_ == 0): enter
            // warm-up and open the first stability window on the current
            // raw value. Completion is checked on a LATER update — a full
            // debounce window must pass inside WARMUP before validity.
            state_ = State::Warmup;
            candidateRaw_ = raw;
            windowStartMs_ = now;
        }
        break;

    case State::Warmup:
        if (raw != candidateRaw_) {
            // Any flip restarts the window (research.md R2).
            candidateRaw_ = raw;
            windowStartMs_ = now;
        } else if (now - windowStartMs_ >= debounceMs_) {
            // First stable window complete: readings become valid.
            stableRaw_ = candidateRaw_;
            state_ = State::Tracking;
        }
        break;

    case State::Tracking:
        if (raw != candidateRaw_) {
            // Any flip restarts the window. A flip BACK to the reported
            // state also lands here: the candidate rejoins stableRaw_ and
            // the pending change is cancelled (no transition ever fires
            // from a value that did not hold for a full window).
            candidateRaw_ = raw;
            windowStartMs_ = now;
        } else if (candidateRaw_ != stableRaw_ &&
                   now - windowStartMs_ >= debounceMs_) {
            stableRaw_ = candidateRaw_;
        }
        break;

    case State::Faulted:
        // Latched (markFaulted): no progress on any update — only
        // notifyPowerOn(), a deliberate re-arm, leaves this state. raw_
        // above still tracks the pin for rawState() diagnostics.
        break;
    }
}

bool DebouncedLevelSensor::isValid()
{
    return state_ == State::Tracking;
}

bool DebouncedLevelSensor::isWaterPresent()
{
    // Polarity absorbed here (FW-5): logical = stable raw XOR active-low.
    // Structurally false whenever invalid (ILevelSensor contract: never a
    // stale or phantom value) — outside TRACKING there is no stable value
    // to map, so nothing meaningless can leak even to a consumer that
    // forgot to gate on isValid().
    return state_ == State::Tracking &&
           (activeLow_ ? !stableRaw_ : stableRaw_);
}

bool DebouncedLevelSensor::rawState()
{
    return raw_;
}

void DebouncedLevelSensor::notifyPowerOn()
{
    // Re-arm settle gating (FW-3) from ANY state — including FAULTED,
    // where this deliberate re-arm is the documented recovery path (PR-14
    // rail control / an operator power cycle): readings are invalid again
    // until the settle window AND a fresh debounce warm-up complete. The
    // settle window opens at the next update() (kNotStarted), keeping
    // validity a pure function of the update stream.
    state_ = State::Settling;
    settleStartMs_ = kNotStarted;
}

void DebouncedLevelSensor::markFaulted()
{
    // Latched fault (header contract): called at the wiring site when the
    // raw input's GPIO init failed — a floating, unconfigured pin must
    // never debounce its way to isValid(). Cleared only by
    // notifyPowerOn().
    state_ = State::Faulted;
}
