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
 * Coverage maps to tasks.md T006 (this file starts with the US1 happy
 * path: initialization incl. the exact parity config-byte sequence, a
 * reference-vector read, getters-before-first-read); T014/T017/T018/T020
 * extend this suite with the error paths, address-variant handling, the
 * full Bosch vector set and the MockEnvironmentalSensor consistency tests.
 */

#include <vector>

#include "unity.h"

#include "sensors/Bme280Sensor.h"
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

void run_bme280_tests(void)
{
    RUN_TEST(test_initialize_writes_parity_config_bytes_in_order);
    RUN_TEST(test_initialize_probes_primary_address_first);
    RUN_TEST(test_initialize_is_idempotent);
    RUN_TEST(test_read_produces_reference_values);
    RUN_TEST(test_read_is_one_eight_byte_burst);
    RUN_TEST(test_getters_before_first_read_are_placeholders);
    RUN_TEST(test_read_initializes_lazily);
}
