/**
 * @file IWaterPump.h
 * @brief Interface for water pump control in the WateringSystem
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#pragma once

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
};