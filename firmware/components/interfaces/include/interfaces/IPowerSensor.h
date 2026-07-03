// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file IPowerSensor.h
 * @brief Interface for the INA226 pump power monitor (bus V / current / power).
 *
 * Raw telemetry only — no protection or alarm logic rides on these values
 * (out of scope by the master PRD; threshold logic is PR-14+). Normative
 * contract: specs/006-level-sensors-ina226/contracts/interfaces.md;
 * registers and scaling: specs/006-level-sensors-ina226/data-model.md.
 *
 * Validity contract: identical to the established sensor family
 * (IEnvironmentalSensor/ISoilSensor). A false return from read() means the
 * data is invalid; the getters keep returning the last-good values (NaN
 * before the first success), so consumers MUST gate on the read result /
 * getLastError() — never on value plausibility.
 *
 * Concurrency: implementations are unsynchronized by design; cross-task
 * consumers (console REPL now; web PR-09, controller PR-11) wrap them in
 * the LockedPowerSensor decorator.
 *
 * Part of the header-only `interfaces` component: no IDF includes allowed.
 */

#ifndef WATERINGSYSTEM_INTERFACES_IPOWERSENSOR_H
#define WATERINGSYSTEM_INTERFACES_IPOWERSENSOR_H

/**
 * @brief Power sensor: atomic V/I/P snapshots with lazy recovery.
 *
 * Error codes reported by getLastError() (family convention,
 * specs/006-level-sensors-ina226/data-model.md):
 *
 *   0  OK — last operation succeeded
 *   1  sensor not found — no device ACK at the configured address, or a
 *      responding device failed the identity check (manufacturer/die ID)
 *   2  read/communication failure after the device identified — a bus
 *      error during a data read or a mid-initialization failure
 *      (config/calibration write after identification)
 */
class IPowerSensor {
public:
    virtual ~IPowerSensor() = default;

    /**
     * @brief Find and configure the sensor.
     *
     * Verifies the device identity, then writes the configuration and
     * calibration registers. Returns false with error 1 when no device is
     * found (or the identity check fails), and false with error 2 when a
     * device identified but a subsequent register write failed (the driver
     * stays uninitialized, so the next attempt re-probes from scratch).
     * Idempotent, and lazy-capable: read()/isAvailable() attempt
     * initialization themselves when it has not happened yet — calling
     * this first is recommended, not required (family convention).
     */
    virtual bool initialize() = 0;

    /**
     * @brief Take ONE snapshot: bus voltage, current and power refreshed
     * together.
     *
     * Atomic: either all three getters are refreshed, or the call fails
     * (error 1 when the lazy (re-)initialization finds no sensor, error 2
     * on a bus error during the data reads) and the last-good values
     * remain untouched. A bus error marks the driver uninitialized, so
     * the next call re-probes the identity (recovery). No retries —
     * recovery comes from the caller's poll cadence.
     *
     * @return true if a fully valid reading was taken.
     */
    virtual bool read() = 0;

    /**
     * @brief Probe sensor presence with a REAL identity read.
     *
     * Every call performs an actual bus transaction — never cached state.
     * The probe itself never touches getLastError(); when it triggers a
     * lazy (re-)initialization, that initialize() owns its own error
     * reporting (family convention). Consumers must not pair
     * isAvailable() with getLastError().
     */
    virtual bool isAvailable() = 0;

    /**
     * @brief Error code of the most recent initialize()/read() (0 = OK;
     * table in the class comment). The isAvailable() probe never touches
     * this code.
     */
    virtual int getLastError() = 0;

    // Values from the most recent successful read(). NaN until the first
    // successful read() (self-announcing placeholders), and after a failed
    // read() they still hold the previous good reading — consumers gate on
    // the read() result, never on the values.

    /// Bus voltage in V.
    virtual float getBusVoltage() = 0;

    /// Current in A — SIGNED (reverse current stays negative, never wrapped).
    virtual float getCurrent() = 0;

    /// Power in W.
    virtual float getPower() = 0;
};

#endif /* WATERINGSYSTEM_INTERFACES_IPOWERSENSOR_H */
