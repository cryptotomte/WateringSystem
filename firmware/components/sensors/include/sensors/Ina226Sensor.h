// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file Ina226Sensor.h
 * @brief Pure C++ INA226 driver logic over an injected II2cBus.
 *
 * New capability — no Arduino counterpart (docs/parity-checklist.md §9).
 * Registers, scaling and error codes are normative in
 * specs/006-level-sensors-ina226/data-model.md; field values are derived
 * from the TI INA226 datasheet (SBOS547) — see the config-value breakdown
 * in Ina226Sensor.cpp.
 *
 * This class holds ALL policy — identity verification (manufacturer/die
 * ID, mirroring the BME280 chip-ID pattern), configuration + calibration
 * writes, register scaling incl. the signed current, error codes, lazy
 * re-initialization and uninitialize-on-bus-error recovery. It contains NO
 * hardware access — every bus transaction goes through the injected
 * II2cBus, so all of it is compiled and unit-tested on the IDF linux
 * preview target against MockI2cBus (constitution II). On device targets
 * the file is built only for INA226-equipped boards (CONFIG_BOARD_REV2,
 * sensors/CMakeLists.txt) — the rev1 binary contains no INA226 code
 * (FR-011).
 *
 * Concurrency: unsynchronized by design (host-testable); cross-task
 * consumers (console REPL now; web PR-09, controller PR-11) wrap it in
 * LockedPowerSensor and access it only through the wrapper.
 */

#ifndef WATERINGSYSTEM_SENSORS_INA226SENSOR_H
#define WATERINGSYSTEM_SENSORS_INA226SENSOR_H

#include <cstdint>
#include <limits>

#include "interfaces/II2cBus.h"
#include "interfaces/IPowerSensor.h"

/**
 * @brief IPowerSensor over the INA226 register map.
 *
 * One read() = one snapshot: bus voltage, power and current registers are
 * fetched together (the device converts continuously — MODE 0b111) and
 * published atomically w.r.t. this object: on any failure the last-good
 * getter values remain untouched and getLastError() carries the cause
 * (0/1/2, data-model.md).
 */
class Ina226Sensor : public IPowerSensor {
public:
    /// Identity registers (0xFE/0xFF) must read exactly these values —
    /// a responding foreign device is rejected with error 1 (FR-009).
    static constexpr uint16_t kManufacturerId = 0x5449;  ///< "TI" in ASCII
    static constexpr uint16_t kDieId = 0x2260;           ///< INA226 die/rev

    /// Fixed current resolution (research.md R7/R9): 0.5 mA/LSB. Chosen
    /// for the rev2 operating point — full scale ±0.5 mA × 2^15 ≈ ±16.4 A
    /// on the 5 mΩ shunt, ample for a ~4 A pump. Compile-time constant;
    /// promote to Kconfig only if a real need appears.
    static constexpr float kCurrentLsbA = 0.0005f;

    /// Bus-voltage register resolution, fixed by the device: 1.25 mV/LSB
    /// (SBOS547, Bus Voltage Register).
    static constexpr float kBusVoltageLsbV = 0.00125f;

    /// Power register resolution, fixed by the device at 25 × Current_LSB
    /// (SBOS547, Power Register) = 12.5 mW/LSB here.
    static constexpr float kPowerLsbW = 25.0f * kCurrentLsbA;

    /**
     * @brief Calibration register value for a given shunt (data-model.md).
     *
     * CAL = 0.00512 / (Current_LSB × R_shunt)
     *     = 0.00512 / (0.0005 A × R_mΩ / 1000)  =  10240 / R_mΩ
     *
     * = 2048 at the default 5 mΩ (the rev2 operating point, pinned by host
     * test). Integer division truncates for non-divisor shunt values —
     * a ≤0.05 % calibration bias at worst within the 1–1000 mΩ Kconfig
     * range, negligible against the 1 % shunt tolerance. Public static so
     * the host tests assert the operating point directly.
     *
     * @param shuntMilliOhm Shunt resistance in mΩ (Kconfig range-checks
     *        1..1000; a raw 0 is clamped defensively — never a division
     *        by zero).
     */
    static constexpr uint16_t calibrationFor(uint32_t shuntMilliOhm)
    {
        return static_cast<uint16_t>(
            10240u / (shuntMilliOhm == 0 ? 1u : shuntMilliOhm));
    }

    /**
     * @brief Construct the sensor over an injected I2C bus.
     *
     * @param bus I2C master used for every transaction; must outlive this
     *            object. MUST be the shared bus instance (the one the
     *            BME280 uses) — never a second bus on the same pins
     *            (bus-sharing contract, FR-008).
     * @param address 7-bit device address (BOARD_INA226_ADDR, 0x40 on
     *            rev2: A0 = A1 = GND).
     * @param shuntMilliOhm Shunt resistance in mΩ
     *            (CONFIG_WS_INA226_SHUNT_MILLIOHM, default 5).
     */
    Ina226Sensor(II2cBus& bus, uint8_t address, uint32_t shuntMilliOhm);

    ~Ina226Sensor() override = default;

    Ina226Sensor(const Ina226Sensor&) = delete;
    Ina226Sensor& operator=(const Ina226Sensor&) = delete;

    // IPowerSensor
    bool initialize() override;
    bool read() override;
    bool isAvailable() override;
    int getLastError() override;

    float getBusVoltage() override;
    float getCurrent() override;
    float getPower() override;

private:
    // Register map (used subset, data-model.md). 0x01 (shunt voltage) and
    // 0x06/0x07 (Mask/Enable, Alert) are unused — the ALERT pin is not
    // connected on rev2.
    static constexpr uint8_t kRegConfig = 0x00;
    static constexpr uint8_t kRegBusVoltage = 0x02;
    static constexpr uint8_t kRegPower = 0x03;
    static constexpr uint8_t kRegCurrent = 0x04;
    static constexpr uint8_t kRegCalibration = 0x05;
    static constexpr uint8_t kRegManufacturerId = 0xFE;
    static constexpr uint8_t kRegDieId = 0xFF;

    /// Configuration register value: AVG ×16, VBUSCT = VSHCT = 1.1 ms,
    /// MODE = continuous shunt + bus. Bit-field derivation at the write
    /// site in Ina226Sensor.cpp; asserted byte-exact by host tests.
    static constexpr uint16_t kConfigValue = 0x4527;

    /// Probe the address and verify manufacturer + die ID.
    bool probeAndIdentify();

    II2cBus& bus_;
    const uint8_t address_;
    const uint16_t calibration_;  ///< calibrationFor(shunt) from the ctor
    bool initialized_ = false;
    /// WARN once per consecutive not-found run (lazy re-init retries on
    /// every consumer attempt); repeats are demoted to debug. Reset on
    /// successful init. Same policy as Bme280Sensor.
    bool initFailureLogged_ = false;
    int lastError_ = 0;

    // Last-good reading (published only by a fully successful read()).
    // NaN until the first successful read — self-announcing for consumers
    // that forget to gate on read()/getLastError(); physically plausible
    // 0.0 placeholders would silently pass for real readings.
    float busVoltage_ = std::numeric_limits<float>::quiet_NaN();
    float current_ = std::numeric_limits<float>::quiet_NaN();
    float power_ = std::numeric_limits<float>::quiet_NaN();
};

#endif /* WATERINGSYSTEM_SENSORS_INA226SENSOR_H */
