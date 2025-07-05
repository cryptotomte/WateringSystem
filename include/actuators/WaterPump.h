/**
 * @file WaterPump.h
 * @brief Water pump implementation
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#ifndef WATER_PUMP_H
#define WATER_PUMP_H

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
    bool manualMode;      // Flag to track if pump is running in manual mode
    
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
    unsigned int getRunDuration() const override;
    bool isManualMode() const override;
    void setManualMode(bool manual) override;
    void update() override;
};

#endif // WATER_PUMP_H