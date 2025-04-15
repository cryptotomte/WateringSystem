/**
 * @file LittleFSStorage.cpp
 * @brief Implementation of LittleFSStorage class
 * @author WateringSystem Team
 * @date 2025-04-15
 */

#include "storage/LittleFSStorage.h"
#include <Arduino.h>
#include <time.h>

LittleFSStorage::LittleFSStorage(const char* configFileName, const char* sensorDataFolder)
    : initialized(false)
    , lastError(0)
    , configFile(configFileName)
    , dataFolder(sensorDataFolder)
{
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
    if (!LittleFS.exists(dataFolder)) {
        // Create the data directory if it doesn't exist
        if (!LittleFS.mkdir(dataFolder)) {
            return false;
        }
    }
    return true;
}

String LittleFSStorage::getSensorDataFilename(const String& sensorId, const String& readingType)
{
    // Format: /data/sensorId_readingType.json
    return String(dataFolder) + "/" + sensorId + "_" + readingType + ".json";
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
    
    if (LittleFS.exists(configFile)) {
        File file = LittleFS.open(configFile, "r");
        if (!file) {
            lastError = 3; // Failed to open file
            return false;
        }
        
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        
        if (error) {
            // Error parsing JSON, create a new document
            doc.clear();
        }
    }
    
    // Update or add the key-value pair
    doc[key] = data;
    
    // Write back to the file
    File file = LittleFS.open(configFile, "w");
    if (!file) {
        lastError = 4; // Failed to open file for writing
        return false;
    }
    
    serializeJson(doc, file);
    file.close();
    
    return true;
}

String LittleFSStorage::getConfig(const String& key, const String& defaultValue)
{
    if (!initialized) {
        if (!initialize()) {
            return defaultValue;
        }
    }
    
    if (!LittleFS.exists(configFile)) {
        return defaultValue;
    }
    
    File file = LittleFS.open(configFile, "r");
    if (!file) {
        lastError = 3; // Failed to open file
        return defaultValue;
    }
    
    DynamicJsonDocument doc(4096); // Adjust size as needed
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        lastError = 5; // JSON parsing error
        return defaultValue;
    }
    
    if (!doc.containsKey(key)) {
        return defaultValue;
    }
    
    return doc[key].as<String>();
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
            doc.createArray();
        }
    } else {
        // Create a new array
        doc.createArray();
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
    JsonArray filteredArray = filteredReadings.createArray();
    
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
                    JsonArray newArray = newDoc.createArray();
                    
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
                        File writeFile = LittleFS.open(String(dataFolder) + "/" + filename, "w");
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