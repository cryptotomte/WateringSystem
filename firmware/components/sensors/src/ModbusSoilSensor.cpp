// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ModbusSoilSensor.cpp
 * @brief Soil sensor decode/validation/calibration logic (pure C++).
 *
 * Uses ESP_LOG only — the log component is simulated on the IDF linux
 * preview target, so this file builds and runs in the host test suite
 * (same rule as actuators/WaterPump.cpp).
 *
 * Port reference: src/sensors/ModbusSoilSensor.cpp (frozen Arduino
 * firmware, read-only). Legacy behaviors confirmed and ported exactly:
 *
 *  - The moisture calibration factor is NOT applied in read(): legacy
 *    computes/stores/writes it in calibrateMoisture() but read() publishes
 *    humidity = raw / 10 with the comment "No calibration factor for
 *    humidity yet", and moisture = humidity. Ported as-is (research.md R8
 *    "moisture factor exists in legacy and is ported as-is"; the data-model
 *    table's "× moisture factor" describes the factor's existence, not a
 *    legacy read-path multiplication).
 *  - pH and EC factors ARE applied in read(), and validation runs on the
 *    FACTORED values (legacy :103-128 validates after multiplying).
 *  - Validation covers moisture, temperature and pH only; EC/N/P/K are not
 *    range-enforced in read() (parity — legacy checks exactly moisture,
 *    temperature, humidity, ph; the humidity check is dropped here because
 *    humidity ≡ moisture makes it redundant).
 *  - Lazy initialization: read()/isAvailable()/calibrate*() attempt
 *    initialize() when not yet initialized (legacy :75-81, :134-137).
 *  - The availability/initialization probe reads ONE register at 0x0000
 *    (legacy REG_HUMIDITY, :65 and :140) — not 0x0001.
 *
 * Error codes follow the new normative table (data-model.md) instead of the
 * legacy ad-hoc codes 4/6..14: client failures propagate the client's error
 * (2/3/100+n), range validation failure is 5 (parity), and the legacy
 * "raw value too low for calibration" (7/10/13) maps onto 5 as well.
 */

#include "sensors/ModbusSoilSensor.h"

#include <cmath>

#include "esp_log.h"

static const char *TAG = "soilsensor";

ModbusSoilSensor::ModbusSoilSensor(IModbusClient& client, uint8_t deviceAddress)
    : client_(client),
      deviceAddress_(deviceAddress)
{
}

bool ModbusSoilSensor::initialize()
{
    // Idempotent, like the legacy sensor (:49-51).
    if (initialized_) {
        return true;
    }

    if (!client_.initialize()) {
        // Legacy code 2 ("Modbus client initialization failed") — coincides
        // with the normative table's bus/communication error.
        lastError_ = 2;
        ESP_LOGE(TAG, "initialize failed: Modbus client init error");
        return false;
    }

    // Verify the sensor answers: one real register read (legacy :63-68;
    // legacy hardcoded error 3, here the client's actual error code — a
    // silent sensor reports 3 = timeout either way).
    uint16_t testRegister = 0;
    if (!client_.readHoldingRegisters(deviceAddress_, kRegHumidity, 1,
                                      &testRegister)) {
        lastError_ = client_.getLastError();
        ESP_LOGW(TAG, "initialize failed: sensor probe error %d", lastError_);
        return false;
    }

    initialized_ = true;
    lastError_ = 0;
    return true;
}

bool ModbusSoilSensor::read()
{
    // Lazy initialization (legacy :77-81). A failure here is already
    // logged inside initialize() (both exits) and lastError_ is set there.
    if (!initialized_ && !initialize()) {
        lastReadOk_ = false;  // read outcome for snapshot()
        return false;
    }

    // One 9-register transaction, single bus attempt (FR-004/parity).
    uint16_t registerValues[kReadRegisterCount] = {};
    if (!client_.readHoldingRegisters(deviceAddress_, kRegHumidity,
                                      kReadRegisterCount, registerValues)) {
        // Client failure: propagate the client's error code (legacy used
        // the ad-hoc code 4) and leave the last-good values untouched.
        lastError_ = client_.getLastError();
        ESP_LOGW(TAG, "read failed: bus error %d", lastError_);
        lastReadOk_ = false;
        return false;
    }

    // Decode + scale into locals first — the members are published only
    // after validation passes (all-or-nothing, FR-005).
    //
    // 0x0000 humidity/moisture 0.1 % — NO moisture calibration factor in
    // the read path (legacy parity, see file header).
    const float humidity = static_cast<float>(registerValues[0]) / 10.0f;
    const float moisture = humidity;
    // 0x0001 temperature 0.1 °C, SIGNED 16-bit (legacy :98-99).
    const float temperature =
        static_cast<float>(static_cast<int16_t>(registerValues[1])) / 10.0f;
    // 0x0002 EC 1 µS/cm, calibration factor applied (legacy :102-103).
    const float ec =
        static_cast<float>(registerValues[2]) * ecCalibrationFactor_;
    // 0x0003 pH 0.1, calibration factor applied (legacy :106-107).
    const float ph =
        (static_cast<float>(registerValues[3]) / 10.0f) * phCalibrationFactor_;
    // 0x0004–0x0006 N/P/K 1 mg/kg, unscaled.
    const float nitrogen = static_cast<float>(registerValues[4]);
    const float phosphorus = static_cast<float>(registerValues[5]);
    const float potassium = static_cast<float>(registerValues[6]);
    // 0x0007 salinity and 0x0008 TDS are read but not exposed (parity).

    // Validate AFTER decode/scaling, on the factored values (legacy
    // :121-128 order). Failure publishes nothing.
    if (moisture < kMoistureMin || moisture > kMoistureMax ||
        temperature < kTemperatureMin || temperature > kTemperatureMax ||
        ph < kPhMin || ph > kPhMax) {
        lastError_ = 5;  // Range validation failed (parity error 5).
        ESP_LOGW(TAG,
                 "read failed: range validation (moisture=%.1f temp=%.1f "
                 "ph=%.1f)",
                 static_cast<double>(moisture),
                 static_cast<double>(temperature), static_cast<double>(ph));
        lastReadOk_ = false;
        return false;
    }

    // Publish atomically w.r.t. this object: plain members are fine —
    // cross-task exclusion is LockedSoilSensor's job.
    humidity_ = humidity;
    moisture_ = moisture;
    temperature_ = temperature;
    ec_ = ec;
    ph_ = ph;
    nitrogen_ = nitrogen;
    phosphorus_ = phosphorus;
    potassium_ = potassium;

    lastError_ = 0;
    lastReadOk_ = true;
    hasEverReadOk_ = true;
    return true;
}

SoilSnapshot ModbusSoilSensor::snapshot()
{
    // Cached-only, NO bus I/O (QUIRK 5): report the read history and the
    // last-good values. available = at least one successful read on record.
    SoilSnapshot s;
    s.readOk = lastReadOk_;
    s.available = hasEverReadOk_;
    s.lastError = lastError_;
    s.moisture = moisture_;
    s.temperature = temperature_;
    s.humidity = humidity_;
    s.ph = ph_;
    s.ec = ec_;
    s.nitrogen = nitrogen_;
    s.phosphorus = phosphorus_;
    s.potassium = potassium_;
    return s;
}

bool ModbusSoilSensor::isAvailable()
{
    // Lazy initialization doubles as the probe (legacy :136-138).
    if (!initialized_) {
        return initialize();
    }

    // REAL 1-register bus read on every call, never cached (parity,
    // legacy :140 — register 0x0000). Legacy passed nullptr as the buffer;
    // the new IModbusClient contract requires a caller-owned buffer.
    uint16_t testRegister = 0;
    const bool available = client_.readHoldingRegisters(
        deviceAddress_, kRegHumidity, 1, &testRegister);
    if (!available) {
        // Probe failure is not latched (next call probes again) and does
        // not touch lastError_ — read() owns the reading's error state.
        ESP_LOGW(TAG, "availability probe failed: error %d",
                 client_.getLastError());
    }
    return available;
}

int ModbusSoilSensor::getLastError()
{
    return lastError_;
}

float ModbusSoilSensor::getMoisture()
{
    return moisture_;
}

float ModbusSoilSensor::getTemperature()
{
    return temperature_;
}

float ModbusSoilSensor::getHumidity()
{
    // Humidity ≡ moisture for this sensor (register 0x0000, parity).
    return humidity_;
}

float ModbusSoilSensor::getPH()
{
    return ph_;
}

float ModbusSoilSensor::getEC()
{
    return ec_;
}

float ModbusSoilSensor::getNitrogen()
{
    return nitrogen_;
}

float ModbusSoilSensor::getPhosphorus()
{
    return phosphorus_;
}

float ModbusSoilSensor::getPotassium()
{
    return potassium_;
}

// Calibration — faithful port of legacy :207-322 (three identical flows
// folded into one helper). Host-tested in test_soil_sensor.cpp (calibration
// suite).
bool ModbusSoilSensor::calibrate(uint16_t rawRegister, float rawScale,
                                 uint16_t calibRegister, float& factor,
                                 float referenceValue, const char* quantity)
{
    // Input hygiene, not parity: legacy accepted any float here. A NaN/inf
    // or non-positive reference would poison the factor (and NaN silently
    // passes every < comparison below), so reject it up front.
    if (!std::isfinite(referenceValue) || referenceValue <= 0.0f) {
        lastError_ = 5;
        ESP_LOGW(TAG, "calibrate %s failed: invalid reference %.3f",
                 quantity, static_cast<double>(referenceValue));
        return false;
    }

    // Lazy initialization (legacy :209-213).
    if (!initialized_ && !initialize()) {
        return false;
    }

    // Fresh raw reading of the single quantity register (legacy errors
    // 6/9/12 — here the client's actual error code).
    uint16_t rawRegisterValue = 0;
    if (!client_.readHoldingRegisters(deviceAddress_, rawRegister, 1,
                                      &rawRegisterValue)) {
        lastError_ = client_.getLastError();
        ESP_LOGW(TAG, "calibrate %s failed: raw read error %d", quantity,
                 lastError_);
        return false;
    }

    const float currentRawValue =
        static_cast<float>(rawRegisterValue) / rawScale;

    // Avoid division by zero (legacy :226-229; legacy codes 7/10/13 map
    // onto the normative range-validation code 5).
    if (currentRawValue < 0.01f) {
        lastError_ = 5;
        ESP_LOGW(TAG, "calibrate %s failed: raw value too low (%.3f)",
                 quantity, static_cast<double>(currentRawValue));
        return false;
    }

    const float newFactor = referenceValue / currentRawValue;

    // Input hygiene, not parity: legacy static_cast this product straight to
    // uint16_t (UB for values outside 0..65535). A factor that large is
    // physically absurd, so reject it BEFORE it is stored or encoded (the
    // reference guard above makes negative factors unreachable).
    if (newFactor * 100.0f > 65535.0f) {
        lastError_ = 5;
        ESP_LOGW(TAG, "calibrate %s failed: factor %.3f out of encodable "
                 "range", quantity, static_cast<double>(newFactor));
        return false;
    }

    factor = newFactor;

    // Best-effort factor write (×100) to the sensor's calibration register
    // — NON-FATAL on failure: the factor is still used locally (parity,
    // legacy :234-241; legacy codes 8/11/14 map onto the client's error).
    const uint16_t calibFactorRegValue =
        static_cast<uint16_t>(factor * 100.0f);
    if (!client_.writeSingleRegister(deviceAddress_, calibRegister,
                                     calibFactorRegValue)) {
        lastError_ = client_.getLastError();
        ESP_LOGW(TAG,
                 "calibrate %s: sensor register write failed (error %d), "
                 "factor %.3f kept locally",
                 quantity, lastError_, static_cast<double>(factor));
    } else {
        lastError_ = 0;
    }

    ESP_LOGI(TAG, "calibrate %s: factor=%.3f", quantity,
             static_cast<double>(factor));
    return true;
}

bool ModbusSoilSensor::calibrateMoisture(float referenceValue)
{
    return calibrate(kRegHumidity, 10.0f, kRegMoistureCalib,
                     moistureCalibrationFactor_, referenceValue, "moisture");
}

bool ModbusSoilSensor::calibratePH(float referenceValue)
{
    return calibrate(kRegPh, 10.0f, kRegPhCalib, phCalibrationFactor_,
                     referenceValue, "ph");
}

bool ModbusSoilSensor::calibrateEC(float referenceValue)
{
    // EC raw value is unscaled (legacy :300-301) — rawScale 1.
    return calibrate(kRegEc, 1.0f, kRegEcCalib, ecCalibrationFactor_,
                     referenceValue, "ec");
}
