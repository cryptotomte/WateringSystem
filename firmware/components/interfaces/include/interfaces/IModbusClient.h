// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file IModbusClient.h
 * @brief Modbus RTU master interface for the RS485 sensor bus.
 *
 * Ported from the frozen Arduino firmware (include/communication/
 * IModbusClient.h) with the same method surface; Arduino-era base-class
 * baggage is trimmed. Normative contract:
 * specs/004-modbus-soil-sensor/contracts/interfaces.md.
 *
 * Error codes reported by getLastError()
 * (specs/004-modbus-soil-sensor/data-model.md):
 *
 *   0      OK
 *   1      not initialized
 *   2      bus/communication error (CRC, framing, truncated response)
 *   3      timeout (no response within the configured timeout)
 *   5      range validation failed — set by the sensor layer on top of this
 *          interface, never by IModbusClient implementations themselves
 *   100+n  Modbus slave exception n
 *
 * Part of the header-only `interfaces` component: no IDF includes allowed.
 */

#ifndef WATERINGSYSTEM_INTERFACES_IMODBUSCLIENT_H
#define WATERINGSYSTEM_INTERFACES_IMODBUSCLIENT_H

#include <cstdint>

/**
 * @brief Modbus RTU master: single-attempt register reads/writes.
 */
class IModbusClient {
public:
    virtual ~IModbusClient() = default;

    /**
     * @brief Bring up the bus (UART/transceiver). Must precede any transfer.
     *
     * @return true on success; false leaves the client unusable (subsequent
     *         transfers fail with error 1).
     */
    virtual bool initialize() = 0;

    /**
     * @brief Read holding registers (Modbus function 0x03).
     *
     * Performs exactly ONE bus attempt — no internal retry (parity,
     * docs/parity-checklist.md §5); recovery comes from the caller's read
     * cadence. On failure returns false and getLastError() carries the code.
     *
     * @param deviceAddress Modbus slave address.
     * @param startRegister First holding register address.
     * @param count Number of consecutive registers to read.
     * @param buffer Caller-owned array of at least `count` elements; filled
     *               only on success.
     * @return true if all requested registers were read.
     */
    virtual bool readHoldingRegisters(uint8_t deviceAddress, uint16_t startRegister,
                                      uint16_t count, uint16_t* buffer) = 0;

    /**
     * @brief Write a single holding register (Modbus function 0x06).
     *
     * Exactly one bus attempt, no retry (parity). Success requires the
     * slave's echo response to be received and verified — a sent-but-not-
     * acknowledged write reports false.
     *
     * @param deviceAddress Modbus slave address.
     * @param registerAddress Holding register address.
     * @param value Value to write.
     * @return true if the write was echoed back by the slave.
     */
    virtual bool writeSingleRegister(uint8_t deviceAddress, uint16_t registerAddress,
                                     uint16_t value) = 0;

    /**
     * @brief Error code of the most recent operation (0 = OK; table above).
     */
    virtual int getLastError() = 0;

    /**
     * @brief Set the response timeout for subsequent transfers.
     *
     * Parity default is 3000 ms (docs/parity-checklist.md §5).
     *
     * @param timeoutMs Timeout in milliseconds.
     */
    virtual void setTimeout(uint32_t timeoutMs) = 0;

    /**
     * @brief Cumulative transaction statistics.
     *
     * Every readHoldingRegisters()/writeSingleRegister() call increments
     * exactly one of the two counters; reading the statistics increments
     * neither.
     *
     * @param successCount Out: number of successful transfers.
     * @param errorCount Out: number of failed transfers.
     */
    virtual void getStatistics(uint32_t* successCount, uint32_t* errorCount) = 0;
};

#endif /* WATERINGSYSTEM_INTERFACES_IMODBUSCLIENT_H */
