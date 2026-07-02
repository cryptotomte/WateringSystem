// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ModbusSoilSensor.h
 * @brief Pure C++ soil sensor logic over an injected IModbusClient.
 *
 * Ported from the frozen Arduino firmware (src/sensors/ModbusSoilSensor.cpp,
 * read-only reference): register map, decode/scaling (incl. signed
 * temperature), fixed parity validation ranges, real-bus-read availability
 * probe and the legacy calibration semantics. Register map, scaling and
 * error codes are normative in specs/004-modbus-soil-sensor/data-model.md.
 *
 * This class contains NO hardware access — every bus transaction goes
 * through the injected IModbusClient, so the decode/validation/calibration
 * logic is compiled and unit-tested on the IDF linux preview target against
 * MockModbusClient (constitution II).
 *
 * Concurrency: unsynchronized by design (host-testable); cross-task
 * consumers (console REPL + future main-loop controller) wrap it in
 * LockedSoilSensor and access it only through the wrapper.
 */

#ifndef WATERINGSYSTEM_SENSORS_MODBUSSOILSENSOR_H
#define WATERINGSYSTEM_SENSORS_MODBUSSOILSENSOR_H

#include <cstdint>

#include "interfaces/IModbusClient.h"
#include "interfaces/ISoilSensor.h"

/**
 * @brief ISoilSensor over Modbus RTU holding registers (data-model.md).
 *
 * One read() = one 9-register transaction (0x0000–0x0008), decoded, scaled
 * and range-validated atomically: on any failure the last-good getter
 * values remain untouched and getLastError() carries the cause.
 */
class ModbusSoilSensor : public ISoilSensor {
public:
    /// Parity slave address (docs/parity-checklist.md §5).
    static constexpr uint8_t kDefaultDeviceAddress = 0x01;

    /**
     * @brief Construct the sensor over an injected Modbus client.
     *
     * @param client Modbus master used for every transaction; must outlive
     *               this object (same injection style as WaterPump's
     *               ITimeProvider).
     * @param deviceAddress Modbus slave address of the sensor.
     */
    explicit ModbusSoilSensor(IModbusClient& client,
                              uint8_t deviceAddress = kDefaultDeviceAddress);

    ~ModbusSoilSensor() override = default;

    ModbusSoilSensor(const ModbusSoilSensor&) = delete;
    ModbusSoilSensor& operator=(const ModbusSoilSensor&) = delete;

    // ISoilSensor
    bool initialize() override;
    bool read() override;
    bool isAvailable() override;
    int getLastError() override;

    float getMoisture() override;
    float getTemperature() override;
    float getHumidity() override;
    float getPH() override;
    float getEC() override;
    float getNitrogen() override;
    float getPhosphorus() override;
    float getPotassium() override;

    // Calibration (legacy-exact port, research.md R8); host-tested in
    // test_soil_sensor.cpp (calibration suite).
    bool calibrateMoisture(float referenceValue) override;
    bool calibratePH(float referenceValue) override;
    bool calibrateEC(float referenceValue) override;

private:
    // Register map (data-model.md; legacy include/sensors/ModbusSoilSensor.h).
    static constexpr uint16_t kRegHumidity = 0x0000;     ///< 0.1 % (= moisture)
    static constexpr uint16_t kRegTemperature = 0x0001;  ///< 0.1 °C, signed
    static constexpr uint16_t kRegEc = 0x0002;           ///< 1 µS/cm
    static constexpr uint16_t kRegPh = 0x0003;           ///< 0.1 pH
    static constexpr uint16_t kRegNitrogen = 0x0004;     ///< 1 mg/kg
    static constexpr uint16_t kRegPhosphorus = 0x0005;   ///< 1 mg/kg
    static constexpr uint16_t kRegPotassium = 0x0006;    ///< 1 mg/kg
    // 0x0007 (salinity) and 0x0008 (TDS) are read but not exposed (parity).

    /// Calibration factor registers (best-effort writes, factor ×100).
    static constexpr uint16_t kRegMoistureCalib = 0x0100;
    static constexpr uint16_t kRegPhCalib = 0x0101;
    static constexpr uint16_t kRegEcCalib = 0x0102;

    /// One transaction covers registers 0x0000–0x0008.
    static constexpr uint16_t kReadRegisterCount = 9;

    // Fixed parity validation ranges (the legacy setValidRange defaults;
    // runtime range changes were trimmed — see interfaces/ISoilSensor.h).
    static constexpr float kMoistureMin = 0.0f;
    static constexpr float kMoistureMax = 100.0f;
    static constexpr float kTemperatureMin = -40.0f;
    static constexpr float kTemperatureMax = 80.0f;
    static constexpr float kPhMin = 3.0f;
    static constexpr float kPhMax = 9.0f;

    /**
     * @brief Shared legacy calibration flow (legacy :207-322, three
     * near-identical bodies folded into one).
     *
     * Reads one raw register, computes factor = reference / (raw / rawScale),
     * stores it into @p factor and best-effort writes factor ×100 to
     * @p calibRegister (a failed write is NON-FATAL: logged, lastError set,
     * call still succeeds — parity).
     */
    bool calibrate(uint16_t rawRegister, float rawScale,
                   uint16_t calibRegister, float& factor,
                   float referenceValue, const char* quantity);

    IModbusClient& client_;
    uint8_t deviceAddress_;
    bool initialized_ = false;
    int lastError_ = 0;

    // Last-good reading (published only by a fully successful read()).
    float moisture_ = 0.0f;
    float temperature_ = 0.0f;
    float humidity_ = 0.0f;
    float ph_ = 0.0f;
    float ec_ = 0.0f;
    float nitrogen_ = 0.0f;
    float phosphorus_ = 0.0f;
    float potassium_ = 0.0f;

    // Calibration factors (RAM-only for now; persistence wired in
    // PR-09/PR-11).
    float moistureCalibrationFactor_ = 1.0f;
    float phCalibrationFactor_ = 1.0f;
    float ecCalibrationFactor_ = 1.0f;
};

#endif /* WATERINGSYSTEM_SENSORS_MODBUSSOILSENSOR_H */
