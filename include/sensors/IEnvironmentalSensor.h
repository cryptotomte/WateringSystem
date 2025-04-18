/**
 * @file IEnvironmentalSensor.h
 * @brief Interface for environmental sensors
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#ifndef I_ENVIRONMENTAL_SENSOR_H
#define I_ENVIRONMENTAL_SENSOR_H

#include "ISensor.h"

/**
 * @brief Interface for environmental sensors
 * 
 * This interface extends the base sensor interface with methods specific
 * to environmental sensors, such as those measuring ambient temperature
 * and humidity.
 */
class IEnvironmentalSensor : public ISensor {
public:
    /**
     * @brief Get temperature in Celsius
     * @return temperature value
     */
    virtual float getTemperature() = 0;
    
    /**
     * @brief Get relative humidity in percent
     * @return humidity value
     */
    virtual float getHumidity() = 0;
    
    /**
     * @brief Get atmospheric pressure in hPa
     * @return pressure value, -1 if not supported
     */
    virtual float getPressure() = 0;
};

#endif // I_ENVIRONMENTAL_SENSOR_H