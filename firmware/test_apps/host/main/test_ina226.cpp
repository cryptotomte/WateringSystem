// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_ina226.cpp
 * @brief Host tests for the INA226 path (linux target).
 *
 * Suite run_ina226_tests(), registered from the shared Unity runner
 * (test_main.cpp); the process exit code equals the failure count and is
 * the CI gate.
 *
 * Coverage: tasks.md T006 — the II2cBus 16-bit extension at the mock
 * level: writeRegister16() recording big-endian into the per-address byte
 * map + call log (byte-level assertions stay valid), the readRegister16()
 * big-endian helper (non-virtual, over readRegisters()), outcome scripting
 * shared across write widths, the uint8_t register index wrap convention,
 * and the word-register overlay (setRegister16(), pointer-addressed INA226
 * model — adjacent 16-bit registers never collide). Tasks.md T018 — the
 * REAL Ina226Sensor driver over MockI2cBus: config/calibration write bytes
 * asserted big-endian in order, the 5 mΩ/0.5 mA operating point
 * (CAL = 2048), scaling vectors hand-computed from the SBOS547 formulas
 * (LSB units, a realistic pump vector, a negative-current vector),
 * identity mismatch / absent device → error 1, mid-read bus error →
 * error 2 + last-good retention + re-probe recovery, mid-init write
 * failure → error 2, live isAvailable() probe, and the LockedPowerSensor
 * delegation check.
 */

#include <cmath>
#include <cstdint>

#include "unity.h"

#include "sensors/Ina226Sensor.h"
#include "sensors/LockedPowerSensor.h"
#include "sensors/testing/MockI2cBus.h"

namespace {

constexpr uint8_t kAddr = 0x40;      ///< rev2 pump INA226 (A0 = A1 = GND)
constexpr uint8_t kRegConfig = 0x00;
constexpr uint8_t kRegBusVoltage = 0x02;
constexpr uint8_t kRegPower = 0x03;
constexpr uint8_t kRegCurrent = 0x04;
constexpr uint8_t kRegCalibration = 0x05;
constexpr uint8_t kRegManufacturerId = 0xFE;
constexpr uint8_t kRegDieId = 0xFF;

/// Default shunt (rev2 BOM R1, Kconfig default): 5 mΩ.
constexpr uint32_t kShuntMilliOhm = 5;

// ---------------------------------------------------------------------------
// writeRegister16: big-endian into the byte map + call log (T006)
// ---------------------------------------------------------------------------

void test_write16_lands_big_endian_in_register_map(void)
{
    MockI2cBus bus;
    bus.addDevice(kAddr);

    TEST_ASSERT_TRUE(bus.writeRegister16(kAddr, kRegConfig, 0x4127));

    // Byte-level view (the assertion style the BME280 suite relies on —
    // 16-bit writes must not bypass the shared byte map): MSB first.
    uint8_t raw[2] = {0, 0};
    TEST_ASSERT_TRUE(bus.readRegisters(kAddr, kRegConfig, raw, 2));
    TEST_ASSERT_EQUAL_HEX8(0x41, raw[0]);
    TEST_ASSERT_EQUAL_HEX8(0x27, raw[1]);
}

void test_write16_read16_roundtrip(void)
{
    MockI2cBus bus;
    bus.addDevice(kAddr);

    // CAL = 2048 (0x0800) — the 5 mΩ / 0.5 mA operating point.
    TEST_ASSERT_TRUE(bus.writeRegister16(kAddr, kRegCalibration, 0x0800));

    uint16_t value = 0;
    TEST_ASSERT_TRUE(bus.readRegister16(kAddr, kRegCalibration, value));
    TEST_ASSERT_EQUAL_HEX16(0x0800, value);
}

void test_write16_recorded_in_call_log(void)
{
    MockI2cBus bus;
    bus.addDevice(kAddr);

    TEST_ASSERT_TRUE(bus.writeRegister16(kAddr, kRegConfig, 0x4127));

    TEST_ASSERT_EQUAL(1, bus.calls.size());
    const MockI2cBus::Call& call = bus.calls[0];
    TEST_ASSERT_TRUE(call.type == MockI2cBus::Call::Type::Write16);
    TEST_ASSERT_EQUAL_HEX8(kAddr, call.address);
    TEST_ASSERT_EQUAL_HEX8(kRegConfig, call.reg);
    TEST_ASSERT_EQUAL(2, call.len);
    TEST_ASSERT_EQUAL_HEX16(0x4127, call.value16);
    TEST_ASSERT_TRUE(call.succeeded);
}

// ---------------------------------------------------------------------------
// readRegister16: big-endian decode of scripted bytes (T006)
// ---------------------------------------------------------------------------

void test_read16_decodes_big_endian(void)
{
    MockI2cBus bus;
    // Manufacturer ID register scripted byte-wise: 0x5449 ("TI") is
    // {0x54, 0x49} MSB first on the wire.
    bus.setRegisters(kAddr, 0xFE, {0x54, 0x49});

    uint16_t value = 0;
    TEST_ASSERT_TRUE(bus.readRegister16(kAddr, 0xFE, value));
    TEST_ASSERT_EQUAL_HEX16(0x5449, value);
}

// ---------------------------------------------------------------------------
// Failure modes: absent device + scripted outcomes (T006)
// ---------------------------------------------------------------------------

void test_write16_fails_on_absent_device(void)
{
    MockI2cBus bus;  // nothing at kAddr — address NACK

    TEST_ASSERT_FALSE(bus.writeRegister16(kAddr, kRegConfig, 0x4127));
    TEST_ASSERT_EQUAL(1, bus.calls.size());
    TEST_ASSERT_FALSE(bus.calls[0].succeeded);

    uint16_t value = 0;
    TEST_ASSERT_FALSE(bus.readRegister16(kAddr, kRegConfig, value));
}

void test_write16_queued_failure_leaves_map_untouched(void)
{
    MockI2cBus bus;
    bus.addDevice(kAddr);
    bus.setRegisters(kAddr, kRegCalibration, {0xAA, 0xBB});  // pre-existing

    bus.queueWriteOutcome(false);  // mid-sequence bus error on a PRESENT device
    TEST_ASSERT_FALSE(bus.writeRegister16(kAddr, kRegCalibration, 0x0800));

    // A failed write never lands in the register map.
    uint16_t value = 0;
    TEST_ASSERT_TRUE(bus.readRegister16(kAddr, kRegCalibration, value));
    TEST_ASSERT_EQUAL_HEX16(0xAABB, value);
}

void test_write_outcome_queue_shared_across_widths_in_order(void)
{
    MockI2cBus bus;
    bus.addDevice(kAddr);

    // ONE FIFO covers 8- and 16-bit writes in call order (documented mock
    // contract) — an INA226 config-then-calibration script mixes widths
    // freely.
    bus.queueWriteOutcome(true);
    bus.queueWriteOutcome(false);
    TEST_ASSERT_TRUE(bus.writeRegister(kAddr, 0x10, 0x01));       // consumes #1
    TEST_ASSERT_FALSE(bus.writeRegister16(kAddr, 0x11, 0x0203));  // consumes #2
    TEST_ASSERT_TRUE(bus.writeRegister16(kAddr, 0x12, 0x0405));   // queue empty
}

// ---------------------------------------------------------------------------
// Register index wrap at 0xFF (mock convention, matches setRegisters())
// ---------------------------------------------------------------------------

void test_write16_wraps_register_index(void)
{
    MockI2cBus bus;
    bus.addDevice(kAddr);

    // MSB at 0xFF, LSB wraps to 0x00 (uint8_t index arithmetic — the same
    // convention readRegisters()/setRegisters() use).
    TEST_ASSERT_TRUE(bus.writeRegister16(kAddr, 0xFF, 0x1234));

    uint8_t msb = 0;
    uint8_t lsb = 0;
    TEST_ASSERT_TRUE(bus.readRegisters(kAddr, 0xFF, &msb, 1));
    TEST_ASSERT_TRUE(bus.readRegisters(kAddr, 0x00, &lsb, 1));
    TEST_ASSERT_EQUAL_HEX8(0x12, msb);
    TEST_ASSERT_EQUAL_HEX8(0x34, lsb);
}

// ---------------------------------------------------------------------------
// Word-register overlay: pointer-addressed 16-bit registers (T018 enabler)
// ---------------------------------------------------------------------------

void test_mock_word_registers_adjacent_no_collision(void)
{
    MockI2cBus bus;
    // The INA226 data registers are ADJACENT pointer addresses; in the
    // byte-map model their MSB/LSB pairs would overwrite each other
    // (0x02's LSB slot is 0x03's MSB slot). The word overlay keeps each
    // 16-bit register independent — the model real hardware implements.
    bus.setRegister16(kAddr, kRegBusVoltage, 0x2580);
    bus.setRegister16(kAddr, kRegPower, 0x0F00);
    bus.setRegister16(kAddr, kRegCurrent, 0x1F40);

    uint16_t value = 0;
    TEST_ASSERT_TRUE(bus.readRegister16(kAddr, kRegBusVoltage, value));
    TEST_ASSERT_EQUAL_HEX16(0x2580, value);
    TEST_ASSERT_TRUE(bus.readRegister16(kAddr, kRegPower, value));
    TEST_ASSERT_EQUAL_HEX16(0x0F00, value);
    TEST_ASSERT_TRUE(bus.readRegister16(kAddr, kRegCurrent, value));
    TEST_ASSERT_EQUAL_HEX16(0x1F40, value);
}

// ---------------------------------------------------------------------------
// Ina226Sensor driver (T018). Scripting helpers: a healthy INA226 answers
// its identity registers (0xFE = 0x5449 "TI", 0xFF = 0x2260) and serves the
// three data registers via the word overlay.
// ---------------------------------------------------------------------------

void script_identity(MockI2cBus& bus)
{
    bus.addDevice(kAddr);
    bus.setRegister16(kAddr, kRegManufacturerId, 0x5449);
    bus.setRegister16(kAddr, kRegDieId, 0x2260);
}

void script_readings(MockI2cBus& bus, uint16_t rawBus, uint16_t rawPower,
                     uint16_t rawCurrent)
{
    bus.setRegister16(kAddr, kRegBusVoltage, rawBus);
    bus.setRegister16(kAddr, kRegPower, rawPower);
    bus.setRegister16(kAddr, kRegCurrent, rawCurrent);
}

// --- initialization: write sequence + operating point ---------------------

void test_ina226_calibration_operating_point(void)
{
    // CAL = 0.00512 / (Current_LSB × R_shunt), Current_LSB = 0.5 mA:
    //   5 mΩ:    0.00512 / (0.0005 A × 0.005 Ω) = 0.00512 / 2.5e-6 = 2048
    //   10 mΩ:   0.00512 / (0.0005 A × 0.010 Ω) = 1024
    //   1 mΩ:    10240 (range floor — still fits u16)
    //   1000 mΩ: 10.24 → 10 (range ceiling, integer truncation)
    TEST_ASSERT_EQUAL_UINT16(2048, Ina226Sensor::calibrationFor(5));
    TEST_ASSERT_EQUAL_UINT16(1024, Ina226Sensor::calibrationFor(10));
    TEST_ASSERT_EQUAL_UINT16(10240, Ina226Sensor::calibrationFor(1));
    TEST_ASSERT_EQUAL_UINT16(10, Ina226Sensor::calibrationFor(1000));
}

void test_ina226_initialize_writes_config_then_calibration(void)
{
    MockI2cBus bus;
    script_identity(bus);
    Ina226Sensor sensor(bus, kAddr, kShuntMilliOhm);

    TEST_ASSERT_TRUE(sensor.initialize());
    TEST_ASSERT_EQUAL_INT(0, sensor.getLastError());

    // Exactly two 16-bit writes, config BEFORE calibration:
    //
    //   config (0x00) = 0x4527, derived from the SBOS547 field layout:
    //     [15] RST=0 | [14:12] reserved=100 (bit 14 is 1 at POR, kept) |
    //     [11:9] AVG=010 (16 samples) | [8:6] VBUSCT=100 (1.1 ms) |
    //     [5:3] VSHCT=100 (1.1 ms) | [2:0] MODE=111 (continuous shunt+bus)
    //     → 0b0100'0101'0010'0111 = 0x4527
    //   calibration (0x05) = 2048 = 0x0800 (5 mΩ operating point above)
    int write16Count = 0;
    for (const MockI2cBus::Call& call : bus.calls) {
        if (call.type != MockI2cBus::Call::Type::Write16) {
            continue;
        }
        ++write16Count;
        if (write16Count == 1) {
            TEST_ASSERT_EQUAL_HEX8(kRegConfig, call.reg);
            TEST_ASSERT_EQUAL_HEX16(0x4527, call.value16);
        } else if (write16Count == 2) {
            TEST_ASSERT_EQUAL_HEX8(kRegCalibration, call.reg);
            TEST_ASSERT_EQUAL_HEX16(0x0800, call.value16);
        }
    }
    TEST_ASSERT_EQUAL_INT(2, write16Count);

    // Byte-level big-endian view (the wire order): MSB first in the byte
    // map for both writes.
    uint8_t raw = 0;
    TEST_ASSERT_TRUE(bus.readRegisters(kAddr, kRegConfig, &raw, 1));
    TEST_ASSERT_EQUAL_HEX8(0x45, raw);
    TEST_ASSERT_TRUE(bus.readRegisters(kAddr, kRegConfig + 1, &raw, 1));
    TEST_ASSERT_EQUAL_HEX8(0x27, raw);
    TEST_ASSERT_TRUE(bus.readRegisters(kAddr, kRegCalibration, &raw, 1));
    TEST_ASSERT_EQUAL_HEX8(0x08, raw);
    TEST_ASSERT_TRUE(bus.readRegisters(kAddr, kRegCalibration + 1, &raw, 1));
    TEST_ASSERT_EQUAL_HEX8(0x00, raw);
}

// --- scaling vectors (hand-computed from the SBOS547 formulas) ------------

void test_ina226_getters_nan_before_first_read(void)
{
    MockI2cBus bus;
    script_identity(bus);
    Ina226Sensor sensor(bus, kAddr, kShuntMilliOhm);
    TEST_ASSERT_TRUE(sensor.initialize());

    // Initialized but never read: the placeholders are self-announcing
    // NaN, never a plausible 0.0 (interface contract).
    TEST_ASSERT_TRUE(std::isnan(sensor.getBusVoltage()));
    TEST_ASSERT_TRUE(std::isnan(sensor.getCurrent()));
    TEST_ASSERT_TRUE(std::isnan(sensor.getPower()));
}

void test_ina226_read_scales_lsb_units(void)
{
    MockI2cBus bus;
    script_identity(bus);
    // Raw 1 in every register pins the LSB weights themselves:
    //   bus:     1 × 1.25 mV            = 0.00125 V
    //   power:   1 × 25 × 0.5 mA        = 12.5 mW = 0.0125 W
    //   current: 1 × 0.5 mA             = 0.0005 A
    script_readings(bus, 1, 1, 1);
    Ina226Sensor sensor(bus, kAddr, kShuntMilliOhm);

    TEST_ASSERT_TRUE(sensor.read());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.00125f, sensor.getBusVoltage());
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, 0.0005f, sensor.getCurrent());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0125f, sensor.getPower());
}

void test_ina226_read_scales_pump_vector(void)
{
    MockI2cBus bus;
    script_identity(bus);
    // Realistic rev2 pump operating point (~12 V, ~4 A):
    //   bus:     12.000 V / 1.25 mV  = 9600 = 0x2580 → 9600 × 0.00125
    //                                                 = 12.000 V
    //   current:  4.000 A / 0.5 mA   = 8000 = 0x1F40 → 8000 × 0.0005
    //                                                 =  4.000 A
    //   power:   48.000 W / 12.5 mW  = 3840 = 0x0F00 → 3840 × 0.0125
    //                                                 = 48.000 W
    //   (coherent: 12 V × 4 A = 48 W — the device computes power from
    //   current × busV internally; the script mirrors that)
    script_readings(bus, 0x2580, 0x0F00, 0x1F40);
    Ina226Sensor sensor(bus, kAddr, kShuntMilliOhm);

    TEST_ASSERT_TRUE(sensor.read());
    TEST_ASSERT_EQUAL_INT(0, sensor.getLastError());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, sensor.getBusVoltage());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, sensor.getCurrent());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 48.0f, sensor.getPower());
}

void test_ina226_negative_current_sign_preserved(void)
{
    MockI2cBus bus;
    script_identity(bus);
    // The current register is SIGNED two's complement (SBOS547): raw
    // 0xE0C0 = 57536 unsigned; as int16: 57536 − 65536 = −8000 →
    // −8000 × 0.5 mA = −4.000 A. The sign must survive — never wrapped to
    // +28.768 A (57536 × 0.5 mA). Bus voltage and power registers stay
    // unsigned.
    script_readings(bus, 0x2580, 0x0F00, 0xE0C0);
    Ina226Sensor sensor(bus, kAddr, kShuntMilliOhm);

    TEST_ASSERT_TRUE(sensor.read());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -4.0f, sensor.getCurrent());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, sensor.getBusVoltage());
}

// --- error paths: identity, absence, bus errors, recovery -----------------

void test_ina226_identity_mismatch_is_error_1(void)
{
    MockI2cBus bus;
    // A device ACKs at 0x40 but is not an INA226: right manufacturer,
    // wrong die ID (e.g. a future pin-compatible part) — rejected with
    // error 1, and the config/calibration writes never happen.
    bus.addDevice(kAddr);
    bus.setRegister16(kAddr, kRegManufacturerId, 0x5449);
    bus.setRegister16(kAddr, kRegDieId, 0x2261);
    Ina226Sensor sensor(bus, kAddr, kShuntMilliOhm);

    TEST_ASSERT_FALSE(sensor.initialize());
    TEST_ASSERT_EQUAL_INT(1, sensor.getLastError());
    for (const MockI2cBus::Call& call : bus.calls) {
        TEST_ASSERT_TRUE(call.type != MockI2cBus::Call::Type::Write16);
    }

    // Foreign manufacturer entirely (correct die by coincidence): same
    // rejection.
    MockI2cBus bus2;
    bus2.addDevice(kAddr);
    bus2.setRegister16(kAddr, kRegManufacturerId, 0x5448);
    bus2.setRegister16(kAddr, kRegDieId, 0x2260);
    Ina226Sensor sensor2(bus2, kAddr, kShuntMilliOhm);
    TEST_ASSERT_FALSE(sensor2.initialize());
    TEST_ASSERT_EQUAL_INT(1, sensor2.getLastError());
}

void test_ina226_absent_device_is_error_1(void)
{
    MockI2cBus bus;  // nothing at 0x40 — address NACK
    Ina226Sensor sensor(bus, kAddr, kShuntMilliOhm);

    TEST_ASSERT_FALSE(sensor.initialize());
    TEST_ASSERT_EQUAL_INT(1, sensor.getLastError());

    // read() lazy-inits and fails the same way; placeholders stay NaN.
    TEST_ASSERT_FALSE(sensor.read());
    TEST_ASSERT_EQUAL_INT(1, sensor.getLastError());
    TEST_ASSERT_TRUE(std::isnan(sensor.getBusVoltage()));
    TEST_ASSERT_TRUE(std::isnan(sensor.getCurrent()));
    TEST_ASSERT_TRUE(std::isnan(sensor.getPower()));
}

void test_ina226_mid_init_write_failure_is_error_2(void)
{
    MockI2cBus bus;
    script_identity(bus);
    Ina226Sensor sensor(bus, kAddr, kShuntMilliOhm);

    // The device identified, then the config write fails: error 2 (a
    // communication failure AFTER identification, distinct from error 1),
    // and the driver stays uninitialized.
    bus.queueWriteOutcome(false);
    TEST_ASSERT_FALSE(sensor.initialize());
    TEST_ASSERT_EQUAL_INT(2, sensor.getLastError());

    // The next attempt re-probes the identity from scratch and succeeds
    // (write queue exhausted = success).
    const size_t probesBefore = [&] {
        size_t n = 0;
        for (const MockI2cBus::Call& call : bus.calls) {
            n += call.type == MockI2cBus::Call::Type::Probe ? 1 : 0;
        }
        return n;
    }();
    TEST_ASSERT_TRUE(sensor.initialize());
    TEST_ASSERT_EQUAL_INT(0, sensor.getLastError());
    size_t probesAfter = 0;
    for (const MockI2cBus::Call& call : bus.calls) {
        probesAfter += call.type == MockI2cBus::Call::Type::Probe ? 1 : 0;
    }
    TEST_ASSERT_EQUAL(probesBefore + 1, probesAfter);
}

void test_ina226_mid_read_bus_error_lastgood_and_recovery(void)
{
    MockI2cBus bus;
    script_identity(bus);
    script_readings(bus, 0x2580, 0x0F00, 0x1F40);  // 12 V / 48 W / 4 A
    Ina226Sensor sensor(bus, kAddr, kShuntMilliOhm);
    TEST_ASSERT_TRUE(sensor.read());

    // Mid-read bus error on a PRESENT device: error 2, the last-good
    // triple is untouched, and the driver drops to uninitialized.
    bus.queueReadOutcome(false);
    TEST_ASSERT_FALSE(sensor.read());
    TEST_ASSERT_EQUAL_INT(2, sensor.getLastError());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, sensor.getBusVoltage());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, sensor.getCurrent());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 48.0f, sensor.getPower());

    // Recovery: the next read() re-probes the identity (init sequence
    // reruns — config + calibration written again) and delivers fresh
    // values. New operating point: 12.5 V / 2 A / 25 W:
    //   bus:     12.5 V / 1.25 mV = 10000 = 0x2710
    //   current:  2.0 A / 0.5 mA  =  4000 = 0x0FA0
    //   power:   25.0 W / 12.5 mW =  2000 = 0x07D0
    script_readings(bus, 0x2710, 0x07D0, 0x0FA0);
    const size_t write16Before = [&] {
        size_t n = 0;
        for (const MockI2cBus::Call& call : bus.calls) {
            n += call.type == MockI2cBus::Call::Type::Write16 ? 1 : 0;
        }
        return n;
    }();
    TEST_ASSERT_TRUE(sensor.read());
    TEST_ASSERT_EQUAL_INT(0, sensor.getLastError());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f, sensor.getBusVoltage());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, sensor.getCurrent());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, sensor.getPower());
    size_t write16After = 0;
    for (const MockI2cBus::Call& call : bus.calls) {
        write16After += call.type == MockI2cBus::Call::Type::Write16 ? 1 : 0;
    }
    TEST_ASSERT_EQUAL(write16Before + 2, write16After);  // re-configured
}

void test_ina226_is_available_live_probe(void)
{
    MockI2cBus bus;
    script_identity(bus);
    script_readings(bus, 0x2580, 0x0F00, 0x1F40);
    Ina226Sensor sensor(bus, kAddr, kShuntMilliOhm);
    TEST_ASSERT_TRUE(sensor.read());

    // Available while the device answers its identity.
    TEST_ASSERT_TRUE(sensor.isAvailable());

    // Unplug: the availability probe detects the loss LIVE (never cached)
    // and must not touch the reading's error code (still 0 from the
    // successful read above).
    bus.removeDevice(kAddr);
    TEST_ASSERT_FALSE(sensor.isAvailable());
    TEST_ASSERT_EQUAL_INT(0, sensor.getLastError());

    // Replug: the lazy re-init path recovers (isAvailable() delegates to
    // initialize(), which owns its own error reporting).
    script_identity(bus);
    script_readings(bus, 0x2580, 0x0F00, 0x1F40);
    TEST_ASSERT_TRUE(sensor.isAvailable());
    TEST_ASSERT_TRUE(sensor.read());
}

// --- LockedPowerSensor: pure delegation ------------------------------------

void test_locked_power_sensor_delegates(void)
{
    MockI2cBus bus;
    script_identity(bus);
    script_readings(bus, 0x2580, 0x0F00, 0x1F40);
    Ina226Sensor raw(bus, kAddr, kShuntMilliOhm);
    LockedPowerSensor sensor(raw);

    TEST_ASSERT_TRUE(sensor.initialize());
    TEST_ASSERT_TRUE(sensor.isAvailable());
    TEST_ASSERT_TRUE(sensor.read());
    TEST_ASSERT_EQUAL_INT(0, sensor.getLastError());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, sensor.getBusVoltage());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, sensor.getCurrent());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 48.0f, sensor.getPower());
}

}  // namespace

void run_ina226_tests(void)
{
    RUN_TEST(test_write16_lands_big_endian_in_register_map);
    RUN_TEST(test_write16_read16_roundtrip);
    RUN_TEST(test_write16_recorded_in_call_log);
    RUN_TEST(test_read16_decodes_big_endian);
    RUN_TEST(test_write16_fails_on_absent_device);
    RUN_TEST(test_write16_queued_failure_leaves_map_untouched);
    RUN_TEST(test_write_outcome_queue_shared_across_widths_in_order);
    RUN_TEST(test_write16_wraps_register_index);
    RUN_TEST(test_mock_word_registers_adjacent_no_collision);
    RUN_TEST(test_ina226_calibration_operating_point);
    RUN_TEST(test_ina226_initialize_writes_config_then_calibration);
    RUN_TEST(test_ina226_getters_nan_before_first_read);
    RUN_TEST(test_ina226_read_scales_lsb_units);
    RUN_TEST(test_ina226_read_scales_pump_vector);
    RUN_TEST(test_ina226_negative_current_sign_preserved);
    RUN_TEST(test_ina226_identity_mismatch_is_error_1);
    RUN_TEST(test_ina226_absent_device_is_error_1);
    RUN_TEST(test_ina226_mid_init_write_failure_is_error_2);
    RUN_TEST(test_ina226_mid_read_bus_error_lastgood_and_recovery);
    RUN_TEST(test_ina226_is_available_live_probe);
    RUN_TEST(test_locked_power_sensor_delegates);
}
