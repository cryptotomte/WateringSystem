// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file IEnvironmentalSensor.h
 * @brief Interface for the BME280 environmental sensor (T/RH/P).
 *
 * Ported from the frozen Arduino firmware
 * (include/sensors/IEnvironmentalSensor.h) in the soil-sensor style: no
 * ISensor base class, no getName() (research.md R5). Normative contract:
 * specs/005-bme280-i2c/contracts/interfaces.md; registers, sampling profile
 * and error codes: specs/005-bme280-i2c/data-model.md.
 *
 * Deliberate divergences from the legacy driver (parity-checklist §6):
 * 0x76/0x77 address probing with a chip-identity check (legacy hard-codes
 * 0x77, no identity check); last-good getter values after a failed read
 * (legacy left NaN in the getters); isAvailable() as a real bus probe
 * (legacy cached available-after-init forever).
 *
 * Validity contract (FR-001/FR-007): a false return from read() means the
 * data is invalid. The getters keep returning the last-good values, so
 * consumers MUST gate on the read result / getLastError() — never on value
 * plausibility.
 *
 * Concurrency: implementations are unsynchronized by design; cross-task
 * consumers (sensor task + console REPL) wrap them in the
 * LockedEnvironmentalSensor decorator, same pattern as LockedSoilSensor.
 *
 * Part of the header-only `interfaces` component: no IDF includes allowed.
 */

#ifndef WATERINGSYSTEM_INTERFACES_IENVIRONMENTALSENSOR_H
#define WATERINGSYSTEM_INTERFACES_IENVIRONMENTALSENSOR_H

/**
 * @brief Environmental sensor: atomic T/RH/P snapshots with lazy recovery.
 *
 * Error codes reported by getLastError()
 * (specs/005-bme280-i2c/data-model.md):
 *
 *   0  OK — last operation succeeded
 *   1  sensor not found — no device ACK on 0x76/0x77, or a responding
 *      device failed the chip-identity check
 *   2  read failed — bus/communication error during a data read, a
 *      mid-initialization bus failure after a device identified
 *      (calibration readout / sampling-profile write), or the
 *      compensation produced NaN
 */
class IEnvironmentalSensor {
public:
    virtual ~IEnvironmentalSensor() = default;

    /**
     * @brief Find and configure the sensor.
     *
     * Probes address 0x76 then 0x77, verifies the chip identity (register
     * 0xD0 == 0x60), reads the calibration data and writes the parity
     * sampling profile (ctrl_hum → ctrl_meas → config, data-model.md).
     * Returns false with error 1 when no BME280 is found, and false with
     * error 2 when a device identified but the calibration readout or
     * sampling-profile write failed mid-initialization (the driver stays
     * uninitialized, so the next attempt re-probes from scratch).
     * Idempotent, and lazy-capable: read()/isAvailable() attempt
     * initialization themselves when it has not happened yet — calling
     * this first is recommended, not required (parity).
     */
    virtual bool initialize() = 0;

    /**
     * @brief Take ONE atomic snapshot of temperature, humidity and pressure.
     *
     * Burst-reads the data registers 0xF7–0xFE in one bus transaction and
     * compensates T → P → H (t_fine ordering). Atomic: either all three
     * getters are refreshed, or the call fails (error 1 when the lazy
     * (re-)initialization finds no sensor, error 2 on a bus error or NaN
     * during the data read — a sensor unplugged mid-run reports error 2 on
     * the failing read, then error 1 from the NEXT read's re-probe) and
     * the last-good values remain untouched. A bus error marks the driver
     * uninitialized, so the next call re-probes both addresses (recovery,
     * FR-004). Exactly one bus attempt, no retry — recovery comes from the
     * caller's poll cadence.
     *
     * @return true if a fully valid reading was taken.
     */
    virtual bool read() = 0;

    /**
     * @brief Probe sensor presence with a REAL chip-ID read (FR-009).
     *
     * Every call performs an actual bus transaction — never cached state
     * (deliberate divergence from the legacy cached availability). The
     * probe itself does not modify getLastError(); when it triggers a lazy
     * (re-)initialization, that initialize() owns its own error reporting —
     * the ISoilSensor convention. Recovery from earlier failures is
     * implicit: a sensor that identifies itself again is available again.
     *
     * Warning: after isAvailable() detects a loss, getLastError() may
     * still be 0 (or stale) until the next read()/initialize() — consumers
     * must not pair isAvailable() with getLastError().
     */
    virtual bool isAvailable() = 0;

    /**
     * @brief Error code of the most recent initialize()/read() (0 = OK;
     * table in the class comment). The isAvailable() probe never touches
     * this code (its lazy-init path reports through initialize()).
     */
    virtual int getLastError() = 0;

    // Values from the most recent successful read(). NaN until the first
    // successful read() (self-announcing placeholders), and after a failed
    // read() they still hold the previous good reading — consumers gate on
    // the read() result, never on the values.

    /// Temperature in °C.
    virtual float getTemperature() = 0;

    /// Relative humidity in %RH.
    virtual float getHumidity() = 0;

    /// Barometric pressure in hPa (parity: legacy converts Pa → hPa).
    virtual float getPressure() = 0;
};

#endif /* WATERINGSYSTEM_INTERFACES_IENVIRONMENTALSENSOR_H */
