// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ISoilSensor.h
 * @brief Interface for the RS485 Modbus soil sensor (7 quantities).
 *
 * Ported from the frozen Arduino firmware (include/sensors/ISoilSensor.h)
 * with one deliberate trim: legacy setValidRange/isWithinValidRange are NOT
 * ported — validation ranges are the fixed parity constants (moisture
 * 0–100 %, temperature −40–80 °C, pH 3–9) and no caller in the legacy
 * firmware ever changed them at runtime. Normative contract:
 * specs/004-modbus-soil-sensor/contracts/interfaces.md; register map,
 * scaling and error codes: specs/004-modbus-soil-sensor/data-model.md.
 *
 * Validity contract (FR-005): a false return from read() means the data is
 * invalid. The getters keep returning the last-good values, so consumers
 * MUST gate on the read result / getLastError() — never on value
 * plausibility.
 *
 * Concurrency: implementations are unsynchronized by design; cross-task
 * consumers (main loop + console REPL) wrap them in the LockedSoilSensor
 * decorator, same pattern as LockedWaterPump/LockedConfigStore.
 *
 * Part of the header-only `interfaces` component: no IDF includes allowed.
 */

#ifndef WATERINGSYSTEM_INTERFACES_ISOILSENSOR_H
#define WATERINGSYSTEM_INTERFACES_ISOILSENSOR_H

/**
 * @brief Soil sensor: atomic multi-quantity reads with parity validation.
 */
class ISoilSensor {
public:
    virtual ~ISoilSensor() = default;

    /**
     * @brief Prepare the sensor (brings up the underlying Modbus client if
     * needed). Must precede read()/isAvailable().
     */
    virtual bool initialize() = 0;

    /**
     * @brief Read all quantities in ONE 9-register transaction
     * (registers 0x0000–0x0008), single bus attempt, no retry.
     *
     * The reading is atomic: either every value is decoded, scaled and
     * range-validated and the getters are refreshed, or the call fails and
     * the last-good getter values remain untouched. false means the data is
     * INVALID (bus error, timeout, exception or range validation failure —
     * see getLastError()); consumers gate on this result, never on how
     * plausible the getter values look.
     *
     * @return true if a fully valid reading was taken.
     */
    virtual bool read() = 0;

    /**
     * @brief Probe sensor presence with a REAL 1-register bus read (parity).
     *
     * Every call performs an actual bus transaction — never a cached or
     * derived state. Recovery from earlier failures is implicit: a sensor
     * that responds again is available again.
     */
    virtual bool isAvailable() = 0;

    /**
     * @brief Error code of the most recent operation (0 = OK).
     *
     * Same table as IModbusClient plus code 5 (range validation failed);
     * see specs/004-modbus-soil-sensor/data-model.md.
     */
    virtual int getLastError() = 0;

    // Values from the most recent successful read(). After a failed read()
    // they still hold the previous good reading — check the read() result
    // before treating them as fresh.

    /// Soil moisture in percent (0–100).
    virtual float getMoisture() = 0;

    /// Soil temperature in °C, signed (−40–80).
    virtual float getTemperature() = 0;

    /**
     * @brief Humidity in percent — identical to getMoisture() (parity).
     *
     * The sensor reports a single moisture/humidity quantity in register
     * 0x0000; the legacy firmware exposed it under both names.
     */
    virtual float getHumidity() = 0;

    /// Soil pH (validated 3–9).
    virtual float getPH() = 0;

    /// Electrical conductivity in µS/cm (not range-enforced, parity).
    virtual float getEC() = 0;

    /// Nitrogen content in mg/kg (not range-enforced, parity).
    virtual float getNitrogen() = 0;

    /// Phosphorus content in mg/kg (not range-enforced, parity).
    virtual float getPhosphorus() = 0;

    /// Potassium content in mg/kg (not range-enforced, parity).
    virtual float getPotassium() = 0;

    // Calibration (CP1 decision A — legacy semantics). Each call computes a
    // local correction factor (reference / current raw reading), applies it
    // to every subsequent read, and best-effort writes the factor to the
    // sensor's calibration register (0x0100/0x0101/0x0102) — a failed
    // sensor-register write is NON-FATAL and does not fail the call.
    // Factors are RAM-only in this PR (persistence arrives in PR-09/PR-11).

    /// Calibrate moisture against a reference value in percent.
    virtual bool calibrateMoisture(float referenceValue) = 0;

    /// Calibrate pH against a reference value.
    virtual bool calibratePH(float referenceValue) = 0;

    /// Calibrate EC against a reference value in µS/cm.
    virtual bool calibrateEC(float referenceValue) = 0;
};

#endif /* WATERINGSYSTEM_INTERFACES_ISOILSENSOR_H */
