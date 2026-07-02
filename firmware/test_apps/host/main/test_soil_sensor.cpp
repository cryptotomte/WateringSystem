// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_soil_sensor.cpp
 * @brief Host tests for the ModbusSoilSensor register decode (linux target).
 *
 * Tests the REAL decode/scaling logic (ModbusSoilSensor) via
 * MockModbusClient (scripted register payloads + call recording).
 * Registered via run_soil_sensor_tests() from the shared Unity runner
 * (test_main.cpp); the process exit code equals the failure count and is
 * the CI gate.
 *
 * Coverage maps to tasks.md T006 / quickstart.md §1: the data-model.md
 * scaling table incl. the signed-temperature decode, humidity ≡ moisture,
 * and the one-transaction-per-read() invariant. Fault paths (timeout,
 * validation, exceptions, recovery) are phase 4 (T014); calibration is
 * phase 6 (T021).
 */

#include <vector>

#include "unity.h"

#include "sensors/ModbusSoilSensor.h"
#include "sensors/testing/MockModbusClient.h"

namespace {

constexpr uint8_t kAddr = 0x01;
constexpr uint16_t kStartReg = 0x0000;
constexpr uint16_t kRegCount = 9;

/// Fresh mock + sensor per test; initialization (client init + 1-register
/// probe) is part of the fixture, and the recorded call log is cleared so
/// each test asserts only on its own transactions.
struct Fixture {
    MockModbusClient mock;
    ModbusSoilSensor sensor{mock, kAddr};

    explicit Fixture(std::vector<uint16_t> payload)
    {
        mock.setRegisters(kAddr, kStartReg, std::move(payload));
        TEST_ASSERT_TRUE(sensor.initialize());
        mock.calls.clear();
    }
};

}  // namespace

// --------------------------------------------------------------------------
// Known 9-register payload decodes per the data-model scaling table,
// including a negative temperature (0xFF38 = -200 raw = -20.0 °C)
// --------------------------------------------------------------------------
static void test_decode_known_payload_negative_temperature(void)
{
    // regs 0x0000..0x0008: humidity/moisture, temp, EC, pH, N, P, K,
    // salinity, TDS.
    Fixture f({550, 0xFF38, 1200, 68, 45, 30, 120, 7, 9});

    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());

    TEST_ASSERT_EQUAL_FLOAT(55.0f, f.sensor.getMoisture());   // 550 / 10
    TEST_ASSERT_EQUAL_FLOAT(-20.0f, f.sensor.getTemperature());  // int16 -200 / 10
    TEST_ASSERT_EQUAL_FLOAT(1200.0f, f.sensor.getEC());       // unscaled
    TEST_ASSERT_EQUAL_FLOAT(6.8f, f.sensor.getPH());          // 68 / 10
    TEST_ASSERT_EQUAL_FLOAT(45.0f, f.sensor.getNitrogen());
    TEST_ASSERT_EQUAL_FLOAT(30.0f, f.sensor.getPhosphorus());
    TEST_ASSERT_EQUAL_FLOAT(120.0f, f.sensor.getPotassium());

    // Salinity (reg 0x0007 = 7) and TDS (reg 0x0008 = 9) are read but not
    // exposed: ISoilSensor has no getter for them, so there is nothing
    // further to assert beyond the 7 values above (deliberate parity trim).
}

// --------------------------------------------------------------------------
// Positive signed temperature decodes the same way (235 raw = 23.5 °C)
// --------------------------------------------------------------------------
static void test_decode_positive_temperature(void)
{
    Fixture f({420, 235, 800, 65, 12, 8, 40, 3, 4});

    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(23.5f, f.sensor.getTemperature());
    TEST_ASSERT_EQUAL_FLOAT(42.0f, f.sensor.getMoisture());
    TEST_ASSERT_EQUAL_FLOAT(6.5f, f.sensor.getPH());
}

// --------------------------------------------------------------------------
// Humidity ≡ moisture (parity: single quantity in register 0x0000)
// --------------------------------------------------------------------------
static void test_humidity_equals_moisture(void)
{
    Fixture f({550, 0xFF38, 1200, 68, 45, 30, 120, 7, 9});

    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(f.sensor.getMoisture(), f.sensor.getHumidity());
    TEST_ASSERT_EQUAL_FLOAT(55.0f, f.sensor.getHumidity());
}

// --------------------------------------------------------------------------
// One read() = exactly ONE bus transaction: all 9 registers in one
// readHoldingRegisters(0x01, 0x0000, 9) call (FR-004, no retry)
// --------------------------------------------------------------------------
static void test_read_is_one_nine_register_transaction(void)
{
    Fixture f({550, 235, 1200, 68, 45, 30, 120, 7, 9});

    TEST_ASSERT_TRUE(f.sensor.read());

    TEST_ASSERT_EQUAL(1, f.mock.calls.size());
    const MockModbusClient::Call &call = f.mock.calls.front();
    TEST_ASSERT_EQUAL(static_cast<int>(MockModbusClient::Call::Type::Read),
                      static_cast<int>(call.type));
    TEST_ASSERT_EQUAL_UINT8(kAddr, call.deviceAddress);
    TEST_ASSERT_EQUAL_UINT16(kStartReg, call.startRegister);
    TEST_ASSERT_EQUAL_UINT16(kRegCount, call.count);
    TEST_ASSERT_TRUE(call.succeeded);
}

void run_soil_sensor_tests(void)
{
    RUN_TEST(test_decode_known_payload_negative_temperature);
    RUN_TEST(test_decode_positive_temperature);
    RUN_TEST(test_humidity_equals_moisture);
    RUN_TEST(test_read_is_one_nine_register_transaction);
}
