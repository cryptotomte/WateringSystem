// SPDX-FileCopyrightText: 2025 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file IActuator.h
 * @brief Interface for all actuators in the system
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#ifndef I_ACTUATOR_H
#define I_ACTUATOR_H

/**
 * @brief Base interface for all actuators
 * 
 * This interface defines the common functionality for all actuator types
 * in the WateringSystem. It provides methods for initialization, control,
 * and checking status.
 */
class IActuator {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~IActuator() = default;
    
    /**
     * @brief Initialize the actuator
     * @return true if initialization successful, false otherwise
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Check if actuator is available and working
     * @return true if actuator is available, false otherwise
     */
    virtual bool isAvailable() = 0;
    
    /**
     * @brief Get last error code
     * @return error code, 0 if no error
     */
    virtual int getLastError() = 0;
    
    /**
     * @brief Get the name of the actuator
     * @return String containing actuator name
     */
    virtual const char* getName() const = 0;
};

#endif // I_ACTUATOR_H