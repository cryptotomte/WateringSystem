// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file Bme280Sensor.cpp
 * @brief BME280 probe/calibration/compensation logic (pure C++).
 *
 * Uses ESP_LOG only — the log component is simulated on the IDF linux
 * preview target, so this file builds and runs in the host test suite
 * (same rule as sensors/ModbusSoilSensor.cpp).
 *
 * Port reference: src/sensors/BME280Sensor.cpp (frozen Arduino firmware,
 * read-only). Legacy behaviors ported: lazy (re-)initialization from
 * read()/isAvailable(), sampling profile (NORMAL, T×2/P×16/H×1, IIR ×16,
 * standby 500 ms), Pa → hPa conversion, NaN-fails-read (error 2), error
 * codes 1 (not found) / 2 (read failed). The Adafruit library is replaced
 * by the Bosch datasheet reference compensation, transcribed exactly
 * (research.md R1/R8).
 *
 * Deliberate divergences (specs/005-bme280-i2c/contracts/interfaces.md,
 * parity-checklist §6 candidates): 0x76/0x77 probing + chip-ID check
 * (legacy hard-codes 0x77, no identity check); last-good getter values
 * after a failed read (legacy left NaN in the getters); isAvailable() as a
 * real chip-ID probe (legacy cached available-after-init forever);
 * bus-error recovery re-probes BOTH addresses (covers a module replugged
 * at the other address).
 */

#include "sensors/Bme280Sensor.h"

#include <cmath>

#include "esp_log.h"

static const char *TAG = "bme280";

namespace {

/// Little-endian 16-bit assembly from two calibration bytes.
uint16_t le16(uint8_t lsb, uint8_t msb)
{
    return static_cast<uint16_t>(static_cast<uint16_t>(msb) << 8 | lsb);
}

}  // namespace

Bme280Sensor::Bme280Sensor(II2cBus& bus)
    : bus_(bus)
{
}

bool Bme280Sensor::initialize()
{
    // Idempotent, like the legacy sensor (:30-32).
    if (initialized_) {
        return true;
    }

    // No BME280 answers (or only wrong-identity devices answer): error 1,
    // "sensor not found" (legacy error 1).
    if (!probeAndIdentify()) {
        lastError_ = 1;
        ESP_LOGW(TAG, "initialize failed: no BME280 found (0x%02x/0x%02x)",
                 kPrimaryAddress, kSecondaryAddress);
        return false;
    }

    // A device that identified as a BME280 but then fails mid-init is a
    // bus/communication failure, not "not found": error 2. The driver
    // stays uninitialized, so the next attempt re-probes from scratch —
    // calibration is re-read on EVERY (re-)initialization because a
    // re-attached module may be a different physical sensor
    // (data-model.md).
    if (!readCalibration()) {
        lastError_ = 2;
        ESP_LOGW(TAG, "initialize failed: calibration readout error at 0x%02x",
                 address_);
        return false;
    }

    if (!writeSamplingProfile()) {
        lastError_ = 2;
        ESP_LOGW(TAG, "initialize failed: sampling profile write error at "
                 "0x%02x", address_);
        return false;
    }

    initialized_ = true;
    lastError_ = 0;
    ESP_LOGI(TAG, "BME280 initialized at 0x%02x (NORMAL, T x2 / P x16 / "
             "H x1, IIR x16, standby 500 ms)", address_);
    return true;
}

bool Bme280Sensor::probeAndIdentify()
{
    // Probing policy (data-model.md): 0x76 first, then 0x77; the first
    // address that ACKs AND identifies as a BME280 wins. A device that
    // ACKs but reports a foreign chip ID is logged distinctly and skipped
    // (US3 — e.g. a BMP280 or another part strapped to the same address).
    static constexpr uint8_t kCandidates[] = {kPrimaryAddress,
                                              kSecondaryAddress};
    for (const uint8_t candidate : kCandidates) {
        if (!bus_.probe(candidate)) {
            continue;
        }
        uint8_t chipId = 0;
        if (!bus_.readRegisters(candidate, kRegChipId, &chipId, 1)) {
            ESP_LOGW(TAG, "device at 0x%02x ACKed but chip-ID read failed",
                     candidate);
            continue;
        }
        if (chipId != kChipId) {
            ESP_LOGW(TAG, "device at 0x%02x is not a BME280 (chip ID 0x%02x, "
                     "expected 0x%02x) — rejected", candidate, chipId,
                     kChipId);
            continue;
        }
        address_ = candidate;
        return true;
    }
    return false;
}

bool Bme280Sensor::readCalibration()
{
    // Two burst reads cover all trimming parameters (datasheet memory map):
    // calib00–25 (0x88–0xA1) and calib26–32 (0xE1–0xE7). All multi-byte
    // parameters are little-endian; dig_H4/dig_H5 are 12-bit signed values
    // sharing the split-nibble register 0xE5.
    uint8_t block1[kCalib00Len] = {};
    uint8_t block2[kCalib26Len] = {};
    if (!bus_.readRegisters(address_, kRegCalib00, block1, kCalib00Len) ||
        !bus_.readRegisters(address_, kRegCalib26, block2, kCalib26Len)) {
        return false;
    }

    cal_.digT1 = le16(block1[0], block1[1]);
    cal_.digT2 = static_cast<int16_t>(le16(block1[2], block1[3]));
    cal_.digT3 = static_cast<int16_t>(le16(block1[4], block1[5]));
    cal_.digP1 = le16(block1[6], block1[7]);
    cal_.digP2 = static_cast<int16_t>(le16(block1[8], block1[9]));
    cal_.digP3 = static_cast<int16_t>(le16(block1[10], block1[11]));
    cal_.digP4 = static_cast<int16_t>(le16(block1[12], block1[13]));
    cal_.digP5 = static_cast<int16_t>(le16(block1[14], block1[15]));
    cal_.digP6 = static_cast<int16_t>(le16(block1[16], block1[17]));
    cal_.digP7 = static_cast<int16_t>(le16(block1[18], block1[19]));
    cal_.digP8 = static_cast<int16_t>(le16(block1[20], block1[21]));
    cal_.digP9 = static_cast<int16_t>(le16(block1[22], block1[23]));
    // block1[24] is calib24 (0xA0), unused padding in the map.
    cal_.digH1 = block1[25];

    cal_.digH2 = static_cast<int16_t>(le16(block2[0], block2[1]));
    cal_.digH3 = block2[2];
    // dig_H4: bits [11:4] in 0xE4, bits [3:0] in 0xE5[3:0]; dig_H5: bits
    // [3:0] in 0xE5[7:4], bits [11:4] in 0xE6. Sign extension of the
    // 12-bit values comes from the int8_t cast of the top byte before the
    // ×16 shift (Bosch reference driver parsing).
    cal_.digH4 = static_cast<int16_t>(
        static_cast<int16_t>(static_cast<int8_t>(block2[3])) * 16 |
        static_cast<int16_t>(block2[4] & 0x0F));
    cal_.digH5 = static_cast<int16_t>(
        static_cast<int16_t>(static_cast<int8_t>(block2[5])) * 16 |
        static_cast<int16_t>(block2[4] >> 4));
    cal_.digH6 = static_cast<int8_t>(block2[6]);
    return true;
}

bool Bme280Sensor::writeSamplingProfile()
{
    // Parity sampling profile (data-model.md; legacy
    // src/sensors/BME280Sensor.cpp:41-46). Byte encodings verified against
    // the Bosch BME280 datasheet register tables ("ctrl_hum", "ctrl_meas",
    // "config"). Order matters: the datasheet requires ctrl_hum to be
    // written BEFORE ctrl_meas — changes to ctrl_hum take effect only
    // after a write to ctrl_meas.
    //
    //   ctrl_hum  (0xF2) = 0x01: osrs_h[2:0] = 001 (humidity oversampling ×1)
    //   ctrl_meas (0xF4) = 0x57: osrs_t[7:5] = 010 (temperature ×2),
    //                            osrs_p[4:2] = 101 (pressure ×16),
    //                            mode[1:0]   = 11  (NORMAL)
    //                            → 010'101'11 = 0x57
    //   config    (0xF5) = 0x90: t_sb[7:5]   = 100 (standby 500 ms),
    //                            filter[4:2] = 100 (IIR ×16),
    //                            spi3w_en[0] = 0
    //                            → 100'100'00 = 0x90
    return bus_.writeRegister(address_, kRegCtrlHum, kCtrlHumValue) &&
           bus_.writeRegister(address_, kRegCtrlMeas, kCtrlMeasValue) &&
           bus_.writeRegister(address_, kRegConfig, kConfigValue);
}

bool Bme280Sensor::read()
{
    // Lazy (re-)initialization (legacy :55-59). A failure here is already
    // logged inside initialize() and lastError_ is set there (1 or 2).
    if (!initialized_ && !initialize()) {
        return false;
    }

    // One 8-byte burst 0xF7–0xFE, single bus attempt (the chip shadows the
    // data registers during a burst, so the raw values form one consistent
    // measurement).
    uint8_t raw[kDataLen] = {};
    if (!bus_.readRegisters(address_, kRegData, raw, kDataLen)) {
        // Bus error: error 2 AND back to UNINITIALIZED so the next call
        // re-probes both addresses (FR-004 recovery; the last-good getter
        // values remain untouched).
        lastError_ = 2;
        initialized_ = false;
        ESP_LOGW(TAG, "read failed: bus error at 0x%02x — sensor lost, "
                 "will re-probe", address_);
        return false;
    }

    // Raw ADC assembly (datasheet "raw data readout"): pressure and
    // temperature are 20-bit (msb, lsb, xlsb[7:4]), humidity is 16-bit.
    const int32_t adcP = static_cast<int32_t>(raw[0]) << 12 |
                         static_cast<int32_t>(raw[1]) << 4 |
                         static_cast<int32_t>(raw[2]) >> 4;
    const int32_t adcT = static_cast<int32_t>(raw[3]) << 12 |
                         static_cast<int32_t>(raw[4]) << 4 |
                         static_cast<int32_t>(raw[5]) >> 4;
    const int32_t adcH = static_cast<int32_t>(raw[6]) << 8 |
                         static_cast<int32_t>(raw[7]);

    // Compensate T first — t_fine feeds P and H (data-model.md).
    int32_t tFine = 0;
    const int32_t t = compensateTemperature(adcT, cal_, tFine);
    const uint32_t p = compensatePressure(adcP, cal_, tFine);
    const uint32_t h = compensateHumidity(adcH, cal_, tFine);

    // Unit conversion (data-model.md): T 0.01 °C → °C; P Q24.8 Pa → Pa →
    // hPa (parity: legacy converts Pa → hPa); H Q22.10 → %RH.
    const float temperature = static_cast<float>(t) / 100.0f;
    const float pressure = static_cast<float>(p) / 256.0f / 100.0f;
    const float humidity = static_cast<float>(h) / 1024.0f;

    // Parity NaN check (legacy :66-69): a NaN fails the whole read with
    // error 2. Unreachable with the integer compensation paths above, but
    // it is the binding legacy contract and stays as a safety net. Not a
    // bus loss — the driver stays initialized.
    if (std::isnan(temperature) || std::isnan(humidity) ||
        std::isnan(pressure)) {
        lastError_ = 2;
        ESP_LOGW(TAG, "read failed: compensation produced NaN");
        return false;
    }

    // Publish atomically w.r.t. this object: plain members are fine —
    // cross-task exclusion is LockedEnvironmentalSensor's job.
    temperature_ = temperature;
    humidity_ = humidity;
    pressure_ = pressure;

    lastError_ = 0;
    return true;
}

bool Bme280Sensor::isAvailable()
{
    // Lazy initialization doubles as the probe (parity, legacy :75-80).
    // Note on the error contract: the availability PROBE below never
    // touches lastError_, but this lazy path delegates to initialize(),
    // which owns its own error reporting (1/2 on failure, 0 on success) —
    // exactly the ModbusSoilSensor::isAvailable() convention the interface
    // contract refers to.
    if (!initialized_) {
        return initialize();
    }

    // REAL chip-ID read on every call, never cached (FR-009 — deliberate
    // divergence from the legacy cached availability). Does not touch
    // lastError_ — read() owns the reading's error state.
    uint8_t chipId = 0;
    const bool available =
        bus_.readRegisters(address_, kRegChipId, &chipId, 1) &&
        chipId == kChipId;
    if (!available) {
        // Loss detected: back to UNINITIALIZED so the next call re-probes
        // both addresses (same recovery path as a failed read()).
        initialized_ = false;
        ESP_LOGW(TAG, "availability probe failed at 0x%02x — sensor lost, "
                 "will re-probe", address_);
    }
    return available;
}

int Bme280Sensor::getLastError()
{
    return lastError_;
}

float Bme280Sensor::getTemperature()
{
    return temperature_;
}

float Bme280Sensor::getHumidity()
{
    return humidity_;
}

float Bme280Sensor::getPressure()
{
    return pressure_;
}

// ---------------------------------------------------------------------------
// Bosch BME280 datasheet reference compensation, transcribed exactly from
// the datasheet's 32/64-bit integer routines (BME280_compensate_T_int32,
// BME280_compensate_P_int64, bme280_compensate_H_int32). Do not "clean up"
// these expressions: bit-exact equivalence with the reference is the
// binding requirement (FR-005/SC-002), verified by host-test vectors
// (research.md R8).
// ---------------------------------------------------------------------------

int32_t Bme280Sensor::compensateTemperature(int32_t adcT,
                                            const Calibration& cal,
                                            int32_t& tFine)
{
    const int32_t var1 =
        (((adcT >> 3) - (static_cast<int32_t>(cal.digT1) << 1)) *
         static_cast<int32_t>(cal.digT2)) >> 11;
    const int32_t var2 =
        (((((adcT >> 4) - static_cast<int32_t>(cal.digT1)) *
           ((adcT >> 4) - static_cast<int32_t>(cal.digT1))) >> 12) *
         static_cast<int32_t>(cal.digT3)) >> 14;
    tFine = var1 + var2;
    // Resolution 0.01 °C: output 5123 = 51.23 °C.
    return (tFine * 5 + 128) >> 8;
}

uint32_t Bme280Sensor::compensatePressure(int32_t adcP,
                                          const Calibration& cal,
                                          int32_t tFine)
{
    int64_t var1 = static_cast<int64_t>(tFine) - 128000;
    int64_t var2 = var1 * var1 * static_cast<int64_t>(cal.digP6);
    var2 = var2 + ((var1 * static_cast<int64_t>(cal.digP5)) << 17);
    var2 = var2 + (static_cast<int64_t>(cal.digP4) << 35);
    var1 = ((var1 * var1 * static_cast<int64_t>(cal.digP3)) >> 8) +
           ((var1 * static_cast<int64_t>(cal.digP2)) << 12);
    var1 = (((static_cast<int64_t>(1) << 47) + var1) *
            static_cast<int64_t>(cal.digP1)) >> 33;
    if (var1 == 0) {
        // Avoid division by zero (datasheet reference behavior).
        return 0;
    }
    int64_t p = 1048576 - adcP;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (static_cast<int64_t>(cal.digP9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (static_cast<int64_t>(cal.digP8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (static_cast<int64_t>(cal.digP7) << 4);
    // Unsigned Q24.8 Pa: output 24674867 = 96386.2 Pa.
    return static_cast<uint32_t>(p);
}

uint32_t Bme280Sensor::compensateHumidity(int32_t adcH,
                                          const Calibration& cal,
                                          int32_t tFine)
{
    int32_t v = tFine - static_cast<int32_t>(76800);
    v = ((((adcH << 14) - (static_cast<int32_t>(cal.digH4) << 20) -
           (static_cast<int32_t>(cal.digH5) * v)) +
          static_cast<int32_t>(16384)) >> 15) *
        (((((((v * static_cast<int32_t>(cal.digH6)) >> 10) *
             (((v * static_cast<int32_t>(cal.digH3)) >> 11) +
              static_cast<int32_t>(32768))) >> 10) +
           static_cast<int32_t>(2097152)) *
              static_cast<int32_t>(cal.digH2) +
          8192) >> 14);
    v = v - (((((v >> 15) * (v >> 15)) >> 7) *
              static_cast<int32_t>(cal.digH1)) >> 4);
    v = (v < 0) ? 0 : v;
    v = (v > 419430400) ? 419430400 : v;
    // Unsigned Q22.10 %RH: output 47445 = 46.333 %RH.
    return static_cast<uint32_t>(v >> 12);
}
