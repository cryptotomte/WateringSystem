/**
 * @file ISensor.h
 * @brief Base interface for all sensor types in the WateringSystem
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#pragma once

/**
 * @brief Base interface for all sensors
 * 
 * This interface defines the common functionality for all sensor types
 * in the WateringSystem. It provides methods for initialization, reading
 * data, and checking status.
 */
class ISensor {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~ISensor() = default;
    
    /**
     * @brief Initialize the sensor
     * @return true if initialization successful, false otherwise
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Read sensor data
     * @return true if reading successful, false otherwise
     */
    virtual bool read() = 0;
    
    /**
     * @brief Check if sensor is available and working
     * @return true if sensor is available, false otherwise
     */
    virtual bool isAvailable() = 0;
    
    /**
     * @brief Get last error code
     * @return error code, 0 if no error
     */
    virtual int getLastError() = 0;
    
    /**
     * @brief Get the name of the sensor
     * @return String containing sensor name
     */
    virtual const char* getName() const = 0;
};