/**
 * @file WateringController.cpp
 * @brief Implementation of the WateringController class
 * @author WateringSystem Team
 * @date 2025-04-15
 */

#include "WateringController.h"
#include <ArduinoJson.h>

// Default configuration values
#define DEFAULT_SENSOR_READ_INTERVAL 60000UL     // 1 minute
#define DEFAULT_DATA_LOG_INTERVAL    300000UL    // 5 minutes
#define DEFAULT_MIN_WATERING_INTERVAL 21600UL    // 6 hours
#define DEFAULT_MOISTURE_THRESHOLD_LOW 30.0f     // 30%
#define DEFAULT_MOISTURE_THRESHOLD_HIGH 55.0f    // 55%
#define DEFAULT_WATERING_DURATION 20             // 20 seconds

WateringController::WateringController(IEnvironmentalSensor* environmental, ISoilSensor* soil,
                                     IWaterPump* pump, IDataStorage* storage)
    : envSensor(environmental)
    , soilSensor(soil)
    , waterPump(pump)
    , dataStorage(storage)
    , initialized(false)
    , lastError(0)
    , lastSensorReadTime(0)
    , lastDataLogTime(0)
    , lastWateringTime(0)
    , wateringEnabled(true)
    , sensorReadInterval(DEFAULT_SENSOR_READ_INTERVAL)
    , dataLogInterval(DEFAULT_DATA_LOG_INTERVAL)
    , minWateringInterval(DEFAULT_MIN_WATERING_INTERVAL)
    , moistureThresholdLow(DEFAULT_MOISTURE_THRESHOLD_LOW)
    , moistureThresholdHigh(DEFAULT_MOISTURE_THRESHOLD_HIGH)
    , wateringDuration(DEFAULT_WATERING_DURATION)
{
}

WateringController::~WateringController()
{
    // We don't own the components, so don't delete them
    // But make sure the pump is off before destroying
    if (waterPump && waterPump->isRunning()) {
        waterPump->stop();
    }
}

bool WateringController::initialize()
{
    if (initialized) {
        return true;
    }
    
    // Check that all required components are provided
    if (!envSensor || !soilSensor || !waterPump || !dataStorage) {
        lastError = 1; // Missing component
        return false;
    }
    
    // Initialize all components
    bool success = true;
    
    if (!dataStorage->initialize()) {
        lastError = 2; // Data storage initialization failed
        success = false;
    }
    
    if (!envSensor->initialize()) {
        lastError = 3; // Environmental sensor initialization failed
        success = false;
    }
    
    if (!soilSensor->initialize()) {
        lastError = 4; // Soil sensor initialization failed
        success = false;
    }
    
    if (!waterPump->initialize()) {
        lastError = 5; // Water pump initialization failed
        success = false;
    }
    
    if (success) {
        // Load configuration from storage
        loadConfiguration();
        
        // Set initial timestamps
        lastSensorReadTime = 0; // Force immediate sensor read
        lastDataLogTime = 0;    // Force immediate data log
        lastWateringTime = 0;   // Reset watering timer
        
        initialized = true;
        lastError = 0;
    }
    
    return success;
}

void WateringController::loadConfiguration()
{
    // Load configuration from data storage
    String config = dataStorage->getConfig("watering_config", "");
    
    if (config.length() > 0) {
        // Parse JSON configuration
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, config);
        
        if (!error) {
            // Load configuration values
            if (doc.containsKey("sensorReadInterval")) {
                sensorReadInterval = doc["sensorReadInterval"];
            }
            
            if (doc.containsKey("dataLogInterval")) {
                dataLogInterval = doc["dataLogInterval"];
            }
            
            if (doc.containsKey("minWateringInterval")) {
                minWateringInterval = doc["minWateringInterval"];
            }
            
            if (doc.containsKey("moistureThresholdLow")) {
                moistureThresholdLow = doc["moistureThresholdLow"];
            }
            
            if (doc.containsKey("moistureThresholdHigh")) {
                moistureThresholdHigh = doc["moistureThresholdHigh"];
            }
            
            if (doc.containsKey("wateringDuration")) {
                wateringDuration = doc["wateringDuration"];
            }
            
            if (doc.containsKey("wateringEnabled")) {
                wateringEnabled = doc["wateringEnabled"];
            }
        }
    }
}

void WateringController::saveConfiguration()
{
    // Create JSON document with configuration
    DynamicJsonDocument doc(512);
    
    doc["sensorReadInterval"] = sensorReadInterval;
    doc["dataLogInterval"] = dataLogInterval;
    doc["minWateringInterval"] = minWateringInterval;
    doc["moistureThresholdLow"] = moistureThresholdLow;
    doc["moistureThresholdHigh"] = moistureThresholdHigh;
    doc["wateringDuration"] = wateringDuration;
    doc["wateringEnabled"] = wateringEnabled;
    
    // Serialize to string
    String config;
    serializeJson(doc, config);
    
    // Save to storage
    dataStorage->storeConfig("watering_config", config);
}

void WateringController::update()
{
    if (!initialized) {
        if (!initialize()) {
            return;
        }
    }
    
    // Update water pump to check for timed runs
    waterPump->update();
    
    // Check if it's time to read sensors
    unsigned long currentTime = millis();
    if ((currentTime - lastSensorReadTime) >= sensorReadInterval) {
        // Read environmental sensor
        if (envSensor->read()) {
            // Success, proceed with soil sensor
            if (soilSensor->read()) {
                // Both sensors read successfully
                processReadings();
            } else {
                // Soil sensor read failed
                lastError = 7;
            }
        } else {
            // Environmental sensor read failed
            lastError = 6;
        }
        
        lastSensorReadTime = currentTime;
    }
    
    // Check if it's time to log data
    if ((currentTime - lastDataLogTime) >= dataLogInterval) {
        logSensorData();
        lastDataLogTime = currentTime;
    }
}

bool WateringController::processReadings()
{
    // Get the current soil moisture
    float moisture = soilSensor->getMoisture();
    
    // Check if we need to water (and enough time has passed since last watering)
    if (wateringEnabled && !waterPump->isRunning() && 
        (moisture <= moistureThresholdLow) && 
        ((millis() - lastWateringTime) >= (minWateringInterval * 1000UL))) {
        
        // Start watering for the configured duration
        waterPump->runFor(wateringDuration);
        lastWateringTime = millis();
        return true;
    } 
    // Check if we need to stop watering (if moisture exceeds high threshold)
    else if (waterPump->isRunning() && (moisture >= moistureThresholdHigh)) {
        waterPump->stop();
    }
    
    return false;
}

void WateringController::logSensorData()
{
    time_t timestamp = time(nullptr);
    
    // Log environmental sensor data
    dataStorage->storeSensorReading("env", "temperature", envSensor->getTemperature(), timestamp);
    dataStorage->storeSensorReading("env", "humidity", envSensor->getHumidity(), timestamp);
    dataStorage->storeSensorReading("env", "pressure", envSensor->getPressure(), timestamp);
    
    // Log soil sensor data
    dataStorage->storeSensorReading("soil", "moisture", soilSensor->getMoisture(), timestamp);
    dataStorage->storeSensorReading("soil", "temperature", soilSensor->getTemperature(), timestamp);
    dataStorage->storeSensorReading("soil", "ph", soilSensor->getPH(), timestamp);
    dataStorage->storeSensorReading("soil", "ec", soilSensor->getEC(), timestamp);
    
    // Only log NPK if values are available (not -1)
    float nitrogen = soilSensor->getNitrogen();
    float phosphorus = soilSensor->getPhosphorus();
    float potassium = soilSensor->getPotassium();
    
    if (nitrogen >= 0) {
        dataStorage->storeSensorReading("soil", "nitrogen", nitrogen, timestamp);
    }
    
    if (phosphorus >= 0) {
        dataStorage->storeSensorReading("soil", "phosphorus", phosphorus, timestamp);
    }
    
    if (potassium >= 0) {
        dataStorage->storeSensorReading("soil", "potassium", potassium, timestamp);
    }
}

int WateringController::getLastError() const
{
    return lastError;
}

void WateringController::enableWatering(bool enable)
{
    wateringEnabled = enable;
    saveConfiguration();
}

bool WateringController::isWateringEnabled() const
{
    return wateringEnabled;
}

bool WateringController::manualWatering(unsigned int duration)
{
    if (!initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    bool result = false;
    
    if (duration > 0) {
        result = waterPump->runFor(duration);
    } else {
        result = waterPump->start();
    }
    
    if (result) {
        lastWateringTime = millis();
    }
    
    return result;
}

bool WateringController::stopWatering()
{
    if (!initialized) {
        lastError = 8; // Not initialized
        return false;
    }
    
    return waterPump->stop();
}

float WateringController::getCurrentMoisture() const
{
    if (!initialized || !soilSensor) {
        return -1.0f;
    }
    
    return soilSensor->getMoisture();
}

unsigned long WateringController::getTimeSinceLastWatering() const
{
    if (lastWateringTime == 0) {
        return 0;
    }
    
    return (millis() - lastWateringTime) / 1000UL; // Convert to seconds
}

void WateringController::setMoistureThresholdLow(float threshold)
{
    if (threshold >= 0.0f && threshold <= 100.0f) {
        moistureThresholdLow = threshold;
        saveConfiguration();
    }
}

void WateringController::setMoistureThresholdHigh(float threshold)
{
    if (threshold >= 0.0f && threshold <= 100.0f) {
        moistureThresholdHigh = threshold;
        saveConfiguration();
    }
}

void WateringController::setWateringDuration(unsigned int seconds)
{
    if (seconds > 0 && seconds <= 300) { // Max 5 minutes for safety
        wateringDuration = seconds;
        saveConfiguration();
    }
}

void WateringController::setMinWateringInterval(unsigned long seconds)
{
    if (seconds > 0) {
        minWateringInterval = seconds;
        saveConfiguration();
    }
}