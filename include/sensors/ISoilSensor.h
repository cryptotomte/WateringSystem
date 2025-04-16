/**
 * @file ISoilSensor.h
 * @brief Interface for soil condition sensors in the WateringSystem
 * @author WateringSystem Team
 * @date 2025-04-15
 */

#ifndef WATERINGSYSTEM_SENSORS_ISOILSENSOR_H
#define WATERINGSYSTEM_SENSORS_ISOILSENSOR_H

#include "ISensor.h"

/**
 * @brief Interface for soil sensors
 * 
 * This interface extends the base sensor interface with methods specific
 * to soil condition sensors, such as those measuring soil moisture,
 * temperature, pH, electrical conductivity and NPK values.
 */
class ISoilSensor : public ISensor {
public:
    /**
     * @brief Get soil moisture level
     * @return Moisture level in percentage (0-100%)
     */
    virtual float getMoisture() = 0;
    
    /**
     * @brief Get soil temperature
     * @return Temperature in degrees Celsius
     */
    virtual float getTemperature() = 0;
    
    /**
     * @brief Get air humidity near the soil
     * @return Relative humidity percentage (0-100%)
     */
    virtual float getHumidity() = 0;
    
    /**
     * @brief Get soil pH value
     * @return pH value (typically 0-14)
     */
    virtual float getPH() = 0;
    
    /**
     * @brief Get soil electrical conductivity
     * @return EC value in µS/cm
     */
    virtual float getEC() = 0;
    
    /**
     * @brief Get soil nitrogen content
     * @return Nitrogen level in mg/kg
     */
    virtual float getNitrogen() = 0;
    
    /**
     * @brief Get soil phosphorus content
     * @return Phosphorus level in mg/kg
     */
    virtual float getPhosphorus() = 0;
    
    /**
     * @brief Get soil potassium content
     * @return Potassium level in mg/kg
     */
    virtual float getPotassium() = 0;
    
    /**
     * @brief Calibrate moisture sensor reading
     * @param referenceValue Known reference moisture percentage
     * @return true if calibration successful, false otherwise
     */
    virtual bool calibrateMoisture(float referenceValue) = 0;
    
    /**
     * @brief Calibrate pH sensor reading
     * @param referenceValue Known reference pH value
     * @return true if calibration successful, false otherwise
     */
    virtual bool calibratePH(float referenceValue) = 0;
    
    /**
     * @brief Calibrate EC sensor reading
     * @param referenceValue Known reference EC value in µS/cm
     * @return true if calibration successful, false otherwise
     */
    virtual bool calibrateEC(float referenceValue) = 0;
    
    /**
     * @brief Set valid ranges for sensor readings
     * @param parameter Sensor parameter to set range for (e.g. "moisture", "ph", "ec")
     * @param minValue Minimum valid value
     * @param maxValue Maximum valid value
     * @return true if range set successfully, false otherwise
     */
    virtual bool setValidRange(const char* parameter, float minValue, float maxValue) = 0;
    
    /**
     * @brief Check if a sensor reading is within valid range
     * @param parameter Sensor parameter to check (e.g. "moisture", "ph", "ec")
     * @param value Value to check
     * @return true if value is within valid range, false otherwise
     */
    virtual bool isWithinValidRange(const char* parameter, float value) = 0;
};

#endif // WATERINGSYSTEM_SENSORS_ISOILSENSOR_H