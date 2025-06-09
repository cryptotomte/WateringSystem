/**
 * @file WateringSystemWebServer.h
 * @brief Web server interface for the watering system
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#ifndef WATERING_SYSTEM_WEB_SERVER_H
#define WATERING_SYSTEM_WEB_SERVER_H

#include "WateringController.h"
#include "sensors/IEnvironmentalSensor.h"
#include "sensors/ISoilSensor.h"
#include "actuators/IWaterPump.h"
#include "storage/IDataStorage.h"
#include <memory>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// Forward declaration of WiFi config callback type
typedef bool (*WiFiConfigSaveCallback)(const String&, const String&);

// Forward declaration of Reservoir pump callback types
typedef void (*ReservoirPumpEnableCallback)(bool);
typedef bool (*ReservoirPumpStatusCallback)(bool*, bool*, bool*);
typedef bool (*ReservoirPumpManualFillCallback)(uint16_t);
typedef void (*ReservoirPumpStopCallback)();
typedef bool (*ReservoirPumpEnabledCheckCallback)();
typedef void (*ReservoirAutoLevelControlEnableCallback)(bool);
typedef bool (*ReservoirAutoLevelControlEnabledCheckCallback)();

/**
 * @brief Web server for monitoring and controlling the watering system
 * 
 * This class provides a web interface to monitor sensor data, control
 * the water pump, and configure the watering system.
 */
class WateringSystemWebServer {
private:
    // System components
    WateringController* controller;
    IEnvironmentalSensor* envSensor;
    ISoilSensor* soilSensor;
    IWaterPump* plantPump;  // Renamed from waterPump to plantPump for clarity
    IWaterPump* reservoirPump; // Added for reservoir control
    IDataStorage* dataStorage;
    
    // Web server
    AsyncWebServer server;
    
    // Server state
    bool initialized;
    int lastError;
    bool isInApMode;
    
    // WiFi configuration callback
    WiFiConfigSaveCallback wifiConfigCallback;
      // Reservoir pump callbacks
    ReservoirPumpEnableCallback reservoirPumpEnableCallback;
    ReservoirPumpStatusCallback reservoirPumpStatusCallback;
    ReservoirPumpManualFillCallback reservoirPumpManualFillCallback;
    ReservoirPumpStopCallback reservoirPumpStopCallback;
    ReservoirPumpEnabledCheckCallback reservoirPumpEnabledCheckCallback;
    ReservoirAutoLevelControlEnableCallback reservoirAutoLevelControlEnableCallback;
    ReservoirAutoLevelControlEnabledCheckCallback reservoirAutoLevelControlEnabledCheckCallback;
    
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
     */    String handleConfigRequest(AsyncWebServerRequest* request);
    
    /**
     * @brief Handle historical data requests
     * @param request API request
     * @return JSON response with historical data
     */
    String handleHistoricalDataRequest(AsyncWebServerRequest* request);
    
    /**
     * @brief Handle WiFi configuration requests
     * @param request API request
     * @return JSON response with WiFi configuration result
     */
    String handleWiFiConfigRequest(AsyncWebServerRequest* request);
    
    /**
     * @brief Handle WiFi scanning requests
     * @param request API request
     * @return JSON response with available WiFi networks
     */
    String handleWiFiScanRequest(AsyncWebServerRequest* request);

    /**
     * @brief Handle auto watering request with form data
     * @param request Web request
     */
    void handleAutoWateringFormRequest(AsyncWebServerRequest *request);

public:
    /**
     * @brief Constructor for WateringSystemWebServer
     * @param controller Pointer to watering controller
     * @param environmental Pointer to environmental sensor
     * @param soil Pointer to soil sensor
     * @param plant Pointer to plant water pump
     * @param reservoir Pointer to reservoir pump (can be nullptr if not used)
     * @param storage Pointer to data storage
     * @param port Server port (default: 80)
     */
    WateringSystemWebServer(WateringController* controller, 
             IEnvironmentalSensor* environmental,
             ISoilSensor* soil,
             IWaterPump* plant,
             IDataStorage* storage,
             int port = DEFAULT_PORT,
             IWaterPump* reservoir = nullptr);
    
    /**
     * @brief Destructor
     */
    virtual ~WateringSystemWebServer();
    
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
    
    /**
     * @brief Set the WiFi configuration callback
     * @param callback Function to call when WiFi credentials are saved
     */
    void setWiFiConfigCallback(WiFiConfigSaveCallback callback);
    
    /**
     * @brief Enable AP mode configuration interface
     * @param enabled true to enable AP mode interface, false otherwise
     */
    void enableApMode(bool enabled);
    
    /**
     * @brief Check if AP mode is enabled
     * @return true if in AP mode, false otherwise
     */
    bool isApModeEnabled() const;
    
    /**
     * @brief Set the reservoir pump enable callback
     * @param callback Function to call when reservoir pump feature is enabled/disabled
     */
    void setReservoirPumpEnableCallback(ReservoirPumpEnableCallback callback);
    
    /**
     * @brief Set the reservoir pump status callback
     * @param callback Function to call to get reservoir pump status
     */
    void setReservoirPumpStatusCallback(ReservoirPumpStatusCallback callback);
    
    /**
     * @brief Set the reservoir pump manual fill callback
     * @param callback Function to call to start manual reservoir filling
     */
    void setReservoirPumpManualFillCallback(ReservoirPumpManualFillCallback callback);
    
    /**
     * @brief Set the reservoir pump stop callback
     * @param callback Function to call to stop the reservoir pump
     */
    void setReservoirPumpStopCallback(ReservoirPumpStopCallback callback);
    
    /**
     * @brief Set the reservoir pump enabled check callback
     * @param callback Function to call to check if reservoir pump feature is enabled
     */
    void setReservoirPumpEnabledCheckCallback(ReservoirPumpEnabledCheckCallback callback);
    
    /**
     * @brief Set the reservoir automatic level control enable callback
     * @param callback Function to call when automatic level control is enabled/disabled
     */
    void setReservoirAutoLevelControlEnableCallback(ReservoirAutoLevelControlEnableCallback callback);
    
    /**
     * @brief Set the reservoir automatic level control enabled check callback
     * @param callback Function to call to check if automatic level control is enabled
     */
    void setReservoirAutoLevelControlEnabledCheckCallback(ReservoirAutoLevelControlEnabledCheckCallback callback);

    /**
     * @brief Handle reservoir pump API requests
     * @param request API request
     * @return JSON response with reservoir pump operation result     */
    String handleReservoirPumpRequest(AsyncWebServerRequest* request);
};

#endif // WATERING_SYSTEM_WEB_SERVER_H