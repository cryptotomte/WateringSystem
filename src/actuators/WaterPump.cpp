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
    if (!initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    digitalWrite(controlPin, HIGH);
    running = true;
    startTime = millis();
    runDuration = 0; // Indefinite run
    
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
    if (seconds == 0) {
        return stop();
    }
    
    if (!start()) {
        return false;
    }
    
    runDuration = seconds;
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
    if (running && runDuration > 0) {        unsigned long elapsedMillis = millis() - startTime;
        
        // Check if we've reached the run duration
        if (elapsedMillis >= (runDuration * 1000)) {
            stop();
        }
    }
}

void WaterPump::update()
{
    checkTimedRun();
}