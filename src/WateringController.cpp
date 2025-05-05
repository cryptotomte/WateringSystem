/**
 * @file WateringController.cpp
 * @brief Implementation of the main watering controller
 * @author Paul Waserbrot
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
    
    // Check minimum required components for manual operation
    if (!waterPump || !dataStorage) {
        lastError = 1; // Missing critical component
        Serial.println("WateringController - Missing critical component (pump or storage)");
        return false;
    }
    
    bool fullSuccess = true;
    bool pumpSuccess = false;
    bool storageSuccess = false;
    
    // Initialize storage
    if (dataStorage->initialize()) {
        storageSuccess = true;
    } else {
        lastError = 2; // Data storage initialization failed
        fullSuccess = false;
        Serial.println("WateringController - Data storage initialization failed");
    }
    
    // Initialize water pump (critical for manual operation)
    if (waterPump->initialize()) {
        pumpSuccess = true;
    } else {
        lastError = 5; // Water pump initialization failed
        fullSuccess = false;
        Serial.println("WateringController - Water pump initialization failed");
    }
    
    // Initialize sensors (non-critical for manual operation)
    bool envSensorSuccess = false;
    if (envSensor) {
        if (envSensor->initialize()) {
            envSensorSuccess = true;
        } else {
            lastError = 3; // Environmental sensor initialization failed
            fullSuccess = false;
            Serial.println("WateringController - Environmental sensor initialization failed");
        }
    }
    
    bool soilSensorSuccess = false;
    if (soilSensor) {
        if (soilSensor->initialize()) {
            soilSensorSuccess = true;
        } else {
            lastError = 4; // Soil sensor initialization failed
            fullSuccess = false;
            Serial.println("WateringController - Soil sensor initialization failed");
        }
    }
    
    // We can initialize the controller if at minimum the pump works properly
    if (pumpSuccess) {
        // Load configuration from storage if available
        if (storageSuccess) {
            loadConfiguration();
        }
        
        // Set initial timestamps
        lastSensorReadTime = 0; // Force immediate sensor read
        lastDataLogTime = 0;    // Force immediate data log
        lastWateringTime = 0;   // Reset watering timer
        
        initialized = true;
        lastError = fullSuccess ? 0 : lastError; // Keep last error if not full success
        
        Serial.println("WateringController initialized successfully (manual mode available)");
    } else {
        Serial.println("WateringController initialization failed for manual operation");
    }
    
    return initialized;
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
    Serial.println("DEBUG-CONTROLLER: WateringController::manualWatering called");
    
    if (!initialized) {
        Serial.println("DEBUG-CONTROLLER: WateringController not initialized, trying to initialize");
        if (!initialize()) {
            Serial.printf("DEBUG-CONTROLLER: Initialization failed with error: %d\n", lastError);
            return false;
        }
        Serial.println("DEBUG-CONTROLLER: Initialization successful");
    }
    
    if (!waterPump) {
        Serial.println("DEBUG-CONTROLLER: Water pump is null");
        lastError = 10; // Invalid water pump
        return false;
    }
    
    bool result = false;
    
    if (duration > 0) {
        Serial.printf("DEBUG-CONTROLLER: Starting pump for %d seconds\n", duration);
        result = waterPump->runFor(duration);
    } else {
        Serial.println("DEBUG-CONTROLLER: Starting pump indefinitely");
        result = waterPump->start();
    }
    
    if (result) {
        Serial.println("DEBUG-CONTROLLER: Pump started successfully");
        lastWateringTime = millis();
    } else {
        Serial.printf("DEBUG-CONTROLLER: Failed to start pump, error: %d\n", waterPump->getLastError());
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