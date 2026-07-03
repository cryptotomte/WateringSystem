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
 * This file currently covers tasks.md T006: the II2cBus 16-bit extension
 * at the mock level — writeRegister16() recording big-endian into the
 * per-address byte map + call log (byte-level assertions stay valid), the
 * readRegister16() big-endian helper (non-virtual, over readRegisters()),
 * outcome scripting shared across write widths, and the uint8_t register
 * index wrap convention. The Ina226Sensor driver tests (T018: scaling
 * vectors, identity, error paths) are added to this suite by US3.
 */

#include <cstdint>

#include "unity.h"

#include "sensors/testing/MockI2cBus.h"

namespace {

constexpr uint8_t kAddr = 0x40;      ///< rev2 pump INA226 (A0 = A1 = GND)
constexpr uint8_t kRegConfig = 0x00;
constexpr uint8_t kRegCalibration = 0x05;

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
}
