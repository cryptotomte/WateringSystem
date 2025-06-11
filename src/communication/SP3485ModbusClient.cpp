/**
 * @file SP3485ModbusClient.cpp
 * @brief RS485 interface implementation for Modbus communication with TXS0108E level shifter
 * @author Paul Waserbrot
 * @date 2025-04-15
 * @updated 2025-06-11 - Changed from FOD817BSD optocoupler to TXS0108E level shifter for faster switching
 */

#include "communication/SP3485ModbusClient.h"
#include "hardware/RS485Config.h"
#include <Arduino.h>

SP3485ModbusClient::SP3485ModbusClient(HardwareSerial *serialPort, int directionPin)
    : serial(serialPort), dePin(directionPin), 
      initialized(false), lastError(0), timeout(RS485_DEFAULT_TIMEOUT_MS),
      successCount(0), errorCount(0)
{
}

SP3485ModbusClient::~SP3485ModbusClient()
{
    // We don't own the serial port, so don't delete it
}

bool SP3485ModbusClient::initialize()
{    if (initialized)
    {
        return true;
    }

    if (!serial)
    {
        lastError = 1; // No serial port
        return false;
    }    // Setup DE/RE pin for direction control (via TXS0108E level shifter)
    pinMode(dePin, OUTPUT);
    setReceiveMode();

    // Hardware-managed power - no software control needed
    // LDO converters provide always-on power to field domain
    // TXS0108E OE pin connected to VCC for always-on operation
    
    // Wait for hardware to stabilize (much faster with TXS0108E)
    delay(RS485_POWER_ON_DELAY_MS);

    // Flush any existing data
    if (serial->available())
    {
        while (serial->available())
        {
            serial->read();
        }
    }

    initialized = true;
    lastError = 0;
    return true;
}

void SP3485ModbusClient::setTransmitMode()
{
    digitalWrite(dePin, HIGH);
    delayMicroseconds(RS485_DE_ASSERT_DELAY_US); // Fast switching with TXS0108E
}

void SP3485ModbusClient::setReceiveMode()
{
    digitalWrite(dePin, LOW);
    delayMicroseconds(RS485_DE_DEASSERT_DELAY_US); // Fast switching with TXS0108E
}

uint16_t SP3485ModbusClient::calculateCRC(uint8_t *buffer, int length)
{
    uint16_t crc = 0xFFFF;

    for (int pos = 0; pos < length; pos++)
    {
        crc ^= static_cast<uint16_t>(buffer[pos]);

        for (int i = 8; i != 0; i--)
        {
            if ((crc & 0x0001) != 0)
            {
                crc >>= 1;
                crc ^= 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

bool SP3485ModbusClient::readHoldingRegisters(uint8_t deviceAddress, uint16_t startRegister,
                                              uint16_t count, uint16_t *buffer)
{
    if (!initialized)
    {
        if (!initialize())
        {
            errorCount++;
            return false;
        }
    }

    // Validate parameters
    if (count > 125)
    {                  // Modbus spec limit
        lastError = 2; // Invalid register count
        errorCount++;
        return false;
    }

    // Create Modbus RTU request
    uint8_t request[8];
    request[0] = deviceAddress;
    request[1] = 0x03;                 // Function code: Read Holding Registers
    request[2] = startRegister >> 8;   // High byte of start register
    request[3] = startRegister & 0xFF; // Low byte of start register
    request[4] = count >> 8;           // High byte of register count
    request[5] = count & 0xFF;         // Low byte of register count

    // Calculate CRC
    uint16_t crc = calculateCRC(request, 6);
    request[6] = crc & 0xFF; // Low byte of CRC
    request[7] = crc >> 8;   // High byte of CRC    // Clear receive buffer
    while (serial->available())
    {
        serial->read();
    }    // Switch to transmit mode and send request
    setTransmitMode();
    delayMicroseconds(50); // Shorter delay for TXS0108E mode switching
    serial->write(request, 8);
    serial->flush(); // Wait for transmission to complete

    // Critical timing: delay before switching to receive mode (much shorter with TXS0108E)
    delayMicroseconds(100); // 100µs delay vs 1ms with optocoupler
    setReceiveMode();// Calculate expected response length
    // Response: [address(1)] + [function(1)] + [byte count(1)] + [data(2*count)] + [CRC(2)]
    uint8_t expectedLength = 5 + (count * 2);
    uint8_t response[256]; // Buffer must be large enough for maximum possible response

    // Wait for response with timeout
    unsigned long startTime = millis();
    int bytesRead = 0;
    uint8_t rawResponse[256]; // Raw buffer to handle timing issues

    while ((bytesRead < (expectedLength + 2)) && ((millis() - startTime) < timeout))
    {
        if (serial->available())
        {
            rawResponse[bytesRead] = serial->read();
            bytesRead++;
        }
        yield(); // Give time for other tasks
    }    // SMART PARSING - Handle byte offset due to timing issues (proven working in test)
    // Look for the correct start pattern: device address + function code (0x03)
    int correctOffset = -1;
    
    for (int i = 0; i < min(3, bytesRead - 1); i++)
    {
        if (rawResponse[i] == deviceAddress && rawResponse[i + 1] == 0x03)
        {
            correctOffset = i;
            break;
        }
    }
    
    if (correctOffset < 0)
    {
        lastError = 4; // Wrong device address / corrupted response
        errorCount++;
        return false;
    }
    
    // Adjust byte count and copy corrected response
    bytesRead -= correctOffset;
    for (int i = 0; i < bytesRead; i++)
    {
        response[i] = rawResponse[i + correctOffset];
    }    // Check if we got complete response (allow for timing offset corrections)
    if (bytesRead < expectedLength)
    {
        lastError = 3; // Incomplete response
        errorCount++;
        return false;
    }
    
    // If we got more bytes than expected, it might be due to timing issues
    // but as long as we have the minimum required bytes after offset correction, continue
    if (bytesRead > expectedLength)
    {
        // Continue processing - we have enough data
    }

    // Verify device address in response
    if (response[0] != deviceAddress)
    {
        lastError = 4; // Wrong device address
        errorCount++;
        return false;
    }

    // Verify function code (should match or be error code)
    if (response[1] != 0x03)
    {
        if (response[1] == 0x83)
        {
            // Modbus exception response
            lastError = 100 + response[2]; // 100 + exception code
            errorCount++;
            return false;
        }
        else
        {
            lastError = 5; // Wrong function code
            errorCount++;
            return false;
        }
    }

    // Verify byte count
    if (response[2] != (count * 2))
    {
        lastError = 6; // Wrong byte count
        errorCount++;
        return false;
    }

    // Verify CRC
    uint16_t responseCRC = static_cast<uint16_t>(response[bytesRead - 1]) << 8 |
                           static_cast<uint16_t>(response[bytesRead - 2]);
    uint16_t calculatedCRC = calculateCRC(response, bytesRead - 2);

    if (responseCRC != calculatedCRC)
    {
        lastError = 7; // CRC error
        errorCount++;
        return false;
    }

    // Parse register values
    if (buffer != nullptr)
    {
        for (uint16_t i = 0; i < count; i++)
        {
            buffer[i] = static_cast<uint16_t>(response[3 + (i * 2)]) << 8 |
                        static_cast<uint16_t>(response[4 + (i * 2)]);
        }
    }

    lastError = 0;
    successCount++;
    return true;
}

bool SP3485ModbusClient::writeSingleRegister(uint8_t deviceAddress, uint16_t registerAddress,
                                             uint16_t value)
{
    if (!initialized)
    {
        if (!initialize())
        {
            errorCount++;
            return false;
        }
    }

    // Create Modbus RTU request
    uint8_t request[8];
    request[0] = deviceAddress;
    request[1] = 0x06;                   // Function code: Write Single Register
    request[2] = registerAddress >> 8;   // High byte of register address
    request[3] = registerAddress & 0xFF; // Low byte of register address
    request[4] = value >> 8;             // High byte of value
    request[5] = value & 0xFF;           // Low byte of value

    // Calculate CRC
    uint16_t crc = calculateCRC(request, 6);
    request[6] = crc & 0xFF; // Low byte of CRC
    request[7] = crc >> 8;   // High byte of CRC

    // Clear receive buffer
    while (serial->available())
    {
        serial->read();
    }

    // Switch to transmit mode and send request
    setTransmitMode();
    serial->write(request, 8);
    serial->flush(); // Wait for transmission to complete

    // Switch to receive mode
    setReceiveMode();

    // Wait for response with timeout
    unsigned long startTime = millis();
    int bytesRead = 0;
    uint8_t response[8]; // Response should be 8 bytes

    while ((bytesRead < 8) && ((millis() - startTime) < timeout))
    {
        if (serial->available())
        {
            response[bytesRead] = serial->read();
            bytesRead++;
        }
        yield(); // Give time for other tasks
    }

    // Check if we got complete response
    if (bytesRead != 8)
    {
        lastError = 3; // Incomplete response
        errorCount++;
        return false;
    }

    // Verify response matches request (echo)
    for (int i = 0; i < 6; i++)
    {
        if (response[i] != request[i])
        {
            lastError = 8; // Echo mismatch
            errorCount++;
            return false;
        }
    }

    // Verify CRC
    uint16_t responseCRC = static_cast<uint16_t>(response[7]) << 8 |
                           static_cast<uint16_t>(response[6]);
    uint16_t calculatedCRC = calculateCRC(response, 6);

    if (responseCRC != calculatedCRC)
    {
        lastError = 7; // CRC error
        errorCount++;
        return false;
    }

    lastError = 0;
    successCount++;
    return true;
}

int SP3485ModbusClient::getLastError()
{
    return lastError;
}

void SP3485ModbusClient::setTimeout(uint32_t timeoutMs)
{
    timeout = timeoutMs;
}

void SP3485ModbusClient::getStatistics(uint32_t *outSuccessCount, uint32_t *outErrorCount)
{
    if (outSuccessCount)
    {
        *outSuccessCount = successCount;
    }

    if (outErrorCount)
    {
        *outErrorCount = errorCount;
    }
}