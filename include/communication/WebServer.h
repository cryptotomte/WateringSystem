/**
 * @file WebServer.h
 * @brief Web server interface for the watering system
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#pragma once

#include <ESPAsyncWebServer.h>
#include "WateringController.h"
#include "sensors/IEnvironmentalSensor.h"
#include "sensors/ISoilSensor.h"
#include "actuators/IWaterPump.h"
#include "storage/IDataStorage.h"
#include <memory>

/**
 * @brief Web server for monitoring and controlling the watering system
 * 
 * This class provides a web interface to monitor sensor data, control
 * the water pump, and configure the watering system.
 */
class WebServer {
private:
    // System components
    WateringController* controller;
    IEnvironmentalSensor* envSensor;
    ISoilSensor* soilSensor;
    IWaterPump* waterPump;
    IDataStorage* dataStorage;
    
    // Web server
    AsyncWebServer server;
    
    // Server state
    bool initialized;
    int lastError;
    
    // Default server port
    static const int DEFAULT_PORT = 80;
    
    /**
     * @brief Setup API endpoints
     */
    void setupEndpoints();
    
    /**
     * @brief Handle API requests
     * @param request API request
     * @return JSON response
     */
    String handleApiRequest(AsyncWebServerRequest* request);
    
    /**
     * @brief Handle sensor data requests
     * @param request API request
     * @return JSON response with sensor data
     */
    String handleSensorDataRequest(AsyncWebServerRequest* request);
    
    /**
     * @brief Handle system status requests
     * @param request API request
     * @return JSON response with system status
     */
    String handleStatusRequest(AsyncWebServerRequest* request);
    
    /**
     * @brief Handle system control requests
     * @param request API request
     * @return JSON response with operation result
     */
    String handleControlRequest(AsyncWebServerRequest* request);
    
    /**
     * @brief Handle configuration requests
     * @param request API request
     * @return JSON response with configuration result
     */
    String handleConfigRequest(AsyncWebServerRequest* request);
    
    /**
     * @brief Handle historical data requests
     * @param request API request
     * @return JSON response with historical data
     */
    String handleHistoricalDataRequest(AsyncWebServerRequest* request);

public:
    /**
     * @brief Constructor for WebServer
     * @param controller Pointer to watering controller
     * @param environmental Pointer to environmental sensor
     * @param soil Pointer to soil sensor
     * @param pump Pointer to water pump
     * @param storage Pointer to data storage
     * @param port Server port (default: 80)
     */
    WebServer(WateringController* controller, 
             IEnvironmentalSensor* environmental,
             ISoilSensor* soil,
             IWaterPump* pump,
             IDataStorage* storage,
             int port = DEFAULT_PORT);
    
    /**
     * @brief Destructor
     */
    virtual ~WebServer();
    
    /**
     * @brief Initialize the web server
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Start the web server
     * @return true if server started successfully, false otherwise
     */
    bool start();
    
    /**
     * @brief Stop the web server
     * @return true if server stopped successfully, false otherwise
     */
    bool stop();
    
    /**
     * @brief Check if the server is running
     * @return true if running, false otherwise
     */
    bool isRunning() const;
    
    /**
     * @brief Get the last error code
     * @return Error code, 0 if no error
     */
    int getLastError() const;
};