/**
 * @file IWaterPump.h
 * @brief Interface for water pump control in the WateringSystem
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#ifndef I_WATER_PUMP_H
#define I_WATER_PUMP_H

#include "IActuator.h"

/**
 * @brief Interface for water pump control
 * 
 * This interface extends the base actuator interface with methods specific
 * to water pump control, such as starting, stopping, and running for a 
 * specific duration.
 */
class IWaterPump : public IActuator {
public:
    /**
     * @brief Start the pump
     * @return true if operation successful, false otherwise
     */
    virtual bool start() = 0;
    
    /**
     * @brief Stop the pump
     * @return true if operation successful, false otherwise
     */
    virtual bool stop() = 0;
    
    /**
     * @brief Run the pump for a specific duration
     * @param seconds Duration to run in seconds
     * @return true if operation successful, false otherwise
     */
    virtual bool runFor(unsigned int seconds) = 0;
    
    /**
     * @brief Check if the pump is currently running
     * @return true if running, false otherwise
     */
    virtual bool isRunning() = 0;
    
    /**
     * @brief Get the current run time in seconds if pump is running
     * @return Running time in seconds, 0 if not running
     */
    virtual unsigned int getRunTime() = 0;
    
    /**
     * @brief Get the configured run duration in seconds
     * @return Run duration in seconds, 0 if indefinite
     */
    virtual unsigned int getRunDuration() const = 0;
    
    /**
     * @brief Check if pump is running in manual mode
     * @return true if manual mode, false if automatic mode
     */
    virtual bool isManualMode() const = 0;
    
    /**
     * @brief Set manual mode flag
     * @param manual true for manual mode, false for automatic mode
     */
    virtual void setManualMode(bool manual) = 0;
    
    /**
     * @brief Update function that should be called regularly
     * 
     * This function performs any periodic updates needed for the pump,
     * such as checking if a timed run should be stopped.
     */
    virtual void update() = 0;
};

#endif // I_WATER_PUMP_H