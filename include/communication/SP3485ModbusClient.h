/**
 * @file SP3485ModbusClient.h
 * @brief Header for RS485 based Modbus client with optical isolation
 * @author Paul Waserbrot
 * @date 2025-04-15
 * @updated 2025-06-04 - Added support for MikroElektronika RS485 5 Click with FOD817BSD isolation
 */

#ifndef WATERINGSYSTEM_COMMUNICATION_SP3485MODBUSCLIENT_H
#define WATERINGSYSTEM_COMMUNICATION_SP3485MODBUSCLIENT_H

#include "communication/IModbusClient.h"
#include "hardware/RS485Config.h"
#include <HardwareSerial.h>

/**
 * @brief Implementation of Modbus client interface for RS485 with optical isolation
 * 
 * This class provides a concrete implementation of the IModbusClient
 * interface for communicating with Modbus RTU devices over RS485 using
 * direct SP3485EN transceiver with FOD817BSD optocoupler isolation.
 *
 * Hardware Configuration (Hardware-Managed Power):
 * - RS485 Transceiver: SP3485EN (direct connection)
 * - Isolation: FOD817BSD optocouplers (5kV optical isolation)
 * - Power Management: Hardware LDO converters (always-on)
 * - Ground Strategy: Common ground with optical signal isolation
 */
class SP3485ModbusClient : public IModbusClient {
private:
    HardwareSerial* serial;
    int dePin;              // DE/RE pin for direction control (via optocoupler)
    bool initialized;
    int lastError;
    uint32_t timeout;       // Communication timeout in milliseconds
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
     * @brief Set transceiver to transmit mode (with optocoupler delay compensation)
     */
    void setTransmitMode();
    
    /**
     * @brief Set transceiver to receive mode (with optocoupler delay compensation)
     */
    void setReceiveMode();

public:
    /**
     * @brief Constructor for SP3485ModbusClient with optical isolation
     * @param serialPort Pointer to HardwareSerial port
     * @param directionPin GPIO pin connected to DE/RE optocoupler
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

#endif // WATERINGSYSTEM_COMMUNICATION_SP3485MODBUSCLIENT_H