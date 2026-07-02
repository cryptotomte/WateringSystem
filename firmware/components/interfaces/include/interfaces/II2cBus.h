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
 * it on the same bus instance (INA226 uses 16-bit register values; PR-05
 * may extend the interface or compose two 8-bit operations, its call).
 *
 * Part of the header-only `interfaces` component: no IDF includes allowed.
 */

#ifndef WATERINGSYSTEM_INTERFACES_II2CBUS_H
#define WATERINGSYSTEM_INTERFACES_II2CBUS_H

#include <cstddef>
#include <cstdint>

/**
 * @brief I2C master: probe + 8-bit register reads/writes, one transaction
 * per call.
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
};

#endif /* WATERINGSYSTEM_INTERFACES_II2CBUS_H */
