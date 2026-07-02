// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_bme280.cpp
 * @brief Host tests for the Bme280Sensor driver logic (linux target).
 *
 * Tests the REAL probe/calibration/compensation logic (Bme280Sensor) via
 * MockI2cBus (scripted register maps + call recording). Registered via
 * run_bme280_tests() from the shared Unity runner (test_main.cpp); the
 * process exit code equals the failure count and is the CI gate.
 *
 * Coverage maps to tasks.md T006 (US1 happy path: initialization incl. the
 * exact parity config-byte sequence, a reference-vector read,
 * getters-before-first-read), T014 (US2 error paths: absent sensor,
 * mid-read bus error with last-good retention, recovery incl. calibration
 * re-read, sensorless boot), T015 (sensor-task log policy), T017 (US3
 * address variants and chip-ID rejection), T018 (Bosch reference vectors,
 * integer outputs asserted exactly) and T020 (MockEnvironmentalSensor
 * consistency).
 */

#include <cmath>
#include <vector>

#include "unity.h"

#include "sensors/Bme280Sensor.h"
#include "sensors/SensorTaskLogPolicy.h"
#include "sensors/testing/MockEnvironmentalSensor.h"
#include "sensors/testing/MockI2cBus.h"

namespace {

constexpr uint8_t kAddrPrimary = 0x76;
constexpr uint8_t kAddrSecondary = 0x77;  ///< bench rig module strapping

constexpr uint8_t kRegChipId = 0xD0;
constexpr uint8_t kRegCtrlHum = 0xF2;
constexpr uint8_t kRegCtrlMeas = 0xF4;
constexpr uint8_t kRegConfig = 0xF5;
constexpr uint8_t kRegData = 0xF7;

// ---------------------------------------------------------------------------
// Reference vector (research R8). Calibration set: the BMP280 datasheet
// worked example (section "Calculation example": dig_T1..T3, dig_P1..P9;
// adc_T = 519888, adc_P = 415148 → 25.08 °C, 100653 Pa) extended with a
// typical humidity calibration (dig_H1=75, H2=362, H3=0, H4=315, H5=50,
// H6=30; adc_H = 32768). Expected outputs derived by running the Bosch
// datasheet reference routines (int32 T, int64 P, int32 H) over these
// inputs:
//   t_fine   = 128422
//   T int32  = 2508      → 25.08 °C            (matches the datasheet)
//   P Q24.8  = 25767233  → 100653.25 Pa = 1006.5325 hPa  (datasheet
//              double-precision result: 100653.27 Pa)
//   H Q22.10 = 71319     → 69.64746 %RH
// ---------------------------------------------------------------------------

/// calib00–25 (0x88–0xA1), little-endian: T1=27504 T2=26435 T3=-1000
/// P1=36477 P2=-10685 P3=3024 P4=2855 P5=140 P6=-7 P7=15500 P8=-14600
/// P9=6000, pad (0xA0), H1=75.
const std::vector<uint8_t> kCalibBlock1 = {
    0x70, 0x6B, 0x43, 0x67, 0x18, 0xFC,              // dig_T1..T3
    0x7D, 0x8E, 0x43, 0xD6, 0xD0, 0x0B, 0x27, 0x0B,  // dig_P1..P4
    0x8C, 0x00, 0xF9, 0xFF, 0x8C, 0x3C, 0xF8, 0xC6,  // dig_P5..P8
    0x70, 0x17,                                      // dig_P9
    0x00,                                            // 0xA0 (unused pad)
    0x4B,                                            // dig_H1 = 75
};

/// calib26–32 (0xE1–0xE7): H2=362 (0x016A LE), H3=0, H4=315 (0xE4=0x13,
/// 0xE5[3:0]=0xB), H5=50 (0xE5[7:4]=0x2, 0xE6=0x03), H6=30.
const std::vector<uint8_t> kCalibBlock2 = {
    0x6A, 0x01, 0x00, 0x13, 0x2B, 0x03, 0x1E,
};

/// Data registers 0xF7–0xFE: adc_P=415148 (0x655AC), adc_T=519888
/// (0x7EED0), adc_H=32768 (0x8000) — 20-bit P/T use xlsb bits [7:4].
const std::vector<uint8_t> kDataBlock = {
    0x65, 0x5A, 0xC0, 0x7E, 0xED, 0x00, 0x80, 0x00,
};

// Expected compensated floats (integer reference outputs ÷100, ÷256÷100,
// ÷1024 — see the derivation comment above).
constexpr float kExpectedTemperature = 25.08f;
constexpr float kExpectedPressure = 1006.5325f;
constexpr float kExpectedHumidity = 69.64746f;

// ---------------------------------------------------------------------------
// The same reference calibration as a Calibration struct, for driving the
// public static compensate*() integer paths directly (T018 vectors).
// ---------------------------------------------------------------------------
constexpr Bme280Sensor::Calibration kRefCal = {
    27504, 26435, -1000,                                     // dig_T1..T3
    36477, -10685, 3024, 2855, 140, -7, 15500, -14600, 6000, // dig_P1..P9
    75, 362, 0, 315, 50, 30,                                 // dig_H1..H6
};

// Integer reference outputs for the worked-example raws (adc_T=519888,
// adc_P=415148, adc_H=32768) — Bosch datasheet routines over kRefCal:
constexpr int32_t kRefTFine = 128422;
constexpr int32_t kRefTemperatureInt = 2508;     // 0.01 °C
constexpr uint32_t kRefPressureQ248 = 25767233;  // Q24.8 Pa
constexpr uint32_t kRefHumidityQ2210 = 71319;    // Q22.10 %RH

// ---------------------------------------------------------------------------
// Calibration set B — a DIFFERENT physical module for the recovery test
// (analyze finding I1: calibration must be re-read on re-init). Identical
// to the reference set except dig_T2 = 30000 (0x7530 LE at 0x8A/0x8B)
// instead of 26435. Expected outputs derived by running the Bosch
// datasheet reference routines over cal-B with the standard raw block:
//   t_fine = 145791, T = 2847 (28.47 °C), P = 25901067 (1011.7604 hPa),
//   H = 71558 (69.8809 %RH)
// ---------------------------------------------------------------------------
const std::vector<uint8_t> kCalibBlock1B = {
    0x70, 0x6B, 0x30, 0x75, 0x18, 0xFC,              // dig_T1, T2=30000, T3
    0x7D, 0x8E, 0x43, 0xD6, 0xD0, 0x0B, 0x27, 0x0B,  // dig_P1..P4
    0x8C, 0x00, 0xF9, 0xFF, 0x8C, 0x3C, 0xF8, 0xC6,  // dig_P5..P8
    0x70, 0x17,                                      // dig_P9
    0x00,                                            // 0xA0 (unused pad)
    0x4B,                                            // dig_H1 = 75
};

constexpr float kExpectedTemperatureB = 28.47f;    // 2847 ÷ 100
constexpr float kExpectedPressureB = 1011.76043f;  // 25901067 ÷ 256 ÷ 100
constexpr float kExpectedHumidityB = 69.880859f;   // 71558 ÷ 1024

// ---------------------------------------------------------------------------
// Extra raw-data blocks for T014/T018 (encodings: P/T 20-bit msb,lsb,
// xlsb[7:4]; H 16-bit msb,lsb):
//
// Negative-temperature vector: adc_T=409600 (0x64000), adc_P=415148,
// adc_H=32768. Bosch reference over kRefCal:
//   t_fine = -49208, T = -961 (-9.61 °C), P = 24415055 (953.7131 hPa),
//   H = 68674 (67.0645 %RH)
//
// Extreme-but-legal vector: adc_T=0xFFFFF (20-bit max), adc_P=0 (20-bit
// min), adc_H=0xFFFF (16-bit max). Bosch reference over kRefCal:
//   t_fine = 960246, T = 18755 (187.55 °C), P = 55536661 (2169.4008 hPa),
//   H = 102400 (100.000 %RH — the reference algorithm's own upper clamp,
//   419430400 >> 12)
// ---------------------------------------------------------------------------
const std::vector<uint8_t> kDataBlockNegativeTemp = {
    0x65, 0x5A, 0xC0, 0x64, 0x00, 0x00, 0x80, 0x00,
};
const std::vector<uint8_t> kDataBlockExtreme = {
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF,
};

/// Script a complete, healthy BME280 at @p address: chip-ID, both
/// calibration blocks and one measurement in the data registers.
void scriptBme280(MockI2cBus& mock, uint8_t address)
{
    mock.addDevice(address);
    mock.setRegister(address, kRegChipId, 0x60);
    mock.setRegisters(address, 0x88, kCalibBlock1);
    mock.setRegisters(address, 0xE1, kCalibBlock2);
    mock.setRegisters(address, kRegData, kDataBlock);
}

/// Fresh mock + sensor per test with the device at @p address;
/// initialization is part of the fixture and the recorded call log is
/// cleared so each test asserts only on its own transactions.
struct Fixture {
    MockI2cBus mock;
    Bme280Sensor sensor{mock};

    explicit Fixture(uint8_t address = kAddrSecondary)
    {
        scriptBme280(mock, address);
        TEST_ASSERT_TRUE(sensor.initialize());
        mock.calls.clear();
    }
};

}  // namespace

// --------------------------------------------------------------------------
// initialize() finds the device (bench parity: strapped to 0x77) and
// writes the EXACT parity sampling profile, ctrl_hum BEFORE ctrl_meas
// (datasheet: ctrl_hum takes effect only on a ctrl_meas write), then
// config: (0xF2,0x01) → (0xF4,0x57) → (0xF5,0x90)
// --------------------------------------------------------------------------
static void test_initialize_writes_parity_config_bytes_in_order(void)
{
    MockI2cBus mock;
    Bme280Sensor sensor(mock);
    scriptBme280(mock, kAddrSecondary);

    TEST_ASSERT_TRUE(sensor.initialize());
    TEST_ASSERT_EQUAL_INT(0, sensor.getLastError());

    // Exactly three writes, in the contract order, with the parity bytes.
    std::vector<MockI2cBus::Call> writes;
    for (const MockI2cBus::Call& call : mock.calls) {
        if (call.type == MockI2cBus::Call::Type::Write) {
            writes.push_back(call);
        }
    }
    TEST_ASSERT_EQUAL(3, writes.size());
    TEST_ASSERT_EQUAL_UINT8(kRegCtrlHum, writes[0].reg);
    TEST_ASSERT_EQUAL_UINT8(0x01, writes[0].value);  // osrs_h ×1
    TEST_ASSERT_EQUAL_UINT8(kRegCtrlMeas, writes[1].reg);
    TEST_ASSERT_EQUAL_UINT8(0x57, writes[1].value);  // T×2, P×16, NORMAL
    TEST_ASSERT_EQUAL_UINT8(kRegConfig, writes[2].reg);
    TEST_ASSERT_EQUAL_UINT8(0x90, writes[2].value);  // 500 ms, IIR ×16
    for (const MockI2cBus::Call& write : writes) {
        TEST_ASSERT_EQUAL_UINT8(kAddrSecondary, write.address);
        TEST_ASSERT_TRUE(write.succeeded);
    }
}

// --------------------------------------------------------------------------
// Probing order: 0x76 is tried first (data-model.md); with the device at
// 0x76 the secondary address is never probed
// --------------------------------------------------------------------------
static void test_initialize_probes_primary_address_first(void)
{
    MockI2cBus mock;
    Bme280Sensor sensor(mock);
    scriptBme280(mock, kAddrPrimary);

    TEST_ASSERT_TRUE(sensor.initialize());

    TEST_ASSERT_FALSE(mock.calls.empty());
    TEST_ASSERT_EQUAL(static_cast<int>(MockI2cBus::Call::Type::Probe),
                      static_cast<int>(mock.calls.front().type));
    TEST_ASSERT_EQUAL_UINT8(kAddrPrimary, mock.calls.front().address);
    for (const MockI2cBus::Call& call : mock.calls) {
        TEST_ASSERT_EQUAL_UINT8(kAddrPrimary, call.address);
    }
}

// --------------------------------------------------------------------------
// initialize() is idempotent: a second call is a no-op on the bus
// --------------------------------------------------------------------------
static void test_initialize_is_idempotent(void)
{
    Fixture f;

    TEST_ASSERT_TRUE(f.sensor.initialize());
    TEST_ASSERT_EQUAL(0, f.mock.calls.size());
}

// --------------------------------------------------------------------------
// read() reproduces the reference vector: 25.08 °C / 69.647 %RH /
// 1006.53 hPa (Bosch integer paths ÷100, ÷1024, ÷256÷100)
// --------------------------------------------------------------------------
static void test_read_produces_reference_values(void)
{
    Fixture f;

    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());

    TEST_ASSERT_FLOAT_WITHIN(0.005f, kExpectedTemperature,
                             f.sensor.getTemperature());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, kExpectedHumidity,
                             f.sensor.getHumidity());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, kExpectedPressure,
                             f.sensor.getPressure());
}

// --------------------------------------------------------------------------
// One read() = exactly ONE bus transaction: an 8-byte burst at 0xF7
// (atomic snapshot within the chip's register shadowing)
// --------------------------------------------------------------------------
static void test_read_is_one_eight_byte_burst(void)
{
    Fixture f;

    TEST_ASSERT_TRUE(f.sensor.read());

    TEST_ASSERT_EQUAL(1, f.mock.calls.size());
    const MockI2cBus::Call& call = f.mock.calls.front();
    TEST_ASSERT_EQUAL(static_cast<int>(MockI2cBus::Call::Type::Read),
                      static_cast<int>(call.type));
    TEST_ASSERT_EQUAL_UINT8(kAddrSecondary, call.address);
    TEST_ASSERT_EQUAL_UINT8(kRegData, call.reg);
    TEST_ASSERT_EQUAL(8, call.len);
    TEST_ASSERT_TRUE(call.succeeded);
}

// --------------------------------------------------------------------------
// Before the FIRST successful read() the getters return the documented
// meaningless placeholders (0.0) — consumers gate on read(), never on
// value plausibility (interface contract)
// --------------------------------------------------------------------------
static void test_getters_before_first_read_are_placeholders(void)
{
    MockI2cBus mock;
    Bme280Sensor sensor(mock);
    scriptBme280(mock, kAddrSecondary);

    TEST_ASSERT_TRUE(sensor.initialize());

    TEST_ASSERT_EQUAL_FLOAT(0.0f, sensor.getTemperature());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sensor.getHumidity());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sensor.getPressure());
}

// --------------------------------------------------------------------------
// Lazy initialization (parity): read() on a never-initialized sensor
// initializes first, then reads — one call delivers a valid reading
// --------------------------------------------------------------------------
static void test_read_initializes_lazily(void)
{
    MockI2cBus mock;
    Bme280Sensor sensor(mock);
    scriptBme280(mock, kAddrSecondary);

    TEST_ASSERT_TRUE(sensor.read());
    TEST_ASSERT_EQUAL_INT(0, sensor.getLastError());
    TEST_ASSERT_FLOAT_WITHIN(0.005f, kExpectedTemperature,
                             sensor.getTemperature());
}

// ==========================================================================
// US2 — error paths (T014)
// ==========================================================================

// --------------------------------------------------------------------------
// Absent sensor: probe NACKs on BOTH addresses → initialize() false,
// error 1 ("sensor not found")
// --------------------------------------------------------------------------
static void test_initialize_absent_sensor_fails_error1(void)
{
    MockI2cBus mock;  // no devices — every probe NACKs
    Bme280Sensor sensor(mock);

    TEST_ASSERT_FALSE(sensor.initialize());
    TEST_ASSERT_EQUAL_INT(1, sensor.getLastError());

    // Both candidate addresses were probed before giving up.
    int probedPrimary = 0;
    int probedSecondary = 0;
    for (const MockI2cBus::Call& call : mock.calls) {
        if (call.type == MockI2cBus::Call::Type::Probe) {
            probedPrimary += call.address == kAddrPrimary;
            probedSecondary += call.address == kAddrSecondary;
        }
    }
    TEST_ASSERT_EQUAL(1, probedPrimary);
    TEST_ASSERT_EQUAL(1, probedSecondary);
}

// --------------------------------------------------------------------------
// Mid-read bus error: read() false, error 2, and the last-good values stay
// untouched (validity contract — consumers gate on read(), values persist)
// --------------------------------------------------------------------------
static void test_read_bus_error_keeps_last_good_and_sets_error2(void)
{
    Fixture f;
    TEST_ASSERT_TRUE(f.sensor.read());  // last-good = reference values

    f.mock.queueReadOutcome(false);  // inject a bus error on the data burst
    TEST_ASSERT_FALSE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(2, f.sensor.getLastError());

    TEST_ASSERT_FLOAT_WITHIN(0.005f, kExpectedTemperature,
                             f.sensor.getTemperature());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, kExpectedHumidity,
                             f.sensor.getHumidity());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, kExpectedPressure,
                             f.sensor.getPressure());
}

// --------------------------------------------------------------------------
// Recovery after a bus error: the driver is uninitialized by the failure,
// so the NEXT read() re-probes BOTH addresses (FR-004) and succeeds
// --------------------------------------------------------------------------
static void test_read_recovers_after_bus_error_with_reprobe(void)
{
    Fixture f;  // device at 0x77
    f.mock.queueReadOutcome(false);
    TEST_ASSERT_FALSE(f.sensor.read());
    f.mock.calls.clear();

    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());

    // The recovery read started with a full re-probe: 0x76 first (NACK),
    // then 0x77 (found) — not a blind retry at the old address.
    TEST_ASSERT_TRUE(f.mock.calls.size() >= 2);
    TEST_ASSERT_EQUAL(static_cast<int>(MockI2cBus::Call::Type::Probe),
                      static_cast<int>(f.mock.calls[0].type));
    TEST_ASSERT_EQUAL_UINT8(kAddrPrimary, f.mock.calls[0].address);
    TEST_ASSERT_EQUAL(static_cast<int>(MockI2cBus::Call::Type::Probe),
                      static_cast<int>(f.mock.calls[1].type));
    TEST_ASSERT_EQUAL_UINT8(kAddrSecondary, f.mock.calls[1].address);
}

// --------------------------------------------------------------------------
// Recovery re-reads the CALIBRATION (analyze finding I1): after a bus
// error, the re-attached module carries a different calibration set (a
// different physical sensor) — the recovered reading must reflect the NEW
// trimming parameters, proving re-init did not reuse the stale ones
// --------------------------------------------------------------------------
static void test_recovery_rereads_calibration_of_replaced_module(void)
{
    Fixture f;  // device at 0x77, reference calibration
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_FLOAT_WITHIN(0.005f, kExpectedTemperature,
                             f.sensor.getTemperature());

    f.mock.queueReadOutcome(false);  // sensor lost mid-read
    TEST_ASSERT_FALSE(f.sensor.read());

    // "Replug" a different module at the same address: same raws, cal-B.
    f.mock.setRegisters(kAddrSecondary, 0x88, kCalibBlock1B);

    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());
    TEST_ASSERT_FLOAT_WITHIN(0.005f, kExpectedTemperatureB,
                             f.sensor.getTemperature());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, kExpectedHumidityB,
                             f.sensor.getHumidity());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, kExpectedPressureB,
                             f.sensor.getPressure());
}

// --------------------------------------------------------------------------
// Sensorless boot, sensor attached later: reads fail with error 1 while
// absent, then the lazy re-init picks the sensor up on a subsequent poll
// without any manual intervention (US2 scenario 3)
// --------------------------------------------------------------------------
static void test_boot_sensorless_then_attach_recovers(void)
{
    MockI2cBus mock;  // boot: nothing on the bus
    Bme280Sensor sensor(mock);

    TEST_ASSERT_FALSE(sensor.read());
    TEST_ASSERT_EQUAL_INT(1, sensor.getLastError());

    scriptBme280(mock, kAddrPrimary);  // attach the module later

    TEST_ASSERT_TRUE(sensor.read());
    TEST_ASSERT_EQUAL_INT(0, sensor.getLastError());
    TEST_ASSERT_FLOAT_WITHIN(0.005f, kExpectedTemperature,
                             sensor.getTemperature());
}

// --------------------------------------------------------------------------
// isAvailable() on an initialized sensor is ONE real chip-ID read and does
// not touch the error code (read() owns the reading's error state)
// --------------------------------------------------------------------------
static void test_is_available_reads_chip_id_and_leaves_error_untouched(void)
{
    Fixture f;
    TEST_ASSERT_TRUE(f.sensor.read());
    f.mock.calls.clear();

    TEST_ASSERT_TRUE(f.sensor.isAvailable());
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());

    TEST_ASSERT_EQUAL(1, f.mock.calls.size());
    const MockI2cBus::Call& call = f.mock.calls.front();
    TEST_ASSERT_EQUAL(static_cast<int>(MockI2cBus::Call::Type::Read),
                      static_cast<int>(call.type));
    TEST_ASSERT_EQUAL_UINT8(kRegChipId, call.reg);
    TEST_ASSERT_EQUAL(1, call.len);
}

// --------------------------------------------------------------------------
// isAvailable() detecting a loss: returns false WITHOUT touching the error
// code, and the next call (lazy re-init, sensor back) reports available
// again. (The lazy path delegates to initialize(), which owns its own
// error reporting — ModbusSoilSensor convention.)
// --------------------------------------------------------------------------
static void test_is_available_false_on_loss_leaves_error_untouched(void)
{
    Fixture f;
    TEST_ASSERT_TRUE(f.sensor.read());  // error 0

    f.mock.queueReadOutcome(false);  // chip-ID read fails: sensor lost
    TEST_ASSERT_FALSE(f.sensor.isAvailable());
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());  // untouched

    // Device still scripted → the lazy re-probe finds it again.
    TEST_ASSERT_TRUE(f.sensor.isAvailable());
}

// --------------------------------------------------------------------------
// NaN guard (US2 scenario 4, FR-007): with the Bosch INTEGER compensation
// paths a NaN is unreachable from any raw register content — the guard is
// retained as the binding legacy-parity safety net. Verified here at the
// extreme of the legal raw range: the read stays finite and succeeds.
// (The NaN→error-2 branch itself cannot be triggered through the II2cBus
// seam; it would require a float path that does not exist.)
// --------------------------------------------------------------------------
static void test_read_extreme_raws_never_produce_nan(void)
{
    Fixture f;
    f.mock.setRegisters(kAddrSecondary, kRegData, kDataBlockExtreme);

    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());
    TEST_ASSERT_FALSE(std::isnan(f.sensor.getTemperature()));
    TEST_ASSERT_FALSE(std::isnan(f.sensor.getHumidity()));
    TEST_ASSERT_FALSE(std::isnan(f.sensor.getPressure()));
}

// ==========================================================================
// US3 — address variants and chip identity (T017)
// ==========================================================================

// --------------------------------------------------------------------------
// Module strapped to 0x76: found, identified and read — every transaction
// stays on the primary address
// --------------------------------------------------------------------------
static void test_device_at_primary_delivers_reading(void)
{
    MockI2cBus mock;
    Bme280Sensor sensor(mock);
    scriptBme280(mock, kAddrPrimary);

    TEST_ASSERT_TRUE(sensor.read());
    TEST_ASSERT_EQUAL_INT(0, sensor.getLastError());
    TEST_ASSERT_FLOAT_WITHIN(0.005f, kExpectedTemperature,
                             sensor.getTemperature());
    for (const MockI2cBus::Call& call : mock.calls) {
        TEST_ASSERT_EQUAL_UINT8(kAddrPrimary, call.address);
    }
}

// --------------------------------------------------------------------------
// Module strapped to 0x77 (bench rig / greenhouse variant): found after
// the 0x76 NACK and read normally
// --------------------------------------------------------------------------
static void test_device_at_secondary_delivers_reading(void)
{
    MockI2cBus mock;
    Bme280Sensor sensor(mock);
    scriptBme280(mock, kAddrSecondary);

    TEST_ASSERT_TRUE(sensor.read());
    TEST_ASSERT_EQUAL_INT(0, sensor.getLastError());
    TEST_ASSERT_FLOAT_WITHIN(0.005f, kExpectedTemperature,
                             sensor.getTemperature());
}

// --------------------------------------------------------------------------
// Wrong chip identity at 0x76 (e.g. a BMP280, ID 0x58) with a real BME280
// at 0x77: the imposter is rejected and 0x77 is selected
// --------------------------------------------------------------------------
static void test_wrong_chip_id_at_primary_selects_secondary(void)
{
    MockI2cBus mock;
    Bme280Sensor sensor(mock);
    mock.addDevice(kAddrPrimary);
    mock.setRegister(kAddrPrimary, kRegChipId, 0x58);  // BMP280 — no RH path
    scriptBme280(mock, kAddrSecondary);

    TEST_ASSERT_TRUE(sensor.initialize());
    TEST_ASSERT_TRUE(sensor.read());

    // All data traffic goes to the verified device at 0x77.
    for (const MockI2cBus::Call& call : mock.calls) {
        if (call.type == MockI2cBus::Call::Type::Write ||
            call.reg == kRegData) {
            TEST_ASSERT_EQUAL_UINT8(kAddrSecondary, call.address);
        }
    }
}

// --------------------------------------------------------------------------
// Foreign devices ACKing on BOTH addresses (address collision): every
// candidate fails the identity check → error 1, sensor unavailable rather
// than misread (US3 scenario 3 / edge case)
// --------------------------------------------------------------------------
static void test_wrong_chip_id_everywhere_fails_error1(void)
{
    MockI2cBus mock;
    Bme280Sensor sensor(mock);
    mock.addDevice(kAddrPrimary);
    mock.setRegister(kAddrPrimary, kRegChipId, 0x58);
    mock.addDevice(kAddrSecondary);
    mock.setRegister(kAddrSecondary, kRegChipId, 0x61);  // BME680

    TEST_ASSERT_FALSE(sensor.initialize());
    TEST_ASSERT_EQUAL_INT(1, sensor.getLastError());
}

// --------------------------------------------------------------------------
// Loss, then reappearance at the OTHER address (module swapped 0x77→0x76
// while unpowered): the recovery re-probe covers both addresses, so the
// swap is transparent (spec edge case, FR-004)
// --------------------------------------------------------------------------
static void test_loss_then_reappearance_at_other_address_recovers(void)
{
    Fixture f;  // device at 0x77
    TEST_ASSERT_TRUE(f.sensor.read());

    f.mock.removeDevice(kAddrSecondary);  // unplug
    TEST_ASSERT_FALSE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(2, f.sensor.getLastError());

    scriptBme280(f.mock, kAddrPrimary);  // replug, strapped to 0x76 now
    f.mock.calls.clear();

    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());
    // The data burst of the recovered reading happened at 0x76.
    const MockI2cBus::Call& last = f.mock.calls.back();
    TEST_ASSERT_EQUAL_UINT8(kRegData, last.reg);
    TEST_ASSERT_EQUAL_UINT8(kAddrPrimary, last.address);
}

// ==========================================================================
// US4 — Bosch reference vectors (T018): integer outputs asserted EXACTLY,
// then the float conversions (÷100, ÷256÷100, ÷1024). Every expectation is
// derived by running the Bosch datasheet reference routines
// (BME280_compensate_T_int32 / _P_int64 / _H_int32) over kRefCal — see the
// per-vector derivation comments at the constants above.
// ==========================================================================

// --------------------------------------------------------------------------
// Datasheet worked example: adc_T=519888, adc_P=415148 → 25.08 °C,
// 100653.25 Pa (datasheet double result: 100653.27 Pa) + humidity extension
// --------------------------------------------------------------------------
static void test_compensation_worked_example_integer_outputs(void)
{
    int32_t tFine = 0;
    const int32_t t = Bme280Sensor::compensateTemperature(519888, kRefCal,
                                                          tFine);
    TEST_ASSERT_EQUAL_INT32(kRefTFine, tFine);
    TEST_ASSERT_EQUAL_INT32(kRefTemperatureInt, t);

    const uint32_t p = Bme280Sensor::compensatePressure(415148, kRefCal,
                                                        tFine);
    TEST_ASSERT_EQUAL_UINT32(kRefPressureQ248, p);

    const uint32_t h = Bme280Sensor::compensateHumidity(32768, kRefCal,
                                                        tFine);
    TEST_ASSERT_EQUAL_UINT32(kRefHumidityQ2210, h);

    // Float conversions (÷100, ÷256÷100, ÷1024).
    TEST_ASSERT_EQUAL_FLOAT(25.08f, static_cast<float>(t) / 100.0f);
    TEST_ASSERT_EQUAL_FLOAT(1006.5325f,
                            static_cast<float>(p) / 256.0f / 100.0f);
    TEST_ASSERT_EQUAL_FLOAT(69.647461f, static_cast<float>(h) / 1024.0f);
}

// --------------------------------------------------------------------------
// Negative-temperature vector: adc_T=409600 (0x64000) over kRefCal →
// t_fine=-49208, T=-961 (-9.61 °C); P/H fed with the negative t_fine
// (derivation: Bosch reference routines, constants comment above)
// --------------------------------------------------------------------------
static void test_compensation_negative_temperature_vector(void)
{
    int32_t tFine = 0;
    const int32_t t = Bme280Sensor::compensateTemperature(409600, kRefCal,
                                                          tFine);
    TEST_ASSERT_EQUAL_INT32(-49208, tFine);
    TEST_ASSERT_EQUAL_INT32(-961, t);

    const uint32_t p = Bme280Sensor::compensatePressure(415148, kRefCal,
                                                        tFine);
    TEST_ASSERT_EQUAL_UINT32(24415055, p);

    const uint32_t h = Bme280Sensor::compensateHumidity(32768, kRefCal,
                                                        tFine);
    TEST_ASSERT_EQUAL_UINT32(68674, h);

    TEST_ASSERT_EQUAL_FLOAT(-9.61f, static_cast<float>(t) / 100.0f);
    TEST_ASSERT_EQUAL_FLOAT(953.71309f,
                            static_cast<float>(p) / 256.0f / 100.0f);
    TEST_ASSERT_EQUAL_FLOAT(67.064453f, static_cast<float>(h) / 1024.0f);
}

// --------------------------------------------------------------------------
// Extreme-but-legal raws: adc_T=0xFFFFF, adc_P=0, adc_H=0xFFFF over
// kRefCal → T=18755, P=55536661, H=102400 (= exactly 100 %RH, the
// reference algorithm's own upper clamp 419430400>>12; derivation in the
// constants comment above)
// --------------------------------------------------------------------------
static void test_compensation_extreme_legal_raws_vector(void)
{
    int32_t tFine = 0;
    const int32_t t = Bme280Sensor::compensateTemperature(0xFFFFF, kRefCal,
                                                          tFine);
    TEST_ASSERT_EQUAL_INT32(960246, tFine);
    TEST_ASSERT_EQUAL_INT32(18755, t);

    const uint32_t p = Bme280Sensor::compensatePressure(0, kRefCal, tFine);
    TEST_ASSERT_EQUAL_UINT32(55536661, p);

    const uint32_t h = Bme280Sensor::compensateHumidity(0xFFFF, kRefCal,
                                                        tFine);
    TEST_ASSERT_EQUAL_UINT32(102400, h);

    TEST_ASSERT_EQUAL_FLOAT(187.55f, static_cast<float>(t) / 100.0f);
    TEST_ASSERT_EQUAL_FLOAT(2169.4008f,
                            static_cast<float>(p) / 256.0f / 100.0f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, static_cast<float>(h) / 1024.0f);
}

// --------------------------------------------------------------------------
// End-to-end read of the negative-temperature raw block: register
// assembly + compensation + float conversion through the full read() path
// --------------------------------------------------------------------------
static void test_read_negative_temperature_block(void)
{
    Fixture f;
    f.mock.setRegisters(kAddrSecondary, kRegData, kDataBlockNegativeTemp);

    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_FLOAT_WITHIN(0.005f, -9.61f, f.sensor.getTemperature());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 67.064453f, f.sensor.getHumidity());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 953.71309f, f.sensor.getPressure());
}

// ==========================================================================
// Sensor-task log policy (T015, analyze finding U1): the WARN/INFO/silence
// decisions of main/sensor_task.cpp, host-tested deterministically
// ==========================================================================

// --------------------------------------------------------------------------
// Sensorless boot: the policy starts "valid", so failure #1 WARNs exactly
// once (FailureTransition); repeats fire at every 12th consecutive failure
// and everything in between is silent
// --------------------------------------------------------------------------
static void test_log_policy_warns_once_then_bounded_repeats(void)
{
    SensorTaskLogPolicy policy;

    TEST_ASSERT_EQUAL(
        static_cast<int>(SensorTaskLogPolicy::Event::FailureTransition),
        static_cast<int>(policy.onReadResult(false)));  // failure 1

    for (uint32_t failure = 2; failure <= 25; ++failure) {
        const SensorTaskLogPolicy::Event event = policy.onReadResult(false);
        if (failure % SensorTaskLogPolicy::kFailureLogInterval == 0) {
            TEST_ASSERT_EQUAL(
                static_cast<int>(
                    SensorTaskLogPolicy::Event::RepeatedFailure),
                static_cast<int>(event));  // failures 12 and 24
            TEST_ASSERT_EQUAL_UINT32(failure, policy.consecutiveFailures());
        } else {
            TEST_ASSERT_EQUAL(
                static_cast<int>(SensorTaskLogPolicy::Event::Silent),
                static_cast<int>(event));
        }
    }
}

// --------------------------------------------------------------------------
// Recovery: the first success after a failure run is a Recovery WARN,
// further successes are plain INFO readings, and a NEW failure run WARNs
// its transition again (the once-per-transition discipline)
// --------------------------------------------------------------------------
static void test_log_policy_warns_on_recovery_then_reads(void)
{
    SensorTaskLogPolicy policy;

    TEST_ASSERT_EQUAL(
        static_cast<int>(SensorTaskLogPolicy::Event::Reading),
        static_cast<int>(policy.onReadResult(true)));  // steady state
    TEST_ASSERT_EQUAL(
        static_cast<int>(SensorTaskLogPolicy::Event::FailureTransition),
        static_cast<int>(policy.onReadResult(false)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(SensorTaskLogPolicy::Event::Recovery),
        static_cast<int>(policy.onReadResult(true)));
    TEST_ASSERT_EQUAL_UINT32(0, policy.consecutiveFailures());
    TEST_ASSERT_EQUAL(
        static_cast<int>(SensorTaskLogPolicy::Event::Reading),
        static_cast<int>(policy.onReadResult(true)));
    TEST_ASSERT_EQUAL(
        static_cast<int>(SensorTaskLogPolicy::Event::FailureTransition),
        static_cast<int>(policy.onReadResult(false)));
}

// --------------------------------------------------------------------------
// Hours-long outage (spec edge case): the log volume stays bounded — over
// 1000 consecutive failures exactly 1 transition WARN + 83 repeat WARNs —
// and the policy keeps producing decisions (no terminal state, no exit)
// --------------------------------------------------------------------------
static void test_log_policy_long_outage_stays_bounded_and_never_stops(void)
{
    SensorTaskLogPolicy policy;
    uint32_t transitions = 0;
    uint32_t repeats = 0;
    uint32_t silents = 0;

    for (int i = 0; i < 1000; ++i) {
        switch (policy.onReadResult(false)) {
        case SensorTaskLogPolicy::Event::FailureTransition: ++transitions; break;
        case SensorTaskLogPolicy::Event::RepeatedFailure: ++repeats; break;
        case SensorTaskLogPolicy::Event::Silent: ++silents; break;
        default: TEST_FAIL_MESSAGE("success event from a failed read");
        }
    }
    TEST_ASSERT_EQUAL_UINT32(1, transitions);
    TEST_ASSERT_EQUAL_UINT32(83, repeats);  // floor(1000 / 12)
    TEST_ASSERT_EQUAL_UINT32(916, silents);

    // Still alive after the outage: recovery is reported normally.
    TEST_ASSERT_EQUAL(
        static_cast<int>(SensorTaskLogPolicy::Event::Recovery),
        static_cast<int>(policy.onReadResult(true)));
}

// ==========================================================================
// MockEnvironmentalSensor consistency (T020) — the consumer-facing double
// for PR-09/PR-11 tests
// ==========================================================================

// --------------------------------------------------------------------------
// A scripted sequence is observed exactly: success → failure (last-good
// values retained, scripted error) → success (fresh values, error 0)
// --------------------------------------------------------------------------
static void test_mock_env_scripted_sequence_observed_exactly(void)
{
    MockEnvironmentalSensor mock;
    mock.scriptSuccessfulRead(21.5f, 40.0f, 1013.2f);
    mock.scriptFailedRead(2);
    mock.scriptSuccessfulRead(22.0f, 41.0f, 1012.8f);

    TEST_ASSERT_TRUE(mock.read());
    TEST_ASSERT_EQUAL_INT(0, mock.getLastError());
    TEST_ASSERT_EQUAL_FLOAT(21.5f, mock.getTemperature());
    TEST_ASSERT_EQUAL_FLOAT(40.0f, mock.getHumidity());
    TEST_ASSERT_EQUAL_FLOAT(1013.2f, mock.getPressure());

    TEST_ASSERT_FALSE(mock.read());
    TEST_ASSERT_EQUAL_INT(2, mock.getLastError());
    TEST_ASSERT_EQUAL_FLOAT(21.5f, mock.getTemperature());  // last-good
    TEST_ASSERT_EQUAL_FLOAT(40.0f, mock.getHumidity());
    TEST_ASSERT_EQUAL_FLOAT(1013.2f, mock.getPressure());

    TEST_ASSERT_TRUE(mock.read());
    TEST_ASSERT_EQUAL_INT(0, mock.getLastError());
    TEST_ASSERT_EQUAL_FLOAT(22.0f, mock.getTemperature());
    TEST_ASSERT_EQUAL_FLOAT(41.0f, mock.getHumidity());
    TEST_ASSERT_EQUAL_FLOAT(1012.8f, mock.getPressure());

    TEST_ASSERT_EQUAL(3, mock.readCalls);
}

// --------------------------------------------------------------------------
// An exhausted script repeats its LAST step forever (steady sensor keeps
// delivering, dead sensor keeps failing) — documented convenience for
// consumer tests that poll more often than they script
// --------------------------------------------------------------------------
static void test_mock_env_exhausted_script_repeats_last_step(void)
{
    MockEnvironmentalSensor good;
    good.scriptSuccessfulRead(20.0f, 50.0f, 1000.0f);
    TEST_ASSERT_TRUE(good.read());
    TEST_ASSERT_TRUE(good.read());  // repeats the success
    TEST_ASSERT_EQUAL_FLOAT(20.0f, good.getTemperature());
    TEST_ASSERT_EQUAL_INT(0, good.getLastError());

    MockEnvironmentalSensor dead;
    dead.scriptSuccessfulRead(20.0f, 50.0f, 1000.0f);
    dead.scriptFailedRead(1);
    TEST_ASSERT_TRUE(dead.read());
    TEST_ASSERT_FALSE(dead.read());
    TEST_ASSERT_FALSE(dead.read());  // repeats the failure
    TEST_ASSERT_EQUAL_INT(1, dead.getLastError());
    TEST_ASSERT_EQUAL_FLOAT(20.0f, dead.getTemperature());  // last-good
}

// --------------------------------------------------------------------------
// Helpers keep values/validity/error coherent: a failed step carries its
// error and leaves values alone; a successful step always resets error 0.
// Unscripted reads succeed with the placeholder values (0.0), matching the
// real driver's before-first-read contract
// --------------------------------------------------------------------------
static void test_mock_env_helpers_keep_state_coherent(void)
{
    MockEnvironmentalSensor unscripted;
    TEST_ASSERT_TRUE(unscripted.read());
    TEST_ASSERT_EQUAL_INT(0, unscripted.getLastError());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, unscripted.getTemperature());

    MockEnvironmentalSensor mock;
    mock.scriptFailedRead(1);  // e.g. sensorless boot
    mock.scriptSuccessfulRead(18.0f, 55.0f, 990.0f);
    TEST_ASSERT_FALSE(mock.read());
    TEST_ASSERT_EQUAL_INT(1, mock.getLastError());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, mock.getTemperature());  // still placeholder
    TEST_ASSERT_TRUE(mock.read());
    TEST_ASSERT_EQUAL_INT(0, mock.getLastError());
    TEST_ASSERT_EQUAL_FLOAT(18.0f, mock.getTemperature());

    // initialize()/isAvailable() are scripted fields with call counters and
    // never touch the error code (interface convention).
    mock.initializeResult = false;
    mock.isAvailableResult = false;
    TEST_ASSERT_FALSE(mock.initialize());
    TEST_ASSERT_FALSE(mock.isAvailable());
    TEST_ASSERT_EQUAL_INT(0, mock.getLastError());
    TEST_ASSERT_EQUAL(1, mock.initializeCalls);
    TEST_ASSERT_EQUAL(1, mock.isAvailableCalls);
}

void run_bme280_tests(void)
{
    // US1 (T006)
    RUN_TEST(test_initialize_writes_parity_config_bytes_in_order);
    RUN_TEST(test_initialize_probes_primary_address_first);
    RUN_TEST(test_initialize_is_idempotent);
    RUN_TEST(test_read_produces_reference_values);
    RUN_TEST(test_read_is_one_eight_byte_burst);
    RUN_TEST(test_getters_before_first_read_are_placeholders);
    RUN_TEST(test_read_initializes_lazily);
    // US2 (T014)
    RUN_TEST(test_initialize_absent_sensor_fails_error1);
    RUN_TEST(test_read_bus_error_keeps_last_good_and_sets_error2);
    RUN_TEST(test_read_recovers_after_bus_error_with_reprobe);
    RUN_TEST(test_recovery_rereads_calibration_of_replaced_module);
    RUN_TEST(test_boot_sensorless_then_attach_recovers);
    RUN_TEST(test_is_available_reads_chip_id_and_leaves_error_untouched);
    RUN_TEST(test_is_available_false_on_loss_leaves_error_untouched);
    RUN_TEST(test_read_extreme_raws_never_produce_nan);
    // US3 (T017)
    RUN_TEST(test_device_at_primary_delivers_reading);
    RUN_TEST(test_device_at_secondary_delivers_reading);
    RUN_TEST(test_wrong_chip_id_at_primary_selects_secondary);
    RUN_TEST(test_wrong_chip_id_everywhere_fails_error1);
    RUN_TEST(test_loss_then_reappearance_at_other_address_recovers);
    // US4 (T018)
    RUN_TEST(test_compensation_worked_example_integer_outputs);
    RUN_TEST(test_compensation_negative_temperature_vector);
    RUN_TEST(test_compensation_extreme_legal_raws_vector);
    RUN_TEST(test_read_negative_temperature_block);
    // Sensor-task log policy (T015)
    RUN_TEST(test_log_policy_warns_once_then_bounded_repeats);
    RUN_TEST(test_log_policy_warns_on_recovery_then_reads);
    RUN_TEST(test_log_policy_long_outage_stays_bounded_and_never_stops);
    // MockEnvironmentalSensor (T020)
    RUN_TEST(test_mock_env_scripted_sequence_observed_exactly);
    RUN_TEST(test_mock_env_exhausted_script_repeats_last_step);
    RUN_TEST(test_mock_env_helpers_keep_state_coherent);
}
