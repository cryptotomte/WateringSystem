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
 * Coverage maps to tasks.md T006 + T014 / quickstart.md §1: the
 * data-model.md scaling table incl. the signed-temperature decode,
 * humidity ≡ moisture, the one-transaction-per-read() invariant, and the
 * US2 fault paths — timeout, range validation (error 5, all-or-nothing
 * publish), exception propagation, no-retry, implicit recovery, lazy
 * (re)initialization, real-bus availability probe, statistics and the
 * setTimeout client contract. T021 adds the calibration flows: factor
 * computation from a fresh 1-register raw read, the ×100 best-effort
 * calibration-register write (NON-FATAL on failure), read-path application
 * for pH/EC (the moisture factor is stored/written but NOT applied — legacy
 * parity), the raw-too-low guard (error 5) and range validation running on
 * the FACTORED values.
 */

#include <cmath>
#include <vector>

#include "unity.h"

#include "sensors/ModbusSoilSensor.h"
#include "sensors/testing/MockModbusClient.h"

namespace {

constexpr uint8_t kAddr = 0x01;
constexpr uint16_t kStartReg = 0x0000;
constexpr uint16_t kRegCount = 9;

// Calibration raw-read registers (data-model.md; 1-register reads — a
// distinct mock script key from the 9-register data payload above) and the
// calibration factor registers written with factor ×100.
constexpr uint16_t kRegEc = 0x0002;
constexpr uint16_t kRegPh = 0x0003;
constexpr uint16_t kRegMoistureCalib = 0x0100;
constexpr uint16_t kRegPhCalib = 0x0101;
constexpr uint16_t kRegEcCalib = 0x0102;

/// Baseline valid payload: moisture 55.0 %, temperature 23.5 °C,
/// EC 1200 µS/cm, pH 6.8, N 45, P 30, K 120 (+ salinity/TDS, unexposed).
std::vector<uint16_t> goodPayload()
{
    return {550, 235, 1200, 68, 45, 30, 120, 7, 9};
}

/// Fresh mock + sensor per test; initialization (client init + 1-register
/// probe) is part of the fixture, and the recorded call log is cleared so
/// each test asserts only on its own transactions. NOTE: the fixture's
/// probe read still counts in the mock STATISTICS (success 1 / error 0
/// baseline) — only the call log is cleared.
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

// ==========================================================================
// Fault paths (T014, US2): timeout / validation / exception / no-retry /
// recovery / statistics / availability probe / setTimeout contract
// ==========================================================================

// --------------------------------------------------------------------------
// Timeout: read() fails with error 3 and the last-good values remain
// untouched — getters after a failed read are stale, not fresh (FR-005)
// --------------------------------------------------------------------------
static void test_timeout_fails_read_and_keeps_last_good_values(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());

    f.mock.queueOutcome(MockModbusClient::kErrTimeout);
    TEST_ASSERT_FALSE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(MockModbusClient::kErrTimeout,
                          f.sensor.getLastError());

    // Last-good reading still served (validity is carried by the read()
    // result + error code, never by the values).
    TEST_ASSERT_EQUAL_FLOAT(55.0f, f.sensor.getMoisture());
    TEST_ASSERT_EQUAL_FLOAT(23.5f, f.sensor.getTemperature());
    TEST_ASSERT_EQUAL_FLOAT(1200.0f, f.sensor.getEC());
    TEST_ASSERT_EQUAL_FLOAT(6.8f, f.sensor.getPH());
}

// --------------------------------------------------------------------------
// Range validation: moisture > 100 % (raw 1500 = 150.0 %) → error 5,
// nothing published
// --------------------------------------------------------------------------
static void test_out_of_range_moisture_rejected_error5(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());

    f.mock.setRegisters(kAddr, kStartReg, {1500, 235, 800, 68, 1, 2, 3, 0, 0});
    TEST_ASSERT_FALSE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(5, f.sensor.getLastError());

    TEST_ASSERT_EQUAL_FLOAT(55.0f, f.sensor.getMoisture());
    TEST_ASSERT_EQUAL_FLOAT(55.0f, f.sensor.getHumidity());
}

// --------------------------------------------------------------------------
// Range validation: temperature < -40 °C (raw 0xFE0C = int16 -500 =
// -50.0 °C) → error 5, nothing published
// --------------------------------------------------------------------------
static void test_out_of_range_temperature_rejected_error5(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());

    f.mock.setRegisters(kAddr, kStartReg,
                        {550, 0xFE0C, 800, 68, 1, 2, 3, 0, 0});
    TEST_ASSERT_FALSE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(5, f.sensor.getLastError());

    TEST_ASSERT_EQUAL_FLOAT(23.5f, f.sensor.getTemperature());
}

// --------------------------------------------------------------------------
// Range validation: pH > 9 (raw 95 = 9.5) → error 5, and the publish is
// ALL-OR-NOTHING: in-range fields of the same payload (EC, N/P/K) are
// rejected together with the offending one (FR-005)
// --------------------------------------------------------------------------
static void test_out_of_range_ph_rejected_all_or_nothing(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());

    // pH 9.5 out of range; EC 999 and N/P/K 77/88/99 are individually fine.
    f.mock.setRegisters(kAddr, kStartReg,
                        {550, 235, 999, 95, 77, 88, 99, 0, 0});
    TEST_ASSERT_FALSE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(5, f.sensor.getLastError());

    TEST_ASSERT_EQUAL_FLOAT(6.8f, f.sensor.getPH());
    TEST_ASSERT_EQUAL_FLOAT(1200.0f, f.sensor.getEC());     // not 999
    TEST_ASSERT_EQUAL_FLOAT(45.0f, f.sensor.getNitrogen()); // not 77
    TEST_ASSERT_EQUAL_FLOAT(30.0f, f.sensor.getPhosphorus());
    TEST_ASSERT_EQUAL_FLOAT(120.0f, f.sensor.getPotassium());
}

// --------------------------------------------------------------------------
// Range validation: temperature > 80 °C (raw 850 = 85.0 °C) → error 5,
// nothing published (upper bound of the parity temperature range)
// --------------------------------------------------------------------------
static void test_out_of_range_high_temperature_rejected_error5(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());

    f.mock.setRegisters(kAddr, kStartReg, {550, 850, 800, 68, 1, 2, 3, 0, 0});
    TEST_ASSERT_FALSE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(5, f.sensor.getLastError());

    TEST_ASSERT_EQUAL_FLOAT(23.5f, f.sensor.getTemperature());
}

// --------------------------------------------------------------------------
// Range validation: pH < 3 (raw 25 = 2.5) → error 5, nothing published
// (lower bound of the parity pH range)
// --------------------------------------------------------------------------
static void test_out_of_range_low_ph_rejected_error5(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());

    f.mock.setRegisters(kAddr, kStartReg, {550, 235, 800, 25, 1, 2, 3, 0, 0});
    TEST_ASSERT_FALSE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(5, f.sensor.getLastError());

    TEST_ASSERT_EQUAL_FLOAT(6.8f, f.sensor.getPH());
}

// --------------------------------------------------------------------------
// Boundary inclusivity: the parity range limits themselves are VALID —
// moisture 100.0 % (raw 1000), temperature -40.0 °C (raw 0xFE70 = -400)
// and pH 3.0 (raw 30) in one payload; pH 9.0 (raw 90) too. One step past
// the limit (moisture 100.1 %, raw 1001) is invalid with error 5.
// --------------------------------------------------------------------------
static void test_range_boundaries_are_inclusive(void)
{
    Fixture f({1000, 0xFE70, 800, 30, 1, 2, 3, 0, 0});

    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());
    TEST_ASSERT_EQUAL_FLOAT(100.0f, f.sensor.getMoisture());
    TEST_ASSERT_EQUAL_FLOAT(-40.0f, f.sensor.getTemperature());
    TEST_ASSERT_EQUAL_FLOAT(3.0f, f.sensor.getPH());

    // Upper pH bound (9.0) is valid too.
    f.mock.setRegisters(kAddr, kStartReg, {1000, 0xFE70, 800, 90, 1, 2, 3, 0, 0});
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(9.0f, f.sensor.getPH());

    // One step past the moisture limit: 100.1 % → error 5.
    f.mock.setRegisters(kAddr, kStartReg, {1001, 0xFE70, 800, 90, 1, 2, 3, 0, 0});
    TEST_ASSERT_FALSE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(5, f.sensor.getLastError());
    TEST_ASSERT_EQUAL_FLOAT(100.0f, f.sensor.getMoisture());
}

// --------------------------------------------------------------------------
// EC/N/P/K are NOT range-enforced on read (parity, FR-004 second sentence):
// extreme values — up to the uint16 maximum — are published as valid
// --------------------------------------------------------------------------
static void test_ec_npk_not_range_enforced(void)
{
    Fixture f({550, 235, 65535, 68, 65535, 65535, 65535, 0, 0});

    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());
    TEST_ASSERT_EQUAL_FLOAT(65535.0f, f.sensor.getEC());
    TEST_ASSERT_EQUAL_FLOAT(65535.0f, f.sensor.getNitrogen());
    TEST_ASSERT_EQUAL_FLOAT(65535.0f, f.sensor.getPhosphorus());
    TEST_ASSERT_EQUAL_FLOAT(65535.0f, f.sensor.getPotassium());
}

// --------------------------------------------------------------------------
// Modbus slave exception: the sensor layer propagates the client's error
// code VERBATIM (here 100+2 = 102). The real EspModbusClient coarsens
// exceptions to code 2 (esp-modbus 2.1.2 hides the exception number —
// documented parity divergence R6); this test pins the sensor-layer
// propagation contract, not the client mapping.
// --------------------------------------------------------------------------
static void test_modbus_exception_propagates_verbatim(void)
{
    Fixture f(goodPayload());

    f.mock.queueOutcome(MockModbusClient::kErrExceptionBase + 2);
    TEST_ASSERT_FALSE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(102, f.sensor.getLastError());
}

// --------------------------------------------------------------------------
// No retry: a failed read() is exactly ONE bus attempt (FR-004) — the
// fixture cleared the init probe, so the call log holds only this read
// --------------------------------------------------------------------------
static void test_failed_read_is_single_bus_attempt(void)
{
    Fixture f(goodPayload());

    f.mock.queueOutcome(MockModbusClient::kErrTimeout);
    TEST_ASSERT_FALSE(f.sensor.read());

    TEST_ASSERT_EQUAL(1, f.mock.calls.size());
    const MockModbusClient::Call &call = f.mock.calls.front();
    TEST_ASSERT_EQUAL(static_cast<int>(MockModbusClient::Call::Type::Read),
                      static_cast<int>(call.type));
    TEST_ASSERT_EQUAL_UINT16(kRegCount, call.count);
    TEST_ASSERT_FALSE(call.succeeded);
}

// --------------------------------------------------------------------------
// Implicit recovery: fail (timeout) then succeed — the next read() returns
// fresh values with NO re-initialization (no extra probe transaction, no
// second client initialize; there is no permanent-failure latch)
// --------------------------------------------------------------------------
static void test_read_recovers_after_failure_without_reinit(void)
{
    Fixture f(goodPayload());

    f.mock.queueOutcome(MockModbusClient::kErrTimeout);
    TEST_ASSERT_FALSE(f.sensor.read());

    TEST_ASSERT_TRUE(f.sensor.read());  // defaultOutcome kOk
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());
    TEST_ASSERT_EQUAL_FLOAT(55.0f, f.sensor.getMoisture());
    TEST_ASSERT_EQUAL_FLOAT(23.5f, f.sensor.getTemperature());

    // Exactly the two 9-register reads — no 1-register probe in between.
    TEST_ASSERT_EQUAL(2, f.mock.calls.size());
    TEST_ASSERT_EQUAL_UINT16(kRegCount, f.mock.calls[0].count);
    TEST_ASSERT_EQUAL_UINT16(kRegCount, f.mock.calls[1].count);
    TEST_ASSERT_EQUAL_INT(1, f.mock.initializeCalls);
}

// --------------------------------------------------------------------------
// Statistics: exactly one counter increments per client call, on both
// outcomes (IModbusClient contract; fixture probe = baseline 1 success)
// --------------------------------------------------------------------------
static void test_statistics_count_one_per_call(void)
{
    Fixture f(goodPayload());

    uint32_t successCount = 0;
    uint32_t errorCount = 0;
    f.mock.getStatistics(&successCount, &errorCount);
    TEST_ASSERT_EQUAL_UINT32(1, successCount);  // fixture init probe
    TEST_ASSERT_EQUAL_UINT32(0, errorCount);

    TEST_ASSERT_TRUE(f.sensor.read());
    f.mock.getStatistics(&successCount, &errorCount);
    TEST_ASSERT_EQUAL_UINT32(2, successCount);
    TEST_ASSERT_EQUAL_UINT32(0, errorCount);

    f.mock.queueOutcome(MockModbusClient::kErrTimeout);
    TEST_ASSERT_FALSE(f.sensor.read());
    f.mock.getStatistics(&successCount, &errorCount);
    TEST_ASSERT_EQUAL_UINT32(2, successCount);
    TEST_ASSERT_EQUAL_UINT32(1, errorCount);
}

// --------------------------------------------------------------------------
// isAvailable(): a REAL 1-register bus read of 0x0000 on EVERY call —
// never cached (FR-011); result reflects the live outcome, and a sensor
// that answers again is available again
// --------------------------------------------------------------------------
static void test_is_available_performs_real_probe_every_call(void)
{
    Fixture f(goodPayload());

    TEST_ASSERT_TRUE(f.sensor.isAvailable());
    TEST_ASSERT_EQUAL(1, f.mock.calls.size());
    const MockModbusClient::Call &probe = f.mock.calls.front();
    TEST_ASSERT_EQUAL(static_cast<int>(MockModbusClient::Call::Type::Read),
                      static_cast<int>(probe.type));
    TEST_ASSERT_EQUAL_UINT8(kAddr, probe.deviceAddress);
    TEST_ASSERT_EQUAL_UINT16(0x0000, probe.startRegister);
    TEST_ASSERT_EQUAL_UINT16(1, probe.count);

    // Second call hits the bus again (not a cached true) and reports the
    // live failure...
    f.mock.queueOutcome(MockModbusClient::kErrTimeout);
    TEST_ASSERT_FALSE(f.sensor.isAvailable());
    TEST_ASSERT_EQUAL(2, f.mock.calls.size());

    // ...without clobbering getLastError(): the probe result is carried by
    // the return value only — the fixture's error state (0 after the init)
    // is UNCHANGED by the failed probe (read() owns the reading's error).
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());

    // ...and the next call recovers implicitly (no failure latch).
    TEST_ASSERT_TRUE(f.sensor.isAvailable());
    TEST_ASSERT_EQUAL(3, f.mock.calls.size());
}

// --------------------------------------------------------------------------
// setTimeout(): reaches the client and is recorded verbatim (FR-006 at the
// IModbusClient contract level — ModbusSoilSensor itself never sets the
// timeout; the parity 3000 ms default lives in the client)
// --------------------------------------------------------------------------
static void test_set_timeout_reaches_client(void)
{
    MockModbusClient mock;
    mock.setTimeout(1234);

    TEST_ASSERT_EQUAL(1, mock.timeoutCalls.size());
    TEST_ASSERT_EQUAL_UINT32(1234, mock.timeoutCalls.front());
}

// --------------------------------------------------------------------------
// Lazy initialization: a failed client initialize() → read() fails with
// error 2 and NO bus transaction; once the client comes up, the next
// read() initializes (probe) and delivers fresh values
// --------------------------------------------------------------------------
static void test_lazy_init_client_failure_then_recovery(void)
{
    MockModbusClient mock;
    mock.setRegisters(kAddr, kStartReg, goodPayload());
    mock.initializeResult = false;
    ModbusSoilSensor sensor(mock, kAddr);

    TEST_ASSERT_FALSE(sensor.read());
    TEST_ASSERT_EQUAL_INT(2, sensor.getLastError());  // client init failed
    TEST_ASSERT_EQUAL(0, mock.calls.size());          // never reached the bus

    mock.initializeResult = true;
    TEST_ASSERT_TRUE(sensor.read());
    TEST_ASSERT_EQUAL_INT(0, sensor.getLastError());
    TEST_ASSERT_EQUAL_FLOAT(55.0f, sensor.getMoisture());

    // Recovery read = 1-register init probe + the 9-register data read.
    TEST_ASSERT_EQUAL(2, mock.calls.size());
    TEST_ASSERT_EQUAL_UINT16(1, mock.calls[0].count);
    TEST_ASSERT_EQUAL_UINT16(kRegCount, mock.calls[1].count);
}

// --------------------------------------------------------------------------
// Lazy initialization: a failed init PROBE keeps the sensor uninitialized
// (read() aborts after the probe — no data transaction) and the next
// read() re-runs the full initialization before reading
// --------------------------------------------------------------------------
static void test_lazy_init_probe_failure_then_recovery(void)
{
    MockModbusClient mock;
    mock.setRegisters(kAddr, kStartReg, goodPayload());
    ModbusSoilSensor sensor(mock, kAddr);

    mock.queueOutcome(MockModbusClient::kErrTimeout);  // hits the init probe
    TEST_ASSERT_FALSE(sensor.read());
    TEST_ASSERT_EQUAL_INT(MockModbusClient::kErrTimeout,
                          sensor.getLastError());
    TEST_ASSERT_EQUAL(1, mock.calls.size());  // probe only, no 9-reg read
    TEST_ASSERT_EQUAL_UINT16(1, mock.calls[0].count);

    TEST_ASSERT_TRUE(sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(55.0f, sensor.getMoisture());

    // Second read re-initialized: probe (1 reg) + data read (9 regs).
    TEST_ASSERT_EQUAL(3, mock.calls.size());
    TEST_ASSERT_EQUAL_UINT16(1, mock.calls[1].count);
    TEST_ASSERT_EQUAL_UINT16(kRegCount, mock.calls[2].count);
    TEST_ASSERT_EQUAL_INT(2, mock.initializeCalls);
}

// ==========================================================================
// Calibration (T021, US3): factor computation + ×100 register write,
// read-path application (pH/EC yes, moisture no — parity), non-fatal write
// failure, raw-too-low guard, validation on factored values
// ==========================================================================

// --------------------------------------------------------------------------
// calibratePH(): fresh 1-register raw read of 0x0003, factor =
// reference / (raw / 10), factor ×100 written to 0x0101, and the factor IS
// applied to every subsequent read()
// --------------------------------------------------------------------------
static void test_calibrate_ph_factor_write_and_read_effect(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(6.8f, f.sensor.getPH());  // pre-calibration
    f.mock.calls.clear();

    // Raw pH register scripted as 68 → 6.8; reference 7.0 → factor 7.0/6.8.
    f.mock.setRegisters(kAddr, kRegPh, {68});
    TEST_ASSERT_TRUE(f.sensor.calibratePH(7.0f));
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());

    // Exactly one 1-register raw read + one factor write (×100 truncated:
    // 7.0/6.8 = 1.0294… → 102), already initialized so no extra probe.
    TEST_ASSERT_EQUAL(2, f.mock.calls.size());
    const MockModbusClient::Call &raw = f.mock.calls[0];
    TEST_ASSERT_EQUAL(static_cast<int>(MockModbusClient::Call::Type::Read),
                      static_cast<int>(raw.type));
    TEST_ASSERT_EQUAL_UINT16(kRegPh, raw.startRegister);
    TEST_ASSERT_EQUAL_UINT16(1, raw.count);
    const MockModbusClient::Call &write = f.mock.calls[1];
    TEST_ASSERT_EQUAL(static_cast<int>(MockModbusClient::Call::Type::Write),
                      static_cast<int>(write.type));
    TEST_ASSERT_EQUAL_UINT8(kAddr, write.deviceAddress);
    TEST_ASSERT_EQUAL_UINT16(kRegPhCalib, write.startRegister);
    TEST_ASSERT_EQUAL_UINT16(1, write.count);
    TEST_ASSERT_EQUAL_UINT16(102, write.value);
    TEST_ASSERT_TRUE(write.succeeded);

    // Read-path effect: raw 68 → 6.8 × factor = 7.0.
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(7.0f, f.sensor.getPH());
}

// --------------------------------------------------------------------------
// calibrateEC(): same flow with unscaled raw (0x0002, rawScale 1), factor
// ×100 written to 0x0102, factor applied on subsequent reads
// --------------------------------------------------------------------------
static void test_calibrate_ec_factor_write_and_read_effect(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(1200.0f, f.sensor.getEC());  // pre-calibration
    f.mock.calls.clear();

    // Raw EC register scripted as 1000 (unscaled); reference 1500 →
    // factor 1.5, written as 150.
    f.mock.setRegisters(kAddr, kRegEc, {1000});
    TEST_ASSERT_TRUE(f.sensor.calibrateEC(1500.0f));
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());

    TEST_ASSERT_EQUAL(2, f.mock.calls.size());
    const MockModbusClient::Call &write = f.mock.calls[1];
    TEST_ASSERT_EQUAL(static_cast<int>(MockModbusClient::Call::Type::Write),
                      static_cast<int>(write.type));
    TEST_ASSERT_EQUAL_UINT16(kRegEcCalib, write.startRegister);
    TEST_ASSERT_EQUAL_UINT16(150, write.value);

    // Read-path effect: 1200 × 1.5 = 1800.
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(1800.0f, f.sensor.getEC());
}

// --------------------------------------------------------------------------
// calibrateMoisture(): the factor is computed and written to 0x0100 (×100)
// but NOT applied in read() — legacy parity: read() publishes raw / 10 with
// "No calibration factor for humidity yet" (see ModbusSoilSensor.cpp header)
// --------------------------------------------------------------------------
static void test_calibrate_moisture_factor_written_but_not_applied(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(55.0f, f.sensor.getMoisture());
    f.mock.calls.clear();

    // Raw moisture register scripted as 500 → 50.0 %; reference 55.0 →
    // factor 1.1, written as 110.
    f.mock.setRegisters(kAddr, kStartReg, {500});  // 1-register key
    TEST_ASSERT_TRUE(f.sensor.calibrateMoisture(55.0f));
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());

    TEST_ASSERT_EQUAL(2, f.mock.calls.size());
    const MockModbusClient::Call &write = f.mock.calls[1];
    TEST_ASSERT_EQUAL(static_cast<int>(MockModbusClient::Call::Type::Write),
                      static_cast<int>(write.type));
    TEST_ASSERT_EQUAL_UINT16(kRegMoistureCalib, write.startRegister);
    TEST_ASSERT_EQUAL_UINT16(110, write.value);

    // NO read-path effect (parity): raw 550 still publishes 55.0, not 60.5.
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(55.0f, f.sensor.getMoisture());
    TEST_ASSERT_EQUAL_FLOAT(55.0f, f.sensor.getHumidity());
}

// --------------------------------------------------------------------------
// Failed calibration-register WRITE is NON-FATAL (parity): calibrate*()
// still returns true, getLastError() carries the write error, and the
// factor is applied locally on the next read()
// --------------------------------------------------------------------------
static void test_calibrate_write_failure_nonfatal_factor_applied(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());
    f.mock.calls.clear();

    f.mock.setRegisters(kAddr, kRegPh, {68});
    f.mock.queueOutcome(MockModbusClient::kOk);      // raw read succeeds
    f.mock.queueOutcome(MockModbusClient::kErrBus);  // factor write fails
    TEST_ASSERT_TRUE(f.sensor.calibratePH(7.0f));    // still succeeds
    TEST_ASSERT_EQUAL_INT(MockModbusClient::kErrBus, f.sensor.getLastError());

    // The write WAS attempted (and failed) — best-effort, single attempt.
    TEST_ASSERT_EQUAL(2, f.mock.calls.size());
    const MockModbusClient::Call &write = f.mock.calls[1];
    TEST_ASSERT_EQUAL(static_cast<int>(MockModbusClient::Call::Type::Write),
                      static_cast<int>(write.type));
    TEST_ASSERT_EQUAL_UINT16(kRegPhCalib, write.startRegister);
    TEST_ASSERT_FALSE(write.succeeded);

    // Factor kept locally despite the failed sensor-register write.
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(7.0f, f.sensor.getPH());
    TEST_ASSERT_EQUAL_INT(0, f.sensor.getLastError());
}

// --------------------------------------------------------------------------
// Raw value < 0.01 → calibration fails with error 5 (division-by-zero
// guard, legacy codes 7/10/13 mapped onto 5), NO factor write, and the
// previous factor (1.0) stays in effect
// --------------------------------------------------------------------------
static void test_calibrate_raw_too_low_error5_no_write(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());
    f.mock.calls.clear();

    f.mock.setRegisters(kAddr, kRegPh, {0});  // raw 0.0 < 0.01
    TEST_ASSERT_FALSE(f.sensor.calibratePH(7.0f));
    TEST_ASSERT_EQUAL_INT(5, f.sensor.getLastError());

    // Only the 1-register raw read reached the bus — no write recorded.
    TEST_ASSERT_EQUAL(1, f.mock.calls.size());
    TEST_ASSERT_EQUAL(static_cast<int>(MockModbusClient::Call::Type::Read),
                      static_cast<int>(f.mock.calls.front().type));

    // Factor unchanged: the next read still publishes the unfactored 6.8.
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(6.8f, f.sensor.getPH());
}

// --------------------------------------------------------------------------
// Validation runs on the FACTORED values (legacy validates after
// multiplying): a factor that pushes pH out of the 3–9 parity range makes
// the next read() fail with error 5 and publish nothing
// --------------------------------------------------------------------------
static void test_calibration_factor_subject_to_range_validation(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(6.8f, f.sensor.getPH());

    // Raw pH 30 → 3.0; reference 9.0 → factor 3.0 (write value 300).
    f.mock.setRegisters(kAddr, kRegPh, {30});
    TEST_ASSERT_TRUE(f.sensor.calibratePH(9.0f));

    // Data payload pH 6.8 × 3.0 = 20.4 > 9 → range validation rejects the
    // whole reading; the last-good (pre-calibration) values survive.
    TEST_ASSERT_FALSE(f.sensor.read());
    TEST_ASSERT_EQUAL_INT(5, f.sensor.getLastError());
    TEST_ASSERT_EQUAL_FLOAT(6.8f, f.sensor.getPH());
    TEST_ASSERT_EQUAL_FLOAT(55.0f, f.sensor.getMoisture());
}

// --------------------------------------------------------------------------
// Failed calibration raw READ is fatal: calibrate*() fails with the
// client's error verbatim, exactly one bus attempt (the raw read), no
// factor write, and the next read() still publishes UNfactored values
// --------------------------------------------------------------------------
static void test_calibrate_raw_read_failure_error_propagated_no_write(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());
    f.mock.calls.clear();

    f.mock.queueOutcome(MockModbusClient::kErrTimeout);  // hits the raw read
    TEST_ASSERT_FALSE(f.sensor.calibratePH(7.0f));
    TEST_ASSERT_EQUAL_INT(MockModbusClient::kErrTimeout,
                          f.sensor.getLastError());

    // Exactly the one failed raw read — no factor write attempted.
    TEST_ASSERT_EQUAL(1, f.mock.calls.size());
    TEST_ASSERT_EQUAL(static_cast<int>(MockModbusClient::Call::Type::Read),
                      static_cast<int>(f.mock.calls.front().type));
    TEST_ASSERT_FALSE(f.mock.calls.front().succeeded);

    // Factor unchanged: the next read still publishes the unfactored 6.8.
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(6.8f, f.sensor.getPH());
}

// --------------------------------------------------------------------------
// Input-hygiene guard (not parity): a non-finite or non-positive reference
// is rejected up front with error 5 — no bus transaction, no factor write,
// factor unchanged
// --------------------------------------------------------------------------
static void test_calibrate_rejects_invalid_reference(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());
    f.mock.calls.clear();

    TEST_ASSERT_FALSE(f.sensor.calibratePH(NAN));
    TEST_ASSERT_EQUAL_INT(5, f.sensor.getLastError());
    TEST_ASSERT_EQUAL(0, f.mock.calls.size());  // guard fires before the bus

    TEST_ASSERT_FALSE(f.sensor.calibratePH(-1.0f));
    TEST_ASSERT_EQUAL_INT(5, f.sensor.getLastError());
    TEST_ASSERT_EQUAL(0, f.mock.calls.size());

    // Factor unchanged: the next read still publishes the unfactored 6.8.
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(6.8f, f.sensor.getPH());
}

// --------------------------------------------------------------------------
// Input-hygiene guard (not parity): a factor whose ×100 register encoding
// would exceed uint16 (float→uint16 overflow was UB in the legacy cast) is
// rejected with error 5 BEFORE being stored — no write, factor unchanged.
// Raw pH 1 → 0.1 (passes the raw-too-low guard); reference 7000 → factor
// 70000 → ×100 = 7,000,000 > 65535.
// --------------------------------------------------------------------------
static void test_calibrate_factor_overflow_rejected_error5(void)
{
    Fixture f(goodPayload());
    TEST_ASSERT_TRUE(f.sensor.read());
    f.mock.calls.clear();

    f.mock.setRegisters(kAddr, kRegPh, {1});
    TEST_ASSERT_FALSE(f.sensor.calibratePH(7000.0f));
    TEST_ASSERT_EQUAL_INT(5, f.sensor.getLastError());

    // Only the raw read reached the bus — the overflowing factor was never
    // written.
    TEST_ASSERT_EQUAL(1, f.mock.calls.size());
    TEST_ASSERT_EQUAL(static_cast<int>(MockModbusClient::Call::Type::Read),
                      static_cast<int>(f.mock.calls.front().type));

    // Factor NOT applied: the next read still publishes the unfactored 6.8.
    TEST_ASSERT_TRUE(f.sensor.read());
    TEST_ASSERT_EQUAL_FLOAT(6.8f, f.sensor.getPH());
}

void run_soil_sensor_tests(void)
{
    RUN_TEST(test_decode_known_payload_negative_temperature);
    RUN_TEST(test_decode_positive_temperature);
    RUN_TEST(test_humidity_equals_moisture);
    RUN_TEST(test_read_is_one_nine_register_transaction);
    RUN_TEST(test_timeout_fails_read_and_keeps_last_good_values);
    RUN_TEST(test_out_of_range_moisture_rejected_error5);
    RUN_TEST(test_out_of_range_temperature_rejected_error5);
    RUN_TEST(test_out_of_range_ph_rejected_all_or_nothing);
    RUN_TEST(test_out_of_range_high_temperature_rejected_error5);
    RUN_TEST(test_out_of_range_low_ph_rejected_error5);
    RUN_TEST(test_range_boundaries_are_inclusive);
    RUN_TEST(test_ec_npk_not_range_enforced);
    RUN_TEST(test_modbus_exception_propagates_verbatim);
    RUN_TEST(test_failed_read_is_single_bus_attempt);
    RUN_TEST(test_read_recovers_after_failure_without_reinit);
    RUN_TEST(test_statistics_count_one_per_call);
    RUN_TEST(test_is_available_performs_real_probe_every_call);
    RUN_TEST(test_set_timeout_reaches_client);
    RUN_TEST(test_lazy_init_client_failure_then_recovery);
    RUN_TEST(test_lazy_init_probe_failure_then_recovery);
    RUN_TEST(test_calibrate_ph_factor_write_and_read_effect);
    RUN_TEST(test_calibrate_ec_factor_write_and_read_effect);
    RUN_TEST(test_calibrate_moisture_factor_written_but_not_applied);
    RUN_TEST(test_calibrate_write_failure_nonfatal_factor_applied);
    RUN_TEST(test_calibrate_raw_too_low_error5_no_write);
    RUN_TEST(test_calibration_factor_subject_to_range_validation);
    RUN_TEST(test_calibrate_raw_read_failure_error_propagated_no_write);
    RUN_TEST(test_calibrate_rejects_invalid_reference);
    RUN_TEST(test_calibrate_factor_overflow_rejected_error5);
}
