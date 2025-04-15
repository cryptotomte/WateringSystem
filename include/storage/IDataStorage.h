/**
 * @file IDataStorage.h
 * @brief Interface for data storage in the WateringSystem
 * @author WateringSystem Team
 * @date 2025-04-15
 */

#pragma once

#include <stdint.h>
#include <time.h>
#include <Arduino.h>

/**
 * @brief Interface for data storage
 * 
 * This interface defines the functionality for storing and retrieving
 * configuration data and sensor readings.
 */
class IDataStorage {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~IDataStorage() = default;
    
    /**
     * @brief Initialize the storage
     * @return true if initialization successful, false otherwise
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Store configuration data
     * @param key Configuration key
     * @param data Data to store
     * @return true if operation successful, false otherwise
     */
    virtual bool storeConfig(const String& key, const String& data) = 0;
    
    /**
     * @brief Retrieve configuration data
     * @param key Configuration key
     * @param defaultValue Default value if key not found
     * @return Retrieved data or default value
     */
    virtual String getConfig(const String& key, const String& defaultValue = "") = 0;
    
    /**
     * @brief Store sensor reading
     * @param sensorId ID of the sensor
     * @param readingType Type of reading (temperature, humidity, etc.)
     * @param value Reading value
     * @param timestamp Time of reading
     * @return true if operation successful, false otherwise
     */
    virtual bool storeSensorReading(const String& sensorId, const String& readingType, 
                                  float value, time_t timestamp) = 0;
    
    /**
     * @brief Get sensor readings for a specific period
     * @param sensorId ID of the sensor
     * @param readingType Type of reading
     * @param startTime Start time of the period
     * @param endTime End time of the period
     * @return JSON string with readings
     */
    virtual String getSensorReadings(const String& sensorId, const String& readingType, 
                                   time_t startTime, time_t endTime) = 0;
    
    /**
     * @brief Get most recent sensor reading
     * @param sensorId ID of the sensor
     * @param readingType Type of reading
     * @return Last reading value, NAN if not available
     */
    virtual float getLastSensorReading(const String& sensorId, const String& readingType) = 0;
    
    /**
     * @brief Delete old readings to free up space
     * @param olderThan Delete readings older than this timestamp
     * @return Number of deleted readings
     */
    virtual int pruneOldReadings(time_t olderThan) = 0;
    
    /**
     * @brief Get storage statistics
     * @param totalSpace Pointer to store total space in bytes
     * @param usedSpace Pointer to store used space in bytes
     * @return true if operation successful, false otherwise
     */
    virtual bool getStorageStats(uint32_t* totalSpace, uint32_t* usedSpace) = 0;
};