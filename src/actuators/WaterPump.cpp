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
    Serial.println("WaterPump::start - Attempting to start pump");
    
    if (!initialized) {
        Serial.println("WaterPump::start - Pump not initialized, attempting to initialize");
        if (!initialize()) {
            Serial.println("WaterPump::start - Pump initialization failed");
            return false;
        }
        Serial.println("WaterPump::start - Pump initialization successful");
    }
    
    Serial.printf("WaterPump::start - Setting pin %d HIGH\n", controlPin);
    digitalWrite(controlPin, HIGH);
    running = true;
    startTime = millis();
    runDuration = 0; // Indefinite run
    
    Serial.println("WaterPump::start - Pump started successfully");
    return true;
}

bool WaterPump::stop()
{
    if (!initialized) {
        lastError = 1; // Not initialized
        return false;
    }
    
    digitalWrite(controlPin, LOW);
    running = false;
    
    return true;
}

bool WaterPump::runFor(unsigned int seconds)
{
    Serial.printf("WaterPump::runFor - Attempting to run pump for %d seconds\n", seconds);
    
    if (seconds == 0) {
        Serial.println("WaterPump::runFor - Duration is 0, stopping pump");
        return stop();
    }
    
    if (!start()) {
        Serial.println("WaterPump::runFor - Failed to start pump");
        return false;
    }
    
    runDuration = seconds;
    Serial.printf("WaterPump::runFor - Pump set to run for %d seconds\n", seconds);
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
        unsigned long elapsedMillis = millis() - startTime;
        unsigned int elapsedSeconds = elapsedMillis / 1000;
        
        // Debug timestamp when close to completion
        if (elapsedSeconds >= (runDuration - 2) && elapsedSeconds <= runDuration) {
            Serial.printf("WaterPump::checkTimedRun - Pump '%s' running for %u seconds of %u duration (elapsed ms: %lu)\n", 
                      name, elapsedSeconds, runDuration, elapsedMillis);
        }
        
        // Check if we've reached the run duration
        if (elapsedMillis >= (runDuration * 1000)) {
            Serial.printf("WaterPump::checkTimedRun - Pump '%s' reached run duration of %u seconds, stopping\n", 
                      name, runDuration);
            stop();
        }
    }
}

void WaterPump::update()
{
    checkTimedRun();
}