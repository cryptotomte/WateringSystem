/**
 * @file ModbusSoilSensor.cpp
 * @brief Implementation of soil sensor using Modbus protocol
 * @author Paul Waserbrot
 * @date 2025-04-16
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
    , moistureCalibrationFactor(1.0f)
    , phCalibrationFactor(1.0f)
    , ecCalibrationFactor(1.0f)
{
    // Initialize default valid ranges
    validRanges["moisture"] = ValidRange(0.0f, 100.0f);
    validRanges["temperature"] = ValidRange(-40.0f, 80.0f);
    validRanges["humidity"] = ValidRange(0.0f, 100.0f);
    validRanges["ph"] = ValidRange(3.0f, 9.0f);
    validRanges["ec"] = ValidRange(0.0f, 5000.0f);
    validRanges["nitrogen"] = ValidRange(0.0f, 3000.0f);
    validRanges["phosphorus"] = ValidRange(0.0f, 3000.0f);
    validRanges["potassium"] = ValidRange(0.0f, 3000.0f);
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
    float rawMoisture = static_cast<float>(registerValues[0]) / 10.0f; // % (divided by 10)
    moisture = rawMoisture * moistureCalibrationFactor; // Apply calibration factor
    
    // Temperature is signed 16-bit with 0.1°C resolution
    int16_t rawTemp = static_cast<int16_t>(registerValues[1]);
    temperature = static_cast<float>(rawTemp) / 10.0f; // °C
    
    float rawPh = static_cast<float>(registerValues[2]) / 10.0f; // pH (divided by 10)
    ph = rawPh * phCalibrationFactor; // Apply calibration factor
    
    float rawEc = static_cast<float>(registerValues[3]); // µS/cm
    ec = rawEc * ecCalibrationFactor; // Apply calibration factor
    
    nitrogen = static_cast<float>(registerValues[4]); // mg/kg
    phosphorus = static_cast<float>(registerValues[5]); // mg/kg
    potassium = static_cast<float>(registerValues[6]); // mg/kg
    humidity = static_cast<float>(registerValues[7]) / 10.0f; // % (divided by 10)
    
    // Validate all readings using the validRanges map
    if (!isWithinValidRange("moisture", moisture) ||
        !isWithinValidRange("temperature", temperature) ||
        !isWithinValidRange("humidity", humidity) ||
        !isWithinValidRange("ph", ph)) {
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

float ModbusSoilSensor::convertRegisterToFloat(uint16_t registerValue, float scale)
{
    return static_cast<float>(registerValue) / scale;
}

uint16_t ModbusSoilSensor::convertFloatToRegister(float value, float scale)
{
    return static_cast<uint16_t>(value * scale);
}

bool ModbusSoilSensor::calibrateMoisture(float referenceValue)
{
    if (!initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    // Read current raw moisture value
    uint16_t rawRegisterValue;
    if (!modbusClient->readHoldingRegisters(deviceAddress, REG_MOISTURE, 1, &rawRegisterValue)) {
        lastError = 6; // Failed to read moisture register
        return false;
    }
    
    // Calculate the current raw moisture value
    float currentRawValue = convertRegisterToFloat(rawRegisterValue, 10.0f);
    
    // Calculate calibration factor (avoid division by zero)
    if (currentRawValue < 0.01f) {
        lastError = 7; // Raw value too low for calibration
        return false;
    }
    
    // Calculate and store calibration factor
    moistureCalibrationFactor = referenceValue / currentRawValue;
    
    // Store calibration factor in sensor (optional)
    uint16_t calibFactorRegValue = convertFloatToRegister(moistureCalibrationFactor, 100.0f);
    if (!modbusClient->writeHoldingRegister(deviceAddress, REG_MOISTURE_CALIB, calibFactorRegValue)) {
        // Non-fatal error - we'll still use the calibration factor locally
        lastError = 8; // Failed to write calibration factor to sensor
    } else {
        lastError = 0;
    }
    
    return true;
}

bool ModbusSoilSensor::calibratePH(float referenceValue)
{
    if (!initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    // Read current raw pH value
    uint16_t rawRegisterValue;
    if (!modbusClient->readHoldingRegisters(deviceAddress, REG_PH, 1, &rawRegisterValue)) {
        lastError = 9; // Failed to read pH register
        return false;
    }
    
    // Calculate the current raw pH value
    float currentRawValue = convertRegisterToFloat(rawRegisterValue, 10.0f);
    
    // Calculate calibration factor (avoid division by zero)
    if (currentRawValue < 0.01f) {
        lastError = 10; // Raw value too low for calibration
        return false;
    }
    
    // Calculate and store calibration factor
    phCalibrationFactor = referenceValue / currentRawValue;
    
    // Store calibration factor in sensor (optional)
    uint16_t calibFactorRegValue = convertFloatToRegister(phCalibrationFactor, 100.0f);
    if (!modbusClient->writeHoldingRegister(deviceAddress, REG_PH_CALIB, calibFactorRegValue)) {
        // Non-fatal error - we'll still use the calibration factor locally
        lastError = 11; // Failed to write calibration factor to sensor
    } else {
        lastError = 0;
    }
    
    return true;
}

bool ModbusSoilSensor::calibrateEC(float referenceValue)
{
    if (!initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    // Read current raw EC value
    uint16_t rawRegisterValue;
    if (!modbusClient->readHoldingRegisters(deviceAddress, REG_EC, 1, &rawRegisterValue)) {
        lastError = 12; // Failed to read EC register
        return false;
    }
    
    // Calculate the current raw EC value (no division by 10 for EC)
    float currentRawValue = static_cast<float>(rawRegisterValue);
    
    // Calculate calibration factor (avoid division by zero)
    if (currentRawValue < 0.01f) {
        lastError = 13; // Raw value too low for calibration
        return false;
    }
    
    // Calculate and store calibration factor
    ecCalibrationFactor = referenceValue / currentRawValue;
    
    // Store calibration factor in sensor (optional)
    uint16_t calibFactorRegValue = convertFloatToRegister(ecCalibrationFactor, 100.0f);
    if (!modbusClient->writeHoldingRegister(deviceAddress, REG_EC_CALIB, calibFactorRegValue)) {
        // Non-fatal error - we'll still use the calibration factor locally
        lastError = 14; // Failed to write calibration factor to sensor
    } else {
        lastError = 0;
    }
    
    return true;
}

bool ModbusSoilSensor::setValidRange(const char* parameter, float minValue, float maxValue)
{
    if (minValue >= maxValue) {
        lastError = 15; // Invalid range values
        return false;
    }
    
    // Store the range in our map
    validRanges[parameter] = ValidRange(minValue, maxValue);
    return true;
}

bool ModbusSoilSensor::isWithinValidRange(const char* parameter, float value)
{
    // Check if parameter exists in the map
    auto it = validRanges.find(parameter);
    if (it == validRanges.end() || !it->second.isSet) {
        return true; // If no range is set, consider the value valid
    }
    
    // Check if value is within the defined range
    return (value >= it->second.min && value <= it->second.max);
}