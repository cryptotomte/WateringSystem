/**
 * @file WateringController.h
 * @brief Main controller for the watering system
 * @author WateringSystem Team
 * @date 2025-04-15
 */

#pragma once

#include "sensors/IEnvironmentalSensor.h"
#include "sensors/ISoilSensor.h"
#include "actuators/IWaterPump.h"
#include "storage/IDataStorage.h"
#include <Arduino.h>

/**
 * @brief Main controller for the WateringSystem
 * 
 * This class coordinates the sensors, actuators, and logic for the
 * watering system. It handles reading sensors, making watering decisions,
 * controlling the pump, and managing schedules.
 */
class WateringController {
private:
    // Component interfaces
    IEnvironmentalSensor* envSensor;
    ISoilSensor* soilSensor;
    IWaterPump* waterPump;
    IDataStorage* dataStorage;
    
    // System state
    bool initialized;
    int lastError;
    unsigned long lastSensorReadTime;
    unsigned long lastDataLogTime;
    unsigned long lastWateringTime;
    bool wateringEnabled;
    
    // Configuration
    unsigned long sensorReadInterval;       // Milliseconds between sensor readings
    unsigned long dataLogInterval;          // Milliseconds between data logging
    unsigned long minWateringInterval;      // Minimum seconds between waterings
    float moistureThresholdLow;             // Threshold to start watering (%)
    float moistureThresholdHigh;            // Threshold to stop watering (%)
    unsigned int wateringDuration;          // Seconds to run the pump
    
    /**
     * @brief Load configuration from storage
     */
    void loadConfiguration();
    
    /**
     * @brief Save configuration to storage
     */
    void saveConfiguration();
    
    /**
     * @brief Process sensor readings and make watering decisions
     * @return true if watering was initiated, false otherwise
     */
    bool processReadings();
    
    /**
     * @brief Log sensor data to storage
     */
    void logSensorData();

public:
    /**
     * @brief Constructor for WateringController
     * @param environmental Pointer to environmental sensor
     * @param soil Pointer to soil sensor
     * @param pump Pointer to water pump
     * @param storage Pointer to data storage
     */
    WateringController(IEnvironmentalSensor* environmental, ISoilSensor* soil,
                      IWaterPump* pump, IDataStorage* storage);
    
    /**
     * @brief Destructor
     */
    virtual ~WateringController();
    
    /**
     * @brief Initialize the controller and all components
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Main update function, call regularly in loop
     */
    void update();
    
    /**
     * @brief Get the last error code
     * @return Error code, 0 if no error
     */
    int getLastError() const;
    
    /**
     * @brief Enable or disable automatic watering
     * @param enable true to enable, false to disable
     */
    void enableWatering(bool enable);
    
    /**
     * @brief Check if automatic watering is enabled
     * @return true if enabled, false if disabled
     */
    bool isWateringEnabled() const;
    
    /**
     * @brief Manually start the water pump
     * @param duration Seconds to run, 0 for indefinite
     * @return true if operation successful, false otherwise
     */
    bool manualWatering(unsigned int duration = 0);
    
    /**
     * @brief Stop the water pump
     * @return true if operation successful, false otherwise
     */
    bool stopWatering();
    
    /**
     * @brief Get current moisture level
     * @return Moisture level in percent, -1 if unavailable
     */
    float getCurrentMoisture() const;
    
    /**
     * @brief Get seconds since last watering
     * @return Seconds since last watering, 0 if never watered
     */
    unsigned long getTimeSinceLastWatering() const;
    
    /**
     * @brief Set the low moisture threshold
     * @param threshold Moisture level in percent to trigger watering
     */
    void setMoistureThresholdLow(float threshold);
    
    /**
     * @brief Set the high moisture threshold
     * @param threshold Moisture level in percent to stop watering
     */
    void setMoistureThresholdHigh(float threshold);
    
    /**
     * @brief Set watering duration
     * @param seconds Seconds to run the pump when watering
     */
    void setWateringDuration(unsigned int seconds);
    
    /**
     * @brief Set minimum interval between waterings
     * @param seconds Minimum seconds between waterings
     */
    void setMinWateringInterval(unsigned long seconds);
};