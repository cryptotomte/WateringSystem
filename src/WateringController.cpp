/**
 * @file WateringController.cpp
 * @brief Implementation of the main watering controller
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#include "WateringController.h"
#include <ArduinoJson.h>

// Default configuration values
// NOTE: Using FreeRTOS task for non-blocking sensor reading to prevent WiFi stability issues
// Sensors now read in separate task with proper synchronization using mutex
#define DEFAULT_SENSOR_READ_INTERVAL 5000UL      // 5 seconds (faster response for automatic watering)
#define DEFAULT_DATA_LOG_INTERVAL    300000UL    // 5 minutes
#define DEFAULT_MIN_WATERING_INTERVAL 300UL      // 5 minutes (reduced from 6 hours for testing)
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
    , sensorTaskHandle(nullptr)
    , sensorDataMutex(nullptr)
    , sensorTaskRunning(false)
    , newSensorDataAvailable(false)
    , sensorReadSuccess(false)
    , sensorReadInterval(DEFAULT_SENSOR_READ_INTERVAL)
    , dataLogInterval(DEFAULT_DATA_LOG_INTERVAL)
    , minWateringInterval(DEFAULT_MIN_WATERING_INTERVAL)
    , moistureThresholdLow(DEFAULT_MOISTURE_THRESHOLD_LOW)
    , moistureThresholdHigh(DEFAULT_MOISTURE_THRESHOLD_HIGH)
    , wateringDuration(DEFAULT_WATERING_DURATION)
{
    // Create mutex for sensor data synchronization
    sensorDataMutex = xSemaphoreCreateMutex();
}

WateringController::~WateringController()
{
    // Stop sensor task if running
    stopSensorTask();
    
    // Delete mutex
    if (sensorDataMutex != nullptr) {
        vSemaphoreDelete(sensorDataMutex);
        sensorDataMutex = nullptr;
    }
    
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
    }    bool soilSensorSuccess = false;
    if (soilSensor) {
        Serial.printf("DEBUG-CONTROLLER: Attempting soil sensor initialization at %lu ms\n", millis());
        if (soilSensor->initialize()) {
            soilSensorSuccess = true;
            Serial.printf("DEBUG-CONTROLLER: Soil sensor initialization SUCCESS at %lu ms\n", millis());
        } else {
            lastError = 4; // Soil sensor initialization failed
            fullSuccess = false;
            Serial.printf("DEBUG-CONTROLLER: Soil sensor initialization FAILED at %lu ms (error: %d)\n", 
                         millis(), soilSensor->getLastError());
        }
    }
    
    // We can initialize the controller if at minimum the pump works properly
    if (pumpSuccess) {
        // Load configuration from storage if available
        if (storageSuccess) {
            loadConfiguration();
        }        // Set initial timestamps
        lastSensorReadTime = 0; // Force immediate sensor read
        lastDataLogTime = 0;    // Force immediate data log
        lastWateringTime = 0;   // Reset watering timer
        lastValidSensorTime = 0; // No valid sensor data yet
          // Start sensor task if we have at least some sensors (even if soil sensor fails)
        // This allows the system to continue operating and potentially recover
        if (envSensorSuccess || soilSensorSuccess || soilSensor) {
            startSensorTask();
            Serial.println("WateringController - Sensor task started (will attempt soil sensor recovery)");
        }
        
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
    waterPump->update();    // Check if new sensor data is available (non-blocking)
    if (newSensorDataAvailable) {
        // Take mutex to safely access sensor data
        if (xSemaphoreTake(sensorDataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            // SAFETY: Always check sensor availability
            if (soilSensor && soilSensor->isAvailable()) {
                processReadings();
            } else {
                // CRITICAL SAFETY: Sensor failed - STOP PUMP IMMEDIATELY!
                if (waterPump && waterPump->isRunning()) {
                    Serial.println("SAFETY: Sensor failed - EMERGENCY PUMP STOP!");
                    waterPump->stop();
                }
                // Mark sensor data as invalid
                lastValidSensorTime = 0;
                lastError = 7;
            }
            
            // Reset flag
            newSensorDataAvailable = false;
            lastSensorReadTime = millis();
            
            xSemaphoreGive(sensorDataMutex);
        }
    }    // REMOVED: Extra sensor reading during watering - FreeRTOS task handles all sensor reading
    // The sensor task reads every 5 seconds and processReadings() is called from update() 
    // when newSensorDataAvailable flag is set, providing fast enough response for watering control
    
    // ADDITIONAL SAFETY CHECK: Periodic verification that pump doesn't run without fresh sensor data
    unsigned long currentTime = millis();
    if (waterPump && waterPump->isRunning()) {
        // If pump is running but we haven't had valid sensor data recently, EMERGENCY STOP
        if (lastValidSensorTime == 0 || (currentTime - lastValidSensorTime) > 30000UL) {
            Serial.println("SAFETY: Pump running without recent sensor data - EMERGENCY STOP!");
            waterPump->stop();
        }
    }
    
    // Check if it's time to log data
    if ((currentTime - lastDataLogTime) >= dataLogInterval) {
        logSensorData();
        lastDataLogTime = currentTime;
    }
}

bool WateringController::processReadings()
{
    // SAFETY CHECK: Ensure we have recent sensor data
    unsigned long currentTime = millis();
    if (lastValidSensorTime > 0 && (currentTime - lastValidSensorTime) > 30000UL) {
        // No valid sensor data for 30 seconds - EMERGENCY STOP!
        if (waterPump && waterPump->isRunning()) {
            Serial.println("SAFETY: Sensor data too old - EMERGENCY PUMP STOP!");
            waterPump->stop();
        }
        return false;
    }
    
    // Mark that we have fresh sensor data
    lastValidSensorTime = currentTime;
    
    // Get the current soil moisture
    float moisture = soilSensor->getMoisture();
    
    // SAFETY CHECK: Validate moisture reading
    if (moisture < 0.0f || moisture > 100.0f) {
        Serial.printf("SAFETY: Invalid moisture reading %.1f%% - cannot proceed with automatic watering\n", moisture);
        // Stop pump if running on invalid data
        if (waterPump && waterPump->isRunning()) {
            Serial.println("SAFETY: Invalid sensor data - EMERGENCY PUMP STOP!");
            waterPump->stop();
        }
        return false;
    }
    
    // Check if we need to water (NO minimum interval - if it's dry, water immediately!)
    if (wateringEnabled && !waterPump->isRunning() && 
        (moisture <= moistureThresholdLow)) {
        
        Serial.printf("AUTO-WATERING: Starting - Moisture %.1f%% <= %.1f%% (threshold)\n", 
                     moisture, moistureThresholdLow);
        
        // Start watering for the configured duration
        waterPump->runFor(wateringDuration);
        lastWateringTime = millis();
        return true;
    } 
    // Check if we need to stop watering (if moisture exceeds high threshold)
    else if (waterPump->isRunning() && (moisture >= moistureThresholdHigh)) {
        Serial.printf("AUTO-WATERING: Stopping early - Moisture %.1f%% >= %.1f%% (high threshold)\n", 
                     moisture, moistureThresholdHigh);
        waterPump->stop();
        return false;
    }
    
    // Log status during watering for debugging
    if (waterPump->isRunning()) {
        Serial.printf("AUTO-WATERING: Active - Moisture %.1f%%, Target %.1f%%, Runtime %us\n", 
                     moisture, moistureThresholdHigh, waterPump->getRunTime());
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
    
    if (!waterPump) {
        lastError = 10; // Invalid water pump
        return false;
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

// FreeRTOS sensor task implementation

void WateringController::sensorTaskWrapper(void* parameter)
{
    WateringController* controller = static_cast<WateringController*>(parameter);
    controller->sensorTask();
}

void WateringController::sensorTask()
{
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t taskFrequency = pdMS_TO_TICKS(sensorReadInterval);
    
    while (sensorTaskRunning) {
        bool readSuccess = false;
        
        // Read environmental sensor
        bool envSuccess = true;
        if (envSensor) {
            envSuccess = envSensor->read();
            if (!envSuccess) {
                Serial.println("WateringController - Environmental sensor read failed in task");
            }
        }        // Read soil sensor
        bool soilSuccess = true;
        if (soilSensor) {
            soilSuccess = soilSensor->read();
            if (!soilSuccess) {
                Serial.println("WateringController - Soil sensor read failed in task");
            } else {
                // Show moisture level and watering status for debugging
                float moisture = soilSensor->getMoisture();
                bool pumpRunning = waterPump && waterPump->isRunning();
                Serial.printf("SENSOR-TASK: Moisture %.1f%% (threshold: %.1f%%) %s\n", 
                             moisture, moistureThresholdLow, 
                             pumpRunning ? "[PUMP RUNNING]" : "[PUMP STOPPED]");
            }
        }
          // Both sensors must succeed (if they exist), but allow partial success
        // If at least one sensor works, we can continue with system operation
        readSuccess = envSuccess && soilSuccess;
        bool systemUsable = envSuccess || soilSuccess; // At least one sensor working
        
        // Update shared data with mutex protection
        if (xSemaphoreTake(sensorDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            sensorReadSuccess = readSuccess;
            newSensorDataAvailable = true; // Always signal new data attempt
            xSemaphoreGive(sensorDataMutex);
        }
        
        // Wait for next reading interval
        vTaskDelayUntil(&lastWakeTime, taskFrequency);
    }
    
    // Task cleanup
    sensorTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

bool WateringController::startSensorTask()
{
    if (sensorTaskRunning || sensorTaskHandle != nullptr) {
        return true; // Already running
    }
    
    if (sensorDataMutex == nullptr) {
        Serial.println("WateringController - Cannot start sensor task: mutex not created");
        return false;
    }
    
    sensorTaskRunning = true;
    
    BaseType_t result = xTaskCreate(
        sensorTaskWrapper,          // Task function
        "SensorTask",              // Task name
        4096,                      // Stack size
        this,                      // Task parameter
        1,                         // Priority (low priority)
        &sensorTaskHandle          // Task handle
    );
    
    if (result != pdPASS) {
        Serial.println("WateringController - Failed to create sensor task");
        sensorTaskRunning = false;
        return false;
    }
    
    Serial.println("WateringController - Sensor task started successfully");
    return true;
}

void WateringController::stopSensorTask()
{
    if (!sensorTaskRunning) {
        return; // Not running
    }
    
    sensorTaskRunning = false;
    
    // Wait for task to finish (with timeout)
    int timeout = 50; // 5 seconds
    while (sensorTaskHandle != nullptr && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout--;
    }
    
    if (sensorTaskHandle != nullptr) {
        Serial.println("WateringController - Force deleting sensor task");
        vTaskDelete(sensorTaskHandle);
        sensorTaskHandle = nullptr;
    }
    
    Serial.println("WateringController - Sensor task stopped");
}