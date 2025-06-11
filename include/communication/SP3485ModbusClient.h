/**
 * @file SP3485ModbusClient.h
 * @brief Header for RS485 based Modbus client with TXS0108E level shifter
 * @author Paul Waserbrot
 * @date 2025-04-15
 * @updated 2025-06-11 - Changed from FOD817BSD optocoupler to TXS0108E level shifter
 */

#ifndef WATERINGSYSTEM_COMMUNICATION_SP3485MODBUSCLIENT_H
#define WATERINGSYSTEM_COMMUNICATION_SP3485MODBUSCLIENT_H

#include "communication/IModbusClient.h"
#include "hardware/RS485Config.h"
#include <HardwareSerial.h>

/**
 * @brief Implementation of Modbus client interface for RS485 with TXS0108E level shifter
 * 
 * This class provides a concrete implementation of the IModbusClient
 * interface for communicating with Modbus RTU devices over RS485 using
 * TXS0108E bidirectional level shifter for voltage translation.
 *
 * Hardware Configuration (Hardware-Managed Power):
 * - RS485 Transceiver: SP3485EN (direct connection)
 * - Level Shifter: TXS0108E (3.3V ↔ 5V bidirectional)
 * - Power Management: Hardware LDO converters (always-on)
 * - Ground Strategy: Common ground with level shifting
 */
class SP3485ModbusClient : public IModbusClient {
private:
    HardwareSerial* serial;
    int dePin;              // DE/RE pin for direction control (via TXS0108E level shifter)
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
     * @brief Set transceiver to transmit mode (with TXS0108E level shifter)
     */
    void setTransmitMode();
    
    /**
     * @brief Set transceiver to receive mode (with TXS0108E level shifter)
     */
    void setReceiveMode();

public:
    /**
     * @brief Constructor for SP3485ModbusClient with TXS0108E level shifter
     * @param serialPort Pointer to HardwareSerial port
     * @param directionPin GPIO pin connected to DE/RE via TXS0108E
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