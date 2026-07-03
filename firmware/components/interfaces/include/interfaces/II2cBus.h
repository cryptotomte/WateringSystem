// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file II2cBus.h
 * @brief Minimal register-oriented I2C master interface (hardware seam).
 *
 * The host-test seam for I2C sensor drivers (research.md R6): Bme280Sensor
 * holds all policy above this interface and is host-tested against
 * MockI2cBus; EspI2cBus is the only hardware-touching implementation.
 * Normative contract: specs/005-bme280-i2c/contracts/interfaces.md.
 *
 * This interface carries no BME280 knowledge — PR-05's INA226 driver reuses
 * it on the same bus instance. PR-05 exercised the extension option its
 * contract sanctioned ("may extend the interface or compose two 8-bit
 * operations") and added the 16-bit register operations: writeRegister16()
 * as a new virtual (the INA226's pointer + two data bytes must land in ONE
 * transaction — not composable from single-byte writes) and readRegister16()
 * as a non-virtual convenience over readRegisters() (feature 006,
 * research.md R6; specs/006-level-sensors-ina226/contracts/interfaces.md).
 *
 * Part of the header-only `interfaces` component: no IDF includes allowed.
 */

#ifndef WATERINGSYSTEM_INTERFACES_II2CBUS_H
#define WATERINGSYSTEM_INTERFACES_II2CBUS_H

#include <cstddef>
#include <cstdint>

/**
 * @brief I2C master: probe + 8/16-bit register reads/writes, one
 * transaction per call.
 *
 * All addresses are 7-bit. Every method returns false on NACK, bus error
 * or timeout; there are NO retries at this layer — recovery policy belongs
 * to the sensor driver above. Implementations serialize safely for
 * multi-task use at transaction granularity (the ESP implementation
 * inherits this from the i2c_master driver's bus lock, research.md R3;
 * mocks are used single-threaded in host tests).
 */
class II2cBus {
public:
    virtual ~II2cBus() = default;

    /**
     * @brief Check whether a device ACKs at @p address7.
     *
     * @param address7 7-bit device address.
     * @return true if the device acknowledged its address.
     */
    virtual bool probe(uint8_t address7) = 0;

    /**
     * @brief Read @p len consecutive registers starting at @p startReg.
     *
     * One transaction: register-pointer write followed by an N-byte read
     * with a REPEATED START in between — required for correct BME280 burst
     * reads (the chip's register shadowing guarantees a consistent
     * measurement only within a single burst transaction).
     *
     * @param address7 7-bit device address.
     * @param startReg First register address.
     * @param buf Caller-owned buffer of at least @p len bytes; contents are
     *            defined only when the call returns true.
     * @param len Number of registers (bytes) to read.
     * @return true if all requested bytes were read.
     */
    virtual bool readRegisters(uint8_t address7, uint8_t startReg,
                               uint8_t* buf, size_t len) = 0;

    /**
     * @brief Write one byte to one register (register pointer + value in a
     * single transaction).
     *
     * @param address7 7-bit device address.
     * @param reg Register address.
     * @param value Value to write.
     * @return true if the device acknowledged the write.
     */
    virtual bool writeRegister(uint8_t address7, uint8_t reg,
                               uint8_t value) = 0;

    /**
     * @brief Write one 16-bit register value, BIG-ENDIAN, in a single
     * transaction (register pointer + MSB + LSB).
     *
     * Required by the INA226 configuration/calibration writes (feature
     * 006): the device latches a 16-bit value per write transaction, so
     * this is NOT composable from two writeRegister() calls. Same error
     * semantics as writeRegister(): false on NACK/bus error/timeout, no
     * retries at this layer.
     *
     * @param address7 7-bit device address.
     * @param reg Register address.
     * @param value Value to write; transmitted MSB first (big-endian).
     * @return true if the device acknowledged the write.
     */
    virtual bool writeRegister16(uint8_t address7, uint8_t reg,
                                 uint16_t value) = 0;

    /**
     * @brief Read one 16-bit register value, BIG-ENDIAN decoded.
     *
     * Non-virtual convenience implemented on the interface over
     * readRegisters(..., 2) — a 16-bit read is already expressible as a
     * 2-byte burst, so implementations provide nothing extra (feature 006,
     * research.md R6). Exists for symmetry with writeRegister16() and
     * call-site clarity in the INA226 driver.
     *
     * @param address7 7-bit device address.
     * @param reg Register address.
     * @param out Decoded value (MSB-first); defined only when the call
     *            returns true.
     * @return true if both bytes were read.
     */
    bool readRegister16(uint8_t address7, uint8_t reg, uint16_t& out)
    {
        uint8_t buf[2] = {0, 0};
        if (!readRegisters(address7, reg, buf, sizeof(buf))) {
            return false;
        }
        out = static_cast<uint16_t>((static_cast<uint16_t>(buf[0]) << 8) |
                                    buf[1]);
        return true;
    }
};

#endif /* WATERINGSYSTEM_INTERFACES_II2CBUS_H */
