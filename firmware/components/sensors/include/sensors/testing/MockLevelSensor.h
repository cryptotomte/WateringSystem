// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file MockLevelSensor.h
 * @brief Scriptable ILevelSensor test double (header-only).
 *
 * Serves the host tests of level-sensor CONSUMERS — PR-11's watering
 * controller composes its reservoir truth table (both-wet / low-only /
 * both-dry / the physically invalid low-dry+high-wet, plus the
 * sensor-invalid "do not act" row) from two instances of this mock. The
 * driver's own settle/debounce/polarity logic is tested against the REAL
 * DebouncedLevelSensor via a scripted IDigitalInput — never through this
 * mock. Never compiled into target builds (only included from test code).
 * No IDF includes.
 *
 * Consistency helpers from the start (PR-04 lesson, MockEnvironmentalSensor
 * pattern): scriptValidState()/scriptInvalid() keep validity and the
 * logical state coherent — an invalid sensor never serves a stale
 * "water present". This matches the interface contract exactly:
 * isWaterPresent() returns false whenever invalid (never a stale or
 * phantom value), which the real DebouncedLevelSensor also guarantees
 * structurally.
 * rawState() is a separate plain scripted field: it is diagnostics-only
 * and polarity-dependent, which a board-agnostic mock cannot derive from
 * the logical state — no coherence is defined between the two (matching
 * the interface, where raw is explicitly undebounced/unmapped).
 */

#ifndef WATERINGSYSTEM_SENSORS_TESTING_MOCKLEVELSENSOR_H
#define WATERINGSYSTEM_SENSORS_TESTING_MOCKLEVELSENSOR_H

#include "interfaces/ILevelSensor.h"

/**
 * @brief ILevelSensor serving a scripted validity + logical state,
 * instrumented for tests.
 *
 * Freshly constructed the mock is INVALID (matching the real sensor's
 * settle/warm-up start). update() only counts calls — the scripted state
 * is stable until the test changes it (a consumer must behave identically
 * however many polls it takes). notifyPowerOn() invalidates, mirroring
 * the real settle re-arm, and counts calls.
 */
class MockLevelSensor : public ILevelSensor {
public:
    // -- Instrumentation ------------------------------------------------------

    int updateCalls = 0;
    int notifyPowerOnCalls = 0;

    // -- Scripted raw state (diagnostics-only, no coherence with logical) ----

    bool scriptedRawState = false;  ///< served by rawState()

    // -- Consistency helpers --------------------------------------------------

    /// Script a VALID sensor reporting @p present (water at this mark).
    void scriptValidState(bool present)
    {
        valid_ = true;
        present_ = present;
    }

    /// Script a NOT-YET-VALID sensor (settling/warming up/faulted): the
    /// logical state loses meaning and is served as false — the interface
    /// contract shared with the real sensor, never a stale previous value.
    void scriptInvalid()
    {
        valid_ = false;
        present_ = false;
    }

    // -- ILevelSensor ----------------------------------------------------------

    void update() override { ++updateCalls; }

    bool isValid() override { return valid_; }

    bool isWaterPresent() override { return valid_ && present_; }

    bool rawState() override { return scriptedRawState; }

    void notifyPowerOn() override
    {
        ++notifyPowerOnCalls;
        // Mirrors the real sensor: a power-on event re-arms settle gating,
        // so readings are invalid until the test scripts a new state.
        scriptInvalid();
    }

private:
    bool valid_ = false;    ///< construction state: not yet valid
    bool present_ = false;  ///< meaningful only while valid_
};

#endif /* WATERINGSYSTEM_SENSORS_TESTING_MOCKLEVELSENSOR_H */
