/**
 * @file ModbusSoilSensor.cpp
 * @brief Implementation of ModbusSoilSensor class
 * @author WateringSystem Team
 * @date 2025-04-15
 */

#include "sensors/ModbusSoilSensor.h"

ModbusSoilSensor::ModbusSoilSensor(IModbusClient* client, uint8_t address, const char* sensorName)
    : modbusClient(client)
    , initialized(false)
    , lastError(0)
    , name(sensorName)
    , deviceAddress(address)
    , moisture(0.0f)
    , temperature(0.0f)
    , humidity(0.0f)
    , ph(0.0f)
    , ec(0.0f)
    , nitrogen(0.0f)
    , phosphorus(0.0f)
    , potassium(0.0f)
{
}

ModbusSoilSensor::~ModbusSoilSensor()
{
    // We don't own the modbusClient, so don't delete it
}

bool ModbusSoilSensor::initialize()
{
    if (initialized) {
        return true;
    }
    
    if (!modbusClient) {
        lastError = 1; // No Modbus client
        return false;
    }
    
    if (!modbusClient->initialize()) {
        lastError = 2; // Modbus client initialization failed
        return false;
    }
    
    // Try to read a single register to verify that the sensor is working
    uint16_t testRegister;
    if (!modbusClient->readHoldingRegisters(deviceAddress, REG_MOISTURE, 1, &testRegister)) {
        lastError = 3; // Unable to communicate with sensor
        return false;
    }
    
    initialized = true;
    lastError = 0;
    return true;
}

bool ModbusSoilSensor::read()
{
    if (!initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    // Read all sensor registers in one request
    const uint16_t registerCount = 8;
    uint16_t registerValues[registerCount];
    
    if (!modbusClient->readHoldingRegisters(deviceAddress, REG_MOISTURE, registerCount, registerValues)) {
        lastError = 4; // Failed to read registers
        return false;
    }
    
    // Parse and convert register values to appropriate units
    moisture = static_cast<float>(registerValues[0]) / 10.0f; // % (divided by 10)
    
    // Temperature is signed 16-bit with 0.1°C resolution
    int16_t rawTemp = static_cast<int16_t>(registerValues[1]);
    temperature = static_cast<float>(rawTemp) / 10.0f; // °C
    
    ph = static_cast<float>(registerValues[2]) / 10.0f; // pH (divided by 10)
    ec = static_cast<float>(registerValues[3]); // µS/cm
    nitrogen = static_cast<float>(registerValues[4]); // mg/kg
    phosphorus = static_cast<float>(registerValues[5]); // mg/kg
    potassium = static_cast<float>(registerValues[6]); // mg/kg
    humidity = static_cast<float>(registerValues[7]) / 10.0f; // % (divided by 10)
    
    // Validate readings are within reasonable ranges
    if (moisture < 0.0f || moisture > 100.0f ||
        temperature < -40.0f || temperature > 80.0f ||
        ph < 3.0f || ph > 9.0f ||
        humidity < 0.0f || humidity > 100.0f) {
        lastError = 5; // Invalid reading values
        return false;
    }
    
    lastError = 0;
    return true;
}

bool ModbusSoilSensor::isAvailable()
{
    if (!initialized) {
        return initialize();
    }
    
    // Check if Modbus client is functional
    if (!modbusClient || !modbusClient->readHoldingRegisters(deviceAddress, REG_MOISTURE, 1, nullptr)) {
        return false;
    }
    
    return true;
}

int ModbusSoilSensor::getLastError()
{
    return lastError;
}

const char* ModbusSoilSensor::getName() const
{
    return name;
}

float ModbusSoilSensor::getMoisture()
{
    return moisture;
}

float ModbusSoilSensor::getTemperature()
{
    return temperature;
}

float ModbusSoilSensor::getHumidity()
{
    return humidity;
}

float ModbusSoilSensor::getPH()
{
    return ph;
}

float ModbusSoilSensor::getEC()
{
    return ec;
}

float ModbusSoilSensor::getNitrogen()
{
    return nitrogen;
}

float ModbusSoilSensor::getPhosphorus()
{
    return phosphorus;
}

float ModbusSoilSensor::getPotassium()
{
    return potassium;
}