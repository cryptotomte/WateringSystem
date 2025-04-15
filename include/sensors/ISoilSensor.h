/**
 * @file ISoilSensor.h
 * @brief Interface for soil condition sensors in the WateringSystem
 * @author WateringSystem Team
 * @date 2025-04-15
 */

#pragma once

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
     * @brief Get soil moisture in percent
     * @return moisture value
     */
    virtual float getMoisture() = 0;
    
    /**
     * @brief Get soil temperature in Celsius
     * @return temperature value
     */
    virtual float getTemperature() = 0;
    
    /**
     * @brief Get soil humidity in percent
     * @return humidity value, -1 if not supported by the sensor
     */
    virtual float getHumidity() = 0;
    
    /**
     * @brief Get soil pH level
     * @return pH value, -1 if not supported by the sensor
     */
    virtual float getPH() = 0;
    
    /**
     * @brief Get electrical conductivity in µS/cm
     * @return EC value, -1 if not supported by the sensor
     */
    virtual float getEC() = 0;
    
    /**
     * @brief Get nitrogen level in mg/kg
     * @return nitrogen value, -1 if not supported by the sensor
     */
    virtual float getNitrogen() = 0;
    
    /**
     * @brief Get phosphorus level in mg/kg
     * @return phosphorus value, -1 if not supported by the sensor
     */
    virtual float getPhosphorus() = 0;
    
    /**
     * @brief Get potassium level in mg/kg
     * @return potassium value, -1 if not supported by the sensor
     */
    virtual float getPotassium() = 0;
};