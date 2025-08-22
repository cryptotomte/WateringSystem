// SPDX-FileCopyrightText: 2025 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file BME280Sensor.cpp
 * @brief Implementation of the BME280 environmental sensor
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#include "sensors/BME280Sensor.h"

BME280Sensor::BME280Sensor(uint8_t address, const char* sensorName)
    : initialized(false)
    , lastError(0)
    , temperature(0.0f)
    , humidity(0.0f)
    , pressure(0.0f)
    , name(sensorName)
    , i2cAddress(address)
{
}

BME280Sensor::~BME280Sensor()
{
    // No specific cleanup needed
}

bool BME280Sensor::initialize()
{
    if (initialized) {
        return true;
    }
    
    // Initialize I2C communication with BME280
    if (!bme.begin(i2cAddress)) {
        lastError = 1; // Sensor not found
        return false;
    }
    
    // Set default settings for the sensor
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,     // Operating Mode
                    Adafruit_BME280::SAMPLING_X2,     // Temperature oversampling
                    Adafruit_BME280::SAMPLING_X16,    // Pressure oversampling
                    Adafruit_BME280::SAMPLING_X1,     // Humidity oversampling
                    Adafruit_BME280::FILTER_X16,      // Filtering
                    Adafruit_BME280::STANDBY_MS_500); // Standby time
    
    initialized = true;
    lastError = 0;
    return true;
}

bool BME280Sensor::read()
{
    if (!initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    temperature = bme.readTemperature();
    humidity = bme.readHumidity();
    pressure = bme.readPressure() / 100.0F; // Convert Pa to hPa
    
    // Check if any reading failed
    if (isnan(temperature) || isnan(humidity) || isnan(pressure)) {
        lastError = 2; // Reading failed
        return false;
    }
    
    lastError = 0;
    return true;
}

bool BME280Sensor::isAvailable()
{
    if (!initialized) {
        return initialize();
    }
    return true;
}

int BME280Sensor::getLastError()
{
    return lastError;
}

const char* BME280Sensor::getName() const
{
    return name;
}

float BME280Sensor::getTemperature()
{
    return temperature;
}

float BME280Sensor::getHumidity()
{
    return humidity;
}

float BME280Sensor::getPressure()
{
    return pressure;
}