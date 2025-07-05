/**
 * @file WaterPump.cpp
 * @brief Implementation of the water pump controller
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#include "actuators/WaterPump.h"

WaterPump::WaterPump(int pin, const char* pumpName)
    : controlPin(pin)
    , initialized(false)
    , running(false)
    , lastError(0)
    , name(pumpName)
    , startTime(0)
    , runDuration(0)
    , manualMode(false)
{
}

WaterPump::~WaterPump()
{
    // Ensure pump is off when object is destroyed
    if (initialized && running) {
        stop();
    }
}

bool WaterPump::initialize()
{
    if (initialized) {
        return true;
    }
    
    // Setup control pin as output
    pinMode(controlPin, OUTPUT);
    digitalWrite(controlPin, LOW); // Ensure pump is off
    
    initialized = true;
    lastError = 0;
    return true;
}

bool WaterPump::isAvailable()
{
    return initialized;
}

int WaterPump::getLastError()
{
    return lastError;
}

const char* WaterPump::getName() const
{
    return name;
}

bool WaterPump::start()
{
    if (!initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    digitalWrite(controlPin, HIGH);
    running = true;
    startTime = millis();
    runDuration = 0; // Indefinite run
    
    // Automatic mode by default for start()
    manualMode = false;
    
    Serial.printf("DEBUG-PUMP: Pump %s started at %lu ms (AUTOMATIC MODE)\n", name, startTime);
    
    return true;
}

bool WaterPump::stop()
{
    if (!initialized) {
        lastError = 1; // Not initialized
        return false;
    }
    
    if (running) {
        unsigned long elapsedMillis = millis() - startTime;
        Serial.printf("DEBUG-PUMP: Stopping pump %s after %lu ms (%.1f seconds) - %s MODE\n", 
                      name, elapsedMillis, elapsedMillis / 1000.0f, 
                      manualMode ? "MANUAL" : "AUTOMATIC");
    }
    
    digitalWrite(controlPin, LOW);
    running = false;
    manualMode = false; // Reset manual mode flag
    
    return true;
}

bool WaterPump::runFor(unsigned int seconds)
{
    if (seconds == 0) {
        return stop();
    }
    
    Serial.printf("DEBUG-PUMP: Starting pump %s for %u seconds at %lu ms (MANUAL MODE)\n", 
                  name, seconds, millis());
    
    if (!start()) {
        Serial.printf("DEBUG-PUMP: Failed to start pump %s\n", name);
        return false;
    }
    
    // Set manual mode flag AFTER start() so it doesn't get overridden
    manualMode = true;
    
    runDuration = seconds;
    Serial.printf("DEBUG-PUMP: Pump %s started successfully, will run until %lu ms (MANUAL MODE)\n", 
                  name, startTime + (runDuration * 1000));
    return true;
}

bool WaterPump::isRunning()
{
    checkTimedRun();
    return running;
}

unsigned int WaterPump::getRunTime()
{
    if (!running) {
        return 0;
    }
    
    // Return elapsed time in seconds
    return (millis() - startTime) / 1000;
}

unsigned int WaterPump::getRunDuration() const
{
    return runDuration;
}

void WaterPump::checkTimedRun()
{
    if (running && runDuration > 0) {
        unsigned long currentTime = millis();
        unsigned long elapsedMillis = currentTime - startTime;
        unsigned long targetMillis = runDuration * 1000;
        
        // Debug-logging every 2 seconds while running
        static unsigned long lastDebugTime = 0;
        if (currentTime - lastDebugTime >= 2000) {
            Serial.printf("DEBUG-PUMP: %s running: %lu/%lu ms (%.1f/%u seconds)\n", 
                          name, elapsedMillis, targetMillis, 
                          elapsedMillis / 1000.0f, runDuration);
            lastDebugTime = currentTime;
        }
        
        // Check if we've reached the run duration
        if (elapsedMillis >= targetMillis) {
            Serial.printf("DEBUG-PUMP: Stopping pump %s after %lu ms (target: %lu ms, duration: %u seconds)\n", 
                          name, elapsedMillis, targetMillis, runDuration);
            stop();
        }
    }
}

void WaterPump::update()
{
    checkTimedRun();
}

bool WaterPump::isManualMode() const
{
    return manualMode;
}

void WaterPump::setManualMode(bool manual)
{
    manualMode = manual;
    Serial.printf("DEBUG-PUMP: Pump %s set to %s mode\n", name, manual ? "MANUAL" : "AUTOMATIC");
}