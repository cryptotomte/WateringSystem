/**
 * @file WaterPump.h
 * @brief Water pump implementation
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#pragma once

#include "actuators/IWaterPump.h"
#include <Arduino.h>

/**
 * @brief Implementation of water pump interface
 * 
 * This class provides a concrete implementation of the IWaterPump
 * interface for controlling a 12V water pump through a MOSFET.
 */
class WaterPump : public IWaterPump {
private:
    int controlPin;       // GPIO pin connected to MOSFET gate
    bool initialized;
    bool running;
    int lastError;
    const char* name;
    unsigned long startTime;  // When the pump was started (millis)
    unsigned int runDuration; // How long to run in seconds
    
    /**
     * @brief Check if timed run has completed
     */
    void checkTimedRun();

public:
    /**
     * @brief Constructor for WaterPump
     * @param pin GPIO pin connected to MOSFET gate for pump control
     * @param pumpName Name to identify this pump
     */
    WaterPump(int pin, const char* pumpName = "WaterPump");
    
    /**
     * @brief Destructor
     */
    virtual ~WaterPump();
    
    // IActuator interface implementations
    bool initialize() override;
    bool isAvailable() override;
    int getLastError() override;
    const char* getName() const override;
    
    // IWaterPump interface implementations
    bool start() override;
    bool stop() override;
    bool runFor(unsigned int seconds) override;
    bool isRunning() override;
    unsigned int getRunTime() override;
    
    /**
     * @brief Update function that should be called regularly
     * 
     * This function checks if a timed run should be stopped and
     * should be called regularly in the main loop.
     */
    void update();
};