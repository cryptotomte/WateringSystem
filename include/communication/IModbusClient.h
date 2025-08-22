// SPDX-FileCopyrightText: 2025 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file IModbusClient.h
 * @brief Interface for Modbus communication
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#ifndef I_MODBUS_CLIENT_H
#define I_MODBUS_CLIENT_H

#include <stdint.h>

/**
 * @brief Interface for Modbus communication
 * 
 * This interface defines the functionality for Modbus RTU communication
 * used for interacting with the soil sensor over RS485.
 */
class IModbusClient {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~IModbusClient() = default;
    
    /**
     * @brief Initialize the Modbus client
     * @return true if initialization successful, false otherwise
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Read holding registers from a Modbus device
     * @param deviceAddress Modbus device address
     * @param startRegister First register to read
     * @param count Number of registers to read
     * @param buffer Buffer to store read values
     * @return true if operation successful, false otherwise
     */
    virtual bool readHoldingRegisters(uint8_t deviceAddress, uint16_t startRegister, 
                                     uint16_t count, uint16_t* buffer) = 0;
    
    /**
     * @brief Write single register to a Modbus device
     * @param deviceAddress Modbus device address
     * @param registerAddress Register address to write
     * @param value Value to write
     * @return true if operation successful, false otherwise
     */
    virtual bool writeSingleRegister(uint8_t deviceAddress, uint16_t registerAddress, 
                                    uint16_t value) = 0;
    
    /**
     * @brief Get last error code
     * @return error code, 0 if no error
     */
    virtual int getLastError() = 0;
    
    /**
     * @brief Set communication timeout
     * @param timeoutMs Timeout in milliseconds
     */
    virtual void setTimeout(uint32_t timeoutMs) = 0;
    
    /**
     * @brief Get communication statistics
     * @param successCount Pointer to store successful transaction count
     * @param errorCount Pointer to store failed transaction count
     */
    virtual void getStatistics(uint32_t* successCount, uint32_t* errorCount) = 0;
};

#endif // I_MODBUS_CLIENT_H