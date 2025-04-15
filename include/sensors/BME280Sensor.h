/**
 * @file BME280Sensor.h
 * @brief BME280 sensor implementation
 * @author WateringSystem Team
 * @date 2025-04-15
 */

#pragma once

#include "sensors/IEnvironmentalSensor.h"
#include <Adafruit_BME280.h>
#include <Wire.h>

/**
 * @brief Implementation of the environmental sensor interface for BME280
 * 
 * This class provides a concrete implementation of the IEnvironmentalSensor
 * interface for the BME280 sensor, which measures temperature, humidity,
 * and pressure.
 */
class BME280Sensor : public IEnvironmentalSensor {
private:
    Adafruit_BME280 bme;
    bool initialized;
    int lastError;
    float temperature;
    float humidity;
    float pressure;
    const char* name;
    uint8_t i2cAddress;

public:
    /**
     * @brief Constructor for BME280Sensor
     * @param address I2C address of the BME280 sensor (default: 0x76)
     * @param sensorName Name to identify this sensor
     */
    BME280Sensor(uint8_t address = BME280_ADDRESS, const char* sensorName = "BME280");
    
    /**
     * @brief Destructor
     */
    virtual ~BME280Sensor();
    
    // ISensor interface implementations
    bool initialize() override;
    bool read() override;
    bool isAvailable() override;
    int getLastError() override;
    const char* getName() const override;
    
    // IEnvironmentalSensor interface implementations
    float getTemperature() override;
    float getHumidity() override;
    float getPressure() override;
};