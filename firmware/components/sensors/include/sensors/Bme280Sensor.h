// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file Bme280Sensor.h
 * @brief Pure C++ BME280 driver logic over an injected II2cBus.
 *
 * Ported from the frozen Arduino firmware (src/sensors/BME280Sensor.cpp,
 * read-only reference), replacing the Adafruit library with the Bosch
 * BME280 datasheet reference compensation (research.md R1). Registers,
 * sampling profile, calibration layout and error codes are normative in
 * specs/005-bme280-i2c/data-model.md.
 *
 * This class holds ALL policy — 0x76/0x77 probing order, chip-ID
 * verification, calibration readout/parsing (incl. the split-nibble
 * dig_H4/dig_H5), the exact legacy sampling profile, compensation math,
 * unit conversion, the NaN check, error codes, lazy re-initialization and
 * uninitialize-on-bus-error recovery. It contains NO hardware access —
 * every bus transaction goes through the injected II2cBus, so all of it is
 * compiled and unit-tested on the IDF linux preview target against
 * MockI2cBus (constitution II).
 *
 * Concurrency: unsynchronized by design (host-testable); cross-task
 * consumers (sensor task + console REPL) wrap it in
 * LockedEnvironmentalSensor and access it only through the wrapper.
 */

#ifndef WATERINGSYSTEM_SENSORS_BME280SENSOR_H
#define WATERINGSYSTEM_SENSORS_BME280SENSOR_H

#include <cstddef>
#include <cstdint>
#include <limits>

#include "interfaces/IEnvironmentalSensor.h"
#include "interfaces/II2cBus.h"

/**
 * @brief IEnvironmentalSensor over the BME280 I2C register map.
 *
 * One read() = one 8-byte burst transaction (0xF7–0xFE), compensated
 * T → P → H via t_fine (Bosch int32/int64 reference algorithms) and
 * converted to °C/%RH/hPa atomically: on any failure the last-good getter
 * values remain untouched and getLastError() carries the cause (0/1/2,
 * data-model.md).
 */
class Bme280Sensor : public IEnvironmentalSensor {
public:
    /// Probing order: 0x76 first, then 0x77 (data-model.md; the bench rig
    /// module is strapped to 0x77, the greenhouse legacy unit too).
    static constexpr uint8_t kPrimaryAddress = 0x76;
    static constexpr uint8_t kSecondaryAddress = 0x77;

    /// Chip identity: register 0xD0 must read 0x60 (BME280 — a BMP280
    /// would report 0x58 and is rejected: it has no humidity path).
    static constexpr uint8_t kChipId = 0x60;

    /**
     * @brief Bosch trimming parameters (datasheet table "compensation
     * parameter storage"), parsed little-endian from calib00–25 (0x88–0xA1)
     * and calib26–32 (0xE1–0xE7).
     *
     * Public together with the static compensate*() functions so the host
     * test suite can drive the integer reference paths directly with fixed
     * calibration vectors (research.md R8).
     */
    struct Calibration {
        uint16_t digT1;
        int16_t digT2;
        int16_t digT3;
        uint16_t digP1;
        int16_t digP2;
        int16_t digP3;
        int16_t digP4;
        int16_t digP5;
        int16_t digP6;
        int16_t digP7;
        int16_t digP8;
        int16_t digP9;
        uint8_t digH1;
        int16_t digH2;
        uint8_t digH3;
        int16_t digH4;  ///< 12-bit signed, split-nibble (0xE4 / 0xE5[3:0])
        int16_t digH5;  ///< 12-bit signed, split-nibble (0xE5[7:4] / 0xE6)
        int8_t digH6;
    };

    /**
     * @brief Construct the sensor over an injected I2C bus.
     *
     * @param bus I2C master used for every transaction; must outlive this
     *            object (same injection style as ModbusSoilSensor's
     *            IModbusClient).
     */
    explicit Bme280Sensor(II2cBus& bus);

    ~Bme280Sensor() override = default;

    Bme280Sensor(const Bme280Sensor&) = delete;
    Bme280Sensor& operator=(const Bme280Sensor&) = delete;

    // IEnvironmentalSensor
    bool initialize() override;
    bool read() override;
    bool isAvailable() override;
    int getLastError() override;

    float getTemperature() override;
    float getHumidity() override;
    float getPressure() override;

    // Bosch BME280 datasheet reference compensation (section "Compensation
    // formulas", 32/64-bit integer implementations) — transcribed exactly;
    // host-tested against reference vectors (research.md R8). Static and
    // public so the vector tests exercise the integer paths before the
    // float conversion.

    /**
     * @brief BME280_compensate_T_int32: temperature in 0.01 °C (output
     * 5123 = 51.23 °C). Writes t_fine, the shared fine-resolution
     * temperature input of the P and H compensation — temperature MUST be
     * compensated first each cycle.
     */
    static int32_t compensateTemperature(int32_t adcT, const Calibration& cal,
                                         int32_t& tFine);

    /**
     * @brief BME280_compensate_P_int64: pressure in Pa as unsigned Q24.8
     * (output 24674867 = 24674867/256 = 96386.2 Pa). Returns 0 when the
     * calibration would divide by zero (datasheet reference behavior).
     */
    static uint32_t compensatePressure(int32_t adcP, const Calibration& cal,
                                       int32_t tFine);

    /**
     * @brief bme280_compensate_H_int32: humidity in %RH as unsigned Q22.10
     * (output 47445 = 47445/1024 = 46.333 %RH), clamped to 0–100 %RH by the
     * reference algorithm itself.
     */
    static uint32_t compensateHumidity(int32_t adcH, const Calibration& cal,
                                       int32_t tFine);

private:
    // Register map (used subset, data-model.md).
    static constexpr uint8_t kRegChipId = 0xD0;
    static constexpr uint8_t kRegCalib00 = 0x88;  ///< calib00–25 block start
    static constexpr size_t kCalib00Len = 26;     ///< 0x88–0xA1
    static constexpr uint8_t kRegCalib26 = 0xE1;  ///< calib26–32 block start
    static constexpr size_t kCalib26Len = 7;      ///< 0xE1–0xE7
    static constexpr uint8_t kRegCtrlHum = 0xF2;
    static constexpr uint8_t kRegCtrlMeas = 0xF4;
    static constexpr uint8_t kRegConfig = 0xF5;
    static constexpr uint8_t kRegData = 0xF7;  ///< press/temp/hum burst start
    static constexpr size_t kDataLen = 8;      ///< 0xF7–0xFE

    // Parity sampling profile (legacy src/sensors/BME280Sensor.cpp:41-46,
    // docs/parity-checklist.md §5; byte encodings verified against the
    // Bosch datasheet register tables — field breakdowns at the write site
    // in Bme280Sensor.cpp).
    static constexpr uint8_t kCtrlHumValue = 0x01;   ///< osrs_h ×1
    static constexpr uint8_t kCtrlMeasValue = 0x57;  ///< T×2, P×16, NORMAL
    static constexpr uint8_t kConfigValue = 0x90;    ///< 500 ms, IIR ×16

    /// Probe 0x76 → 0x77 and verify the chip identity; sets address_.
    bool probeAndIdentify();

    /// Burst-read and parse both calibration blocks into cal_.
    bool readCalibration();

    /// Write the parity sampling profile (ctrl_hum → ctrl_meas → config).
    bool writeSamplingProfile();

    II2cBus& bus_;
    uint8_t address_ = 0;  ///< resolved device address (valid when initialized_)
    bool initialized_ = false;
    /// WARN once per consecutive not-found run (lazy re-init retries every
    /// poll); repeats are demoted to debug. Reset on successful init.
    bool initFailureLogged_ = false;
    int lastError_ = 0;
    Calibration cal_{};

    // Last-good reading (published only by a fully successful read()).
    // NaN until the first successful read — self-announcing for consumers
    // that forget to gate on read()/getLastError(); physically plausible
    // 0.0 placeholders would silently pass for real readings.
    float temperature_ = std::numeric_limits<float>::quiet_NaN();
    float humidity_ = std::numeric_limits<float>::quiet_NaN();
    float pressure_ = std::numeric_limits<float>::quiet_NaN();
};

#endif /* WATERINGSYSTEM_SENSORS_BME280SENSOR_H */
