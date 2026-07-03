// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ILevelSensor.h
 * @brief Interface for one XKC-Y26 reservoir water-level mark (low or high).
 *
 * One instance per level mark (research.md R1): the low mark and the high
 * mark are two independent ILevelSensor objects, so PR-11's watering
 * controller composes its truth table (incl. the invalid combination) from
 * two sensors instead of an aggregate baked into this layer. Normative
 * contract: specs/006-level-sensors-ina226/contracts/interfaces.md; the
 * settle/warm-up/tracking state machine:
 * specs/006-level-sensors-ina226/data-model.md.
 *
 * Deliberate divergences from the frozen legacy firmware
 * (docs/parity-checklist.md §6): stability-window debounce (legacy reads
 * bare pins every loop pass) and an explicit not-yet-valid state during
 * settle gating and debounce warm-up (legacy has no validity concept).
 *
 * Validity contract: isWaterPresent() is meaningful ONLY while isValid()
 * returns true. Consumers gate on validity — PR-11's truth table treats an
 * invalid sensor as "do not act", never as "water absent".
 *
 * Concurrency: implementations are unsynchronized by design; cross-task
 * consumers (main-loop update() + console REPL, controller in PR-11) wrap
 * them in the LockedLevelSensor decorator, same pattern as the other
 * Locked* wrappers.
 *
 * Part of the header-only `interfaces` component: no IDF includes allowed.
 */

#ifndef WATERINGSYSTEM_INTERFACES_ILEVELSENSOR_H
#define WATERINGSYSTEM_INTERFACES_ILEVELSENSOR_H

/**
 * @brief Polled, debounced, polarity-absorbed water-level state with
 * explicit validity.
 *
 * Fail direction (pinned by host tests, docs/parity-checklist.md line 97):
 * a disconnected input is pulled HIGH by the board's pull-up, so rev1
 * (active HIGH) reads "water present" — the fill pump stays off — while
 * rev2 (active LOW, 2N7002 inverter) reads "water absent" — the drawing
 * node does not pump. Both directions fail safe for their pump topology.
 */
class ILevelSensor {
public:
    virtual ~ILevelSensor() = default;

    /**
     * @brief Sample the raw input once and advance the settle/debounce
     * state machine.
     *
     * Called by the owner at its polling cadence (the 10 Hz main loop).
     * No update ⇒ no state change: validity and the reported state are
     * functions of the update stream, never of wall-clock reads alone —
     * time passing without update() calls changes nothing.
     */
    virtual void update() = 0;

    /**
     * @brief Whether isWaterPresent() currently carries meaning.
     *
     * False during settle gating (after construction or notifyPowerOn(),
     * FW-3) and during debounce warm-up (until the raw input has held one
     * value for a full stability window). Consumers MUST gate on this —
     * "not yet valid" is a distinct state, never to be conflated with
     * wet or dry (SC-005/FR-012).
     */
    virtual bool isValid() = 0;

    /**
     * @brief Logical water state: true = water present at this mark.
     *
     * Polarity is fully absorbed here via board configuration (FW-5:
     * rev1 GPIO active HIGH, rev2 active LOW) — no consumer ever sees a
     * raw-polarity value except through rawState(). Debounced: the value
     * changes only after the raw input has been stable in the new state
     * for the board's debounce window; any flip restarts the window.
     * Meaningful ONLY while isValid() returns true (returns false before
     * the first stable window, by definition without meaning).
     */
    virtual bool isWaterPresent() = 0;

    /**
     * @brief Raw, undebounced pin level from the most recent update()
     * (true = electrically HIGH). Diagnostics only — consumers use
     * isWaterPresent().
     */
    virtual bool rawState() = 0;

    /**
     * @brief Re-arm settle gating after a sensor-rail power-on (FW-3).
     *
     * From any state the sensor returns to settling: readings are invalid
     * again until the settle window (rev2 ≥500 ms; rev1 0 ms) has elapsed
     * AND a fresh debounce warm-up completes. Rail *control* itself is
     * PR-14 (CP1 decision A) — in this feature app_main calls this once at
     * boot (the rail is on by default at power-up on rev2).
     */
    virtual void notifyPowerOn() = 0;
};

#endif /* WATERINGSYSTEM_INTERFACES_ILEVELSENSOR_H */
