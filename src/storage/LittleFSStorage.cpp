// SPDX-FileCopyrightText: 2025 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LittleFSStorage.cpp
 * @brief Implementation of LittleFS data storage
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#include "storage/LittleFSStorage.h"
#include <Arduino.h>
#include <time.h>

LittleFSStorage::LittleFSStorage(const char* configFileName, const char* sensorDataFolder)
    : initialized(false)
    , lastError(0)
{
    // Keep leading slash for LittleFS paths, but avoid double slashes
    if (configFileName && configFileName[0] == '/') {
        configFile = String(configFileName);  // Keep the leading slash
    } else {
        configFile = String("/") + (configFileName ? configFileName : "config.json");
    }
    
    if (sensorDataFolder && sensorDataFolder[0] == '/') {
        dataFolder = String(sensorDataFolder);  // Keep the leading slash
    } else {
        dataFolder = String("/") + (sensorDataFolder ? sensorDataFolder : "data");
    }
    
    Serial.printf("LittleFSStorage initialized with config file: %s, data folder: %s\n", 
                  configFile.c_str(), dataFolder.c_str());
}

LittleFSStorage::~LittleFSStorage()
{
    // No specific cleanup needed
}

bool LittleFSStorage::initialize()
{
    if (initialized) {
        return true;
    }
    
    // Mount the LittleFS file system
    if (!LittleFS.begin(true)) {
        lastError = 1; // Failed to mount filesystem
        return false;
    }
    
    // Ensure the data directory exists
    if (!ensureDataDirectory()) {
        lastError = 2; // Failed to create data directory
        return false;
    }
    
    initialized = true;
    lastError = 0;
    return true;
}

bool LittleFSStorage::ensureDataDirectory()
{
    Serial.printf("Ensuring data directory exists: %s\n", dataFolder.c_str());
    
    // Check available space first
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;
    
    Serial.printf("LittleFS: %u bytes total, %u bytes used, %u bytes free (%0.1f%% used)\n", 
                  totalBytes, usedBytes, freeBytes, (usedBytes * 100.0) / totalBytes);
    
    if (!LittleFS.exists(dataFolder)) {
        Serial.println("Data directory does not exist, creating it now");
        
        // Check if we have enough space (at least 1KB for directory structure)
        if (freeBytes < 1024) {
            Serial.printf("Not enough free space to create data directory! Only %u bytes available\n", freeBytes);
            Serial.println("Consider using a larger partition table or cleaning up files");
            return false;
        }
        
        // Create the data directory if it doesn't exist
        if (!LittleFS.mkdir(dataFolder)) {
            Serial.println("Failed to create data directory!");
            // List possible reasons
            Serial.println("Possible causes:");
            Serial.println("1. File system is full");
            Serial.println("2. File system is corrupted");
            Serial.println("3. Path contains invalid characters");
            return false;
        }
        Serial.println("Data directory created successfully");
    } else {
        Serial.println("Data directory already exists");
    }
    return true;
}

String LittleFSStorage::getSensorDataFilename(const String& sensorId, const String& readingType)
{
    // Format: /data/sensorId_readingType.json (with leading slash)
    // Make sure we avoid double slashes
    if (dataFolder.endsWith("/")) {
        return dataFolder + sensorId + "_" + readingType + ".json";
    } else {
        return dataFolder + "/" + sensorId + "_" + readingType + ".json";
    }
}

bool LittleFSStorage::storeConfig(const String& key, const String& data)
{
    if (!initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    // Load existing configuration
    DynamicJsonDocument doc(4096); // Adjust size as needed
    
    // Check if config file exists and create it if it doesn't
    if (!LittleFS.exists(configFile)) {
        Serial.printf("Config file %s does not exist, creating it\n", configFile.c_str());
        File createFile = LittleFS.open(configFile, "w");
        if (!createFile) {
            Serial.printf("Failed to create config file %s\n", configFile.c_str());
            lastError = 3; // Failed to create file
            return false;
        }
        // Create an empty JSON object
        createFile.print("{}");
        createFile.close();
        Serial.println("Created empty config file");
    }
    
    // Now open the config file for reading
    File file = LittleFS.open(configFile, "r");
    if (!file) {
        lastError = 3; // Failed to open file
        Serial.printf("Failed to open config file %s for reading\n", configFile.c_str());
        return false;
    }
    
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        // Error parsing JSON, create a new document
        Serial.printf("Error parsing JSON in config file: %s, creating new document\n", error.c_str());
        doc.clear();
    }
    
    // Update or add the key-value pair
    doc[key] = data;
    
    // Write back to the file
    File writeFile = LittleFS.open(configFile, "w");
    if (!writeFile) {
        lastError = 4; // Failed to open file for writing
        Serial.printf("Failed to open config file %s for writing\n", configFile.c_str());
        return false;
    }
    
    serializeJson(doc, writeFile);
    writeFile.close();
    Serial.printf("Successfully stored config for key %s\n", key.c_str());
    
    return true;
}

String LittleFSStorage::getConfig(const String& key, const String& defaultValue)
{
    if (!initialized) {
        if (!initialize()) {
            Serial.println("Failed to initialize before reading config");
            return defaultValue;
        }
    }
    
    if (!LittleFS.exists(configFile)) {
        Serial.printf("Config file %s does not exist, returning default value for %s\n", 
                      configFile.c_str(), key.c_str());
        
        // Create empty config file for future use
        File createFile = LittleFS.open(configFile, "w");
        if (createFile) {
            createFile.print("{}");
            createFile.close();
            Serial.println("Created empty config file for future use");
        } else {
            Serial.printf("Failed to create empty config file %s\n", configFile.c_str());
        }
        
        return defaultValue;
    }
    
    File file = LittleFS.open(configFile, "r");
    if (!file) {
        lastError = 3; // Failed to open file
        Serial.printf("Failed to open config file %s for reading\n", configFile.c_str());
        return defaultValue;
    }
    
    DynamicJsonDocument doc(4096); // Adjust size as needed
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        lastError = 5; // JSON parsing error
        Serial.printf("Error parsing config file: %s\n", error.c_str());
        return defaultValue;
    }
    
    if (!doc.containsKey(key)) {
        Serial.printf("Key '%s' not found in config file, returning default value\n", key.c_str());
        return defaultValue;
    }
    
    String value = doc[key].as<String>();
    Serial.printf("Read config key '%s' with value '%s'\n", key.c_str(), value.c_str());
    return value;
}

bool LittleFSStorage::storeSensorReading(const String& sensorId, const String& readingType, 
                                        float value, time_t timestamp)
{
    if (!initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    String filename = getSensorDataFilename(sensorId, readingType);
    DynamicJsonDocument doc(16384); // Adjust size as needed
    
    // Load existing readings if file exists
    if (LittleFS.exists(filename)) {
        File file = LittleFS.open(filename, "r");
        if (!file) {
            lastError = 3; // Failed to open file
            return false;
        }
        
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        
        if (error) {
            // Error parsing JSON, create a new document
            doc.clear();
            doc.to<JsonArray>(); // Fixed: using to<JsonArray>() instead of createArray()
        }
    } else {
        // Create a new array
        doc.to<JsonArray>(); // Fixed: using to<JsonArray>() instead of createArray()
    }
    
    // Add new reading
    JsonObject reading = doc.createNestedObject();
    reading["timestamp"] = timestamp;
    reading["value"] = value;
    
    // Write back to the file
    File file = LittleFS.open(filename, "w");
    if (!file) {
        lastError = 4; // Failed to open file for writing
        return false;
    }
    
    serializeJson(doc, file);
    file.close();
    
    return true;
}

String LittleFSStorage::getSensorReadings(const String& sensorId, const String& readingType, 
                                         time_t startTime, time_t endTime)
{
    if (!initialized) {
        if (!initialize()) {
            return "[]";
        }
    }
    
    String filename = getSensorDataFilename(sensorId, readingType);
    if (!LittleFS.exists(filename)) {
        return "[]";
    }
    
    File file = LittleFS.open(filename, "r");
    if (!file) {
        lastError = 3; // Failed to open file
        return "[]";
    }
    
    DynamicJsonDocument allReadings(16384); // Adjust size as needed
    DeserializationError error = deserializeJson(allReadings, file);
    file.close();
    
    if (error) {
        lastError = 5; // JSON parsing error
        return "[]";
    }
    
    // Filter readings by time range
    DynamicJsonDocument filteredReadings(8192);
    JsonArray filteredArray = filteredReadings.to<JsonArray>(); // Fixed: using to<JsonArray>() instead of createArray()
    
    for (JsonObject reading : allReadings.as<JsonArray>()) {
        time_t readingTime = reading["timestamp"];
        if (readingTime >= startTime && readingTime <= endTime) {
            JsonObject newReading = filteredArray.createNestedObject();
            newReading["timestamp"] = readingTime;
            newReading["value"] = reading["value"];
        }
    }
    
    // Serialize the filtered readings to JSON string
    String result;
    serializeJson(filteredArray, result);
    return result;
}

float LittleFSStorage::getLastSensorReading(const String& sensorId, const String& readingType)
{
    if (!initialized) {
        if (!initialize()) {
            return NAN;
        }
    }
    
    String filename = getSensorDataFilename(sensorId, readingType);
    if (!LittleFS.exists(filename)) {
        return NAN;
    }
    
    File file = LittleFS.open(filename, "r");
    if (!file) {
        lastError = 3; // Failed to open file
        return NAN;
    }
    
    DynamicJsonDocument doc(16384); // Adjust size as needed
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        lastError = 5; // JSON parsing error
        return NAN;
    }
    
    JsonArray readings = doc.as<JsonArray>();
    if (readings.size() == 0) {
        return NAN;
    }
    
    // Find the reading with the most recent timestamp
    time_t latestTime = 0;
    float latestValue = NAN;
    
    for (JsonObject reading : readings) {
        time_t readingTime = reading["timestamp"];
        if (readingTime > latestTime) {
            latestTime = readingTime;
            latestValue = reading["value"];
        }
    }
    
    return latestValue;
}

int LittleFSStorage::pruneOldReadings(time_t olderThan)
{
    if (!initialized) {
        if (!initialize()) {
            return 0;
        }
    }
    
    int totalPruned = 0;
    File root = LittleFS.open(dataFolder);
    if (!root || !root.isDirectory()) {
        lastError = 6; // Could not open data directory
        return 0;
    }
    
    // Iterate through data files
    File file;
    while (file = root.openNextFile()) {
        if (!file.isDirectory()) {
            String filename = String(file.name());
            if (filename.endsWith(".json")) {
                // Load the file
                DynamicJsonDocument doc(16384); // Adjust size as needed
                DeserializationError error = deserializeJson(doc, file);
                file.close();
                
                if (!error) {
                    JsonArray readings = doc.as<JsonArray>();
                    size_t originalSize = readings.size();
                    
                    // Create a new array with only recent readings
                    DynamicJsonDocument newDoc(16384);
                    JsonArray newArray = newDoc.to<JsonArray>();
                    
                    for (JsonObject reading : readings) {
                        time_t readingTime = reading["timestamp"];
                        if (readingTime >= olderThan) {
                            JsonObject newReading = newArray.createNestedObject();
                            newReading["timestamp"] = readingTime;
                            newReading["value"] = reading["value"];
                        }
                    }
                    
                    // Write back to the file if we pruned anything
                    if (newArray.size() < originalSize) {
                        // Ensure correct path format with leading slash
                        String fullPath;
                        if (dataFolder.endsWith("/")) {
                            fullPath = dataFolder + filename;
                        } else {
                            fullPath = dataFolder + "/" + filename;
                        }
                        
                        File writeFile = LittleFS.open(fullPath, "w");
                        if (writeFile) {
                            serializeJson(newArray, writeFile);
                            writeFile.close();
                            totalPruned += (originalSize - newArray.size());
                        }
                    }
                }
            }
        }
    }
    
    return totalPruned;
}

bool LittleFSStorage::getStorageStats(uint32_t* totalSpace, uint32_t* usedSpace)
{
    if (!initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    if (totalSpace) {
        *totalSpace = LittleFS.totalBytes();
    }
    
    if (usedSpace) {
        *usedSpace = LittleFS.usedBytes();
    }
    
    return true;
}