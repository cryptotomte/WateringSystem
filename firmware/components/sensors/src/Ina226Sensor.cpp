// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file Ina226Sensor.cpp
 * @brief INA226 identity/configuration/scaling logic (pure C++).
 *
 * Uses ESP_LOG only — the log component is simulated on the IDF linux
 * preview target, so this file builds and runs in the host test suite
 * (same rule as sensors/Bme280Sensor.cpp). On device targets it is built
 * only when CONFIG_BOARD_REV2 (sensors/CMakeLists.txt, FR-011).
 *
 * Architecture is the proven Bme280Sensor pattern (research.md R7): lazy
 * (re-)initialization from read()/isAvailable(), identity check at init,
 * error codes 1 (not found / identity mismatch) / 2 (communication failure
 * after identification), last-good getter values with NaN placeholders,
 * and uninitialize-on-bus-error → identity re-probe recovery.
 *
 * Register field values are derived from the TI INA226 datasheet, SBOS547
 * (Configuration Register 00h, Bus Voltage Register 02h, Power Register
 * 03h, Current Register 04h, Calibration Register 05h, Manufacturer ID FEh,
 * Die ID FFh). TODO(PR-14): confirm the configuration value against live
 * hardware at rev2 bring-up — no INA226 exists on the rev1 bench rig, so
 * this driver is host-verified only until then.
 */

#include "sensors/Ina226Sensor.h"

#include "esp_log.h"

static const char *TAG = "ina226";

// ---------------------------------------------------------------------------
// Configuration register (00h) value 0x4527, derived from the SBOS547
// field layout (verify on hardware at PR-14):
//
//   bit  15     RST    = 0    normal operation (no reset)
//   bits 14:12  —      = 100  reserved; bit 14 reads 1 at POR (POR value
//                             0x4127) and is written back unchanged
//   bits 11:9   AVG    = 010  averaging: 16 samples
//   bits 8:6    VBUSCT = 100  bus-voltage conversion time 1.1 ms
//   bits 5:3    VSHCT  = 100  shunt-voltage conversion time 1.1 ms
//   bits 2:0    MODE   = 111  shunt and bus, continuous
//
//   0b0100'0101'0010'0111 = 0x4527
//   cross-check: 0x4000 | (0b010 << 9) | (0b100 << 6) | (0b100 << 3) | 0b111
//              = 0x4000 | 0x0400 | 0x0100 | 0x0020 | 0x0007 = 0x4527
//
// One averaged result every 16 × (1.1 + 1.1) ms ≈ 35 ms — smooth pump
// telemetry with negligible lag for the on-demand console/API reads this
// PR and PR-09 need (no periodic telemetry task in this PR).
// ---------------------------------------------------------------------------

Ina226Sensor::Ina226Sensor(II2cBus& bus, uint8_t address,
                           uint32_t shuntMilliOhm)
    : bus_(bus),
      address_(address),
      calibration_(calibrationFor(shuntMilliOhm))
{
}

bool Ina226Sensor::initialize()
{
    // Idempotent (family convention, Bme280Sensor pattern).
    if (initialized_) {
        return true;
    }

    // No device ACKs at the configured address, or a responding device is
    // not an INA226: error 1, "sensor not found". WARN only once per
    // consecutive failure run — the lazy re-init retries on every consumer
    // attempt and a permanently absent sensor must not flood the log.
    // Repeats go to debug; the flag resets on successful initialization.
    if (!probeAndIdentify()) {
        lastError_ = 1;
        if (!initFailureLogged_) {
            initFailureLogged_ = true;
            ESP_LOGW(TAG, "initialize failed: no INA226 found at 0x%02x",
                     address_);
        } else {
            ESP_LOGD(TAG, "initialize failed: no INA226 found at 0x%02x",
                     address_);
        }
        return false;
    }

    // A device that identified as an INA226 but then fails mid-init is a
    // bus/communication failure, not "not found": error 2. The driver
    // stays uninitialized, so the next attempt re-probes the identity from
    // scratch. Order matters for the host-test byte assertions: config
    // first, then calibration (both 16-bit big-endian, one transaction
    // each — writeRegister16, the seam extension this feature added).
    if (!bus_.writeRegister16(address_, kRegConfig, kConfigValue) ||
        !bus_.writeRegister16(address_, kRegCalibration, calibration_)) {
        lastError_ = 2;
        ESP_LOGW(TAG, "initialize failed: config/calibration write error "
                 "at 0x%02x", address_);
        return false;
    }

    initialized_ = true;
    lastError_ = 0;
    // Recovery re-arms the once-per-run not-found WARN (the INFO line
    // below announces the recovery itself).
    initFailureLogged_ = false;
    ESP_LOGI(TAG, "INA226 initialized at 0x%02x (AVG x16, 1.1 ms "
             "conversions, continuous shunt+bus, CAL=%u)",
             address_, static_cast<unsigned>(calibration_));
    return true;
}

bool Ina226Sensor::probeAndIdentify()
{
    if (!bus_.probe(address_)) {
        return false;
    }
    // Identity check (FR-009, mirrors the BME280 chip-ID pattern): both
    // the manufacturer ID and the die ID must match. A device that ACKs
    // but cannot deliver its identity is treated as not-found — it never
    // identified, so error 2 ("after identification") does not apply.
    uint16_t manufacturer = 0;
    uint16_t die = 0;
    if (!bus_.readRegister16(address_, kRegManufacturerId, manufacturer) ||
        !bus_.readRegister16(address_, kRegDieId, die)) {
        ESP_LOGW(TAG, "device at 0x%02x ACKed but identity read failed",
                 address_);
        return false;
    }
    if (manufacturer != kManufacturerId || die != kDieId) {
        ESP_LOGW(TAG, "device at 0x%02x is not an INA226 (manufacturer "
                 "0x%04x, die 0x%04x; expected 0x%04x/0x%04x) — rejected",
                 address_, static_cast<unsigned>(manufacturer),
                 static_cast<unsigned>(die),
                 static_cast<unsigned>(kManufacturerId),
                 static_cast<unsigned>(kDieId));
        return false;
    }
    return true;
}

bool Ina226Sensor::read()
{
    // Lazy (re-)initialization (family convention). A failure here is
    // already logged inside initialize() and lastError_ is set there.
    if (!initialized_ && !initialize()) {
        return false;
    }

    // One snapshot: all three registers are fetched BEFORE any value is
    // published, so a mid-read failure leaves the last-good triple fully
    // intact — never a fresh voltage next to a stale power. The device
    // converts continuously (MODE 0b111); these reads return its latest
    // completed averaged conversion set.
    uint16_t rawBus = 0;
    uint16_t rawPower = 0;
    uint16_t rawCurrent = 0;
    if (!bus_.readRegister16(address_, kRegBusVoltage, rawBus) ||
        !bus_.readRegister16(address_, kRegPower, rawPower) ||
        !bus_.readRegister16(address_, kRegCurrent, rawCurrent)) {
        // Bus error: error 2 AND back to UNINITIALIZED so the next call
        // re-probes the identity (recovery; the last-good getter values
        // remain untouched).
        lastError_ = 2;
        initialized_ = false;
        ESP_LOGW(TAG, "read failed: bus error at 0x%02x — sensor lost, "
                 "will re-probe", address_);
        return false;
    }

    // Scaling (SBOS547, data-model.md):
    //   bus voltage: unsigned, fixed 1.25 mV/LSB
    //   current:     SIGNED two's complement × Current_LSB — the int16_t
    //                cast preserves the sign (reverse current stays
    //                negative, never wrapped)
    //   power:       unsigned, 25 × Current_LSB per LSB
    busVoltage_ = static_cast<float>(rawBus) * kBusVoltageLsbV;
    current_ =
        static_cast<float>(static_cast<int16_t>(rawCurrent)) * kCurrentLsbA;
    power_ = static_cast<float>(rawPower) * kPowerLsbW;

    lastError_ = 0;
    return true;
}

bool Ina226Sensor::isAvailable()
{
    // Lazy initialization doubles as the probe. Note on the error
    // contract: the availability PROBE below never touches lastError_,
    // but this lazy path delegates to initialize(), which owns its own
    // error reporting (1/2 on failure, 0 on success) — the family
    // convention the interface contract refers to.
    if (!initialized_) {
        return initialize();
    }

    // REAL identity read on every call, never cached (family convention —
    // a dead sensor must not report alive forever). Does not touch
    // lastError_ — read() owns the reading's error state.
    uint16_t manufacturer = 0;
    uint16_t die = 0;
    const bool available =
        bus_.readRegister16(address_, kRegManufacturerId, manufacturer) &&
        bus_.readRegister16(address_, kRegDieId, die) &&
        manufacturer == kManufacturerId && die == kDieId;
    if (!available) {
        // Loss detected: back to UNINITIALIZED so the next call re-probes
        // (same recovery path as a failed read()).
        initialized_ = false;
        ESP_LOGW(TAG, "availability probe failed at 0x%02x — sensor lost, "
                 "will re-probe", address_);
    }
    return available;
}

int Ina226Sensor::getLastError()
{
    return lastError_;
}

float Ina226Sensor::getBusVoltage()
{
    return busVoltage_;
}

float Ina226Sensor::getCurrent()
{
    return current_;
}

float Ina226Sensor::getPower()
{
    return power_;
}
