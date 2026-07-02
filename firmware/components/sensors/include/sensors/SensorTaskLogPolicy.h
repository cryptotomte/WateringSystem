// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file SensorTaskLogPolicy.h
 * @brief Pure log-decision policy of the environmental sensor task
 * (header-only).
 *
 * Extracted from main/sensor_task.cpp so the logging discipline required by
 * specs/005-bme280-i2c/contracts/interfaces.md ("Sensor task") is
 * host-testable instead of review-verified: WARN once on the valid→invalid
 * transition and once on recovery, repeated failures at a bounded cadence
 * (every kFailureLogInterval-th consecutive failure), INFO readings on
 * success. The policy decides WHAT to log; the task owns the ESP_LOG calls
 * and the 5 s cadence. It lives in the sensors component (not main/)
 * because the host test app builds components only; it stays free of
 * IDF/FreeRTOS includes for the same reason. PR-11's controller may absorb
 * it together with the task cadence.
 *
 * The policy has no terminal state — a poll loop driving it never runs out
 * of decisions, matching the task contract (never exits, never reboots).
 */

#ifndef WATERINGSYSTEM_SENSORS_SENSORTASKLOGPOLICY_H
#define WATERINGSYSTEM_SENSORS_SENSORTASKLOGPOLICY_H

#include <cstdint>

/**
 * @brief Maps each poll's read() result to the log action the sensor task
 * must take.
 *
 * Starts in the "valid" state so the FIRST failure (e.g. booting with no
 * sensor attached) yields the transition warning exactly once. Feed every
 * read() result to onReadResult(), in order.
 */
class SensorTaskLogPolicy {
public:
    /// What the task logs for one poll result.
    enum class Event {
        Reading,           ///< success, steady state: INFO reading only
        Recovery,          ///< success after failure(s): WARN recovery, then
                           ///< the INFO reading (values are fresh again)
        FailureTransition, ///< first failure after valid: WARN exactly once
        RepeatedFailure,   ///< bounded repeat WARN while the failure persists
        Silent,            ///< failure within the bounded cadence: log nothing
    };

    /// Repeated-failure log cadence: every Nth consecutive failure
    /// (12 × 5 s ≈ once a minute) to avoid log flood (research.md R7).
    static constexpr uint32_t kFailureLogInterval = 12;

    /**
     * @brief Record one poll result and decide the log action.
     *
     * Failure events fire on consecutive failures 1 (FailureTransition),
     * then kFailureLogInterval, 2×kFailureLogInterval, ... (RepeatedFailure);
     * everything in between is Silent. The first success after any failure
     * run is Recovery, further successes are Reading.
     */
    Event onReadResult(bool readOk)
    {
        if (readOk) {
            consecutiveFailures_ = 0;
            if (!wasValid_) {
                wasValid_ = true;
                return Event::Recovery;
            }
            return Event::Reading;
        }
        ++consecutiveFailures_;
        if (wasValid_) {
            wasValid_ = false;
            return Event::FailureTransition;
        }
        if (consecutiveFailures_ % kFailureLogInterval == 0) {
            return Event::RepeatedFailure;
        }
        return Event::Silent;
    }

    /// Consecutive failures since the last success (for the repeat WARN).
    uint32_t consecutiveFailures() const { return consecutiveFailures_; }

private:
    bool wasValid_ = true;
    uint32_t consecutiveFailures_ = 0;
};

#endif /* WATERINGSYSTEM_SENSORS_SENSORTASKLOGPOLICY_H */
