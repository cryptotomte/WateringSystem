// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file IWaterPump.h
 * @brief Interface for water pump control in the WateringSystem.
 *
 * Ported from the frozen Arduino firmware (include/actuators/IWaterPump.h)
 * with a tightened contract (see specs/002-pump-gpio-board/contracts/
 * iwaterpump.md): no indefinite runs, no silent clamping, no restart of a
 * running pump's clock, observable stop reasons.
 *
 * Part of the header-only `interfaces` component: no IDF includes allowed.
 */

#ifndef WATERINGSYSTEM_INTERFACES_IWATERPUMP_H
#define WATERINGSYSTEM_INTERFACES_IWATERPUMP_H

#include <cstdint>

#include "interfaces/IActuator.h"

/**
 * @brief Why a pump last transitioned to the stopped state.
 */
enum class StopReason {
    None,             ///< Never stopped since boot (no run completed yet)
    Commanded,        ///< Explicit stop() call
    DurationElapsed,  ///< Timed run completed normally
    MaxRuntimeForced, ///< Hard max-runtime cap enforced (logged as an error)
};

/**
 * @brief Water pump control: timed runs with hard max-runtime enforcement.
 */
class IWaterPump : public IActuator {
public:
    /**
     * @brief Start a timed run.
     *
     * Contract:
     *  - durationS <= 0          -> rejected (false): no indefinite runs
     *  - durationS > max runtime -> rejected (false): no silent clamping
     *  - already running         -> rejected (false): clock NOT restarted
     *  - success                 -> output ON exactly once, true returned
     *
     * Rejected calls cause no output change and no state change.
     *
     * @param durationS Requested run duration in seconds.
     * @return true if the run was started, false if rejected.
     */
    virtual bool runFor(int durationS) = 0;

    /**
     * @brief Stop the pump.
     *
     * Always allowed; stopping a stopped pump is a successful no-op.
     *
     * @return true on success (including the no-op case).
     */
    virtual bool stop() = 0;

    /**
     * @brief Check whether the pump is currently running.
     */
    virtual bool isRunning() const = 0;

    /**
     * @brief Periodic enforcement; call at main-loop cadence (>= 10 Hz).
     *
     * Stops the pump when the requested duration elapses or when the hard
     * max runtime (300 s) is reached. A max-runtime stop is logged at
     * warning/error level and observable via getLastStopReason().
     */
    virtual void update() = 0;

    /**
     * @brief Elapsed time of the current run in milliseconds; 0 when stopped.
     */
    virtual int64_t getCurrentRunTimeMs() const = 0;

    /**
     * @brief Total accumulated run time since boot in milliseconds.
     */
    virtual int64_t getAccumulatedRunTimeMs() const = 0;

    /**
     * @brief Reason for the most recent stop (StopReason::None if never run).
     */
    virtual StopReason getLastStopReason() const = 0;
};

#endif /* WATERINGSYSTEM_INTERFACES_IWATERPUMP_H */
