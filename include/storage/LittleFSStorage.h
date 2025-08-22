// SPDX-FileCopyrightText: 2025 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LittleFSStorage.h
 * @brief LittleFS based data storage implementation
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#ifndef LITTLE_FS_STORAGE_H
#define LITTLE_FS_STORAGE_H

#include "storage/IDataStorage.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

/**
 * @brief Implementation of data storage interface using LittleFS
 * 
 * This class provides a concrete implementation of the IDataStorage
 * interface for storing and retrieving configuration data and sensor
 * readings using the LittleFS file system.
 */
class LittleFSStorage : public IDataStorage {
private:
    bool initialized;
    int lastError;
    String configFile;    // Changed from const char* to String
    String dataFolder;    // Changed from const char* to String
    
    /**
     * @brief Ensure data directory exists
     * @return true if directory exists or was created, false otherwise
     */
    bool ensureDataDirectory();
    
    /**
     * @brief Generate filename for sensor data based on sensor and reading type
     * @param sensorId ID of the sensor
     * @param readingType Type of reading
     * @return String containing the filename
     */
    String getSensorDataFilename(const String& sensorId, const String& readingType);

public:
    /**
     * @brief Constructor for LittleFSStorage
     * @param configFileName Name of the configuration file
     * @param sensorDataFolder Name of the folder for storing sensor data
     */
    LittleFSStorage(const char* configFileName = "/config.json", 
                  const char* sensorDataFolder = "/data");
    
    /**
     * @brief Destructor
     */
    virtual ~LittleFSStorage();
    
    // IDataStorage interface implementations
    bool initialize() override;
    bool storeConfig(const String& key, const String& data) override;
    String getConfig(const String& key, const String& defaultValue) override;
    bool storeSensorReading(const String& sensorId, const String& readingType, 
                          float value, time_t timestamp) override;
    String getSensorReadings(const String& sensorId, const String& readingType, 
                           time_t startTime, time_t endTime) override;
    float getLastSensorReading(const String& sensorId, const String& readingType) override;
    int pruneOldReadings(time_t olderThan) override;
    bool getStorageStats(uint32_t* totalSpace, uint32_t* usedSpace) override;
};

#endif // LITTLE_FS_STORAGE_H