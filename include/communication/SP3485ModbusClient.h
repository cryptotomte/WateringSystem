/**
 * @file SP3485ModbusClient.h
 * @brief SP3485 based Modbus RTU client implementation
 * @author WateringSystem Team
 * @date 2025-04-15
 */

#pragma once

#include "communication/IModbusClient.h"
#include <HardwareSerial.h>

/**
 * @brief Implementation of Modbus client interface for SP3485 RS485 transceiver
 * 
 * This class provides a concrete implementation of the IModbusClient
 * interface for communicating with Modbus RTU devices over RS485 using
 * the SP3485 transceiver.
 */
class SP3485ModbusClient : public IModbusClient {
private:
    HardwareSerial* serial;
    int dePin;          // DE/RE pin for direction control
    bool initialized;
    int lastError;
    uint32_t timeout;   // Communication timeout in milliseconds
    uint32_t successCount;
    uint32_t errorCount;

    /**
     * @brief Calculate Modbus CRC16
     * @param buffer Data buffer
     * @param length Buffer length
     * @return Calculated CRC16
     */
    uint16_t calculateCRC(uint8_t* buffer, int length);
    
    /**
     * @brief Set transceiver to transmit mode
     */
    void setTransmitMode();
    
    /**
     * @brief Set transceiver to receive mode
     */
    void setReceiveMode();

public:
    /**
     * @brief Constructor for SP3485ModbusClient
     * @param serialPort Pointer to HardwareSerial port
     * @param directionPin GPIO pin connected to DE/RE pins of SP3485
     */
    SP3485ModbusClient(HardwareSerial* serialPort, int directionPin);
    
    /**
     * @brief Destructor
     */
    virtual ~SP3485ModbusClient();
    
    // IModbusClient interface implementations
    bool initialize() override;
    bool readHoldingRegisters(uint8_t deviceAddress, uint16_t startRegister, 
                            uint16_t count, uint16_t* buffer) override;
    bool writeSingleRegister(uint8_t deviceAddress, uint16_t registerAddress, 
                           uint16_t value) override;
    int getLastError() override;
    void setTimeout(uint32_t timeoutMs) override;
    void getStatistics(uint32_t* successCount, uint32_t* errorCount) override;
};