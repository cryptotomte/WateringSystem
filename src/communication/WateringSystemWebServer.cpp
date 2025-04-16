/**
 * @file WateringSystemWebServer.cpp
 * @brief Implementation of web server for remote control and monitoring
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

// Include our own header first
#include "communication/WateringSystemWebServer.h"

// Third-party includes with correct order to avoid conflicts
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <time.h>

// Use AsyncJson after ArduinoJson
#include <AsyncJson.h>

// Other includes can be added here as needed

WateringSystemWebServer::WateringSystemWebServer(WateringController* controller, 
                   IEnvironmentalSensor* environmental,
                   ISoilSensor* soil,
                   IWaterPump* plant,
                   IDataStorage* storage,
                   int port,
                   IWaterPump* reservoir)
    : controller(controller)
    , envSensor(environmental)
    , soilSensor(soil)
    , plantPump(plant)
    , reservoirPump(reservoir)
    , dataStorage(storage)
    , server(port)
    , initialized(false)
    , lastError(0)
    , isInApMode(false)
    , wifiConfigCallback(nullptr)
    , reservoirPumpEnableCallback(nullptr)
    , reservoirPumpStatusCallback(nullptr)
    , reservoirPumpManualFillCallback(nullptr)
    , reservoirPumpStopCallback(nullptr)
    , reservoirPumpEnabledCheckCallback(nullptr)
{
}

WateringSystemWebServer::~WateringSystemWebServer()
{
    // Stop the server in case it's still running
    if (initialized) {
        stop();
    }
}

bool WateringSystemWebServer::initialize()
{
    if (initialized) {
        return true;
    }
    
    // Check that all required components are provided
    if (!controller || !envSensor || !soilSensor || !plantPump || !dataStorage) {
        lastError = 1; // Missing component
        return false;
    }
    
    // Verify LittleFS is mounted
    if (!LittleFS.begin(false)) {
        lastError = 2; // Failed to mount filesystem
        return false;
    }
    
    // Setup web server endpoints
    setupEndpoints();
    
    initialized = true;
    lastError = 0;
    return true;
}

void WateringSystemWebServer::setupEndpoints()
{
    // Set the appropriate default file based on mode
    if (isInApMode) {
        // In AP mode, use wifi_setup.html as the default page
        server.serveStatic("/", LittleFS, "/").setDefaultFile("wifi_setup.html");
        
        // Also serve the regular index.html for users who have already configured
        server.serveStatic("/index.html", LittleFS, "/index.html");
    } else {
        // Normal operation mode, use index.html
        server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
        
        // Also serve the wifi setup page if needed
        server.serveStatic("/wifi_setup.html", LittleFS, "/wifi_setup.html");
    }
    
    // API endpoints
    server.on("/api/sensor-data", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String response = handleSensorDataRequest(request);
        request->send(200, "application/json", response);
    });
    
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String response = handleStatusRequest(request);
        request->send(200, "application/json", response);
    });
    
    server.on("/api/control", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String response = handleControlRequest(request);
        request->send(200, "application/json", response);
    });
    
    server.on("/api/config", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String response = handleConfigRequest(request);
        request->send(200, "application/json", response);
    });
    
    server.on("/api/historical-data", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String response = handleHistoricalDataRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Reservoir pump control endpoint
    server.on("/api/reservoir", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String response = handleReservoirPumpRequest(request);
        request->send(200, "application/json", response);
    });
    
    // WiFi configuration endpoints (always available, but only functional in AP mode)
    server.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String response = handleWiFiScanRequest(request);
        request->send(200, "application/json", response);
    });
    
    server.on("/api/wifi/config", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String response = handleWiFiConfigRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Handle 404 (Not Found)
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });
}

String WateringSystemWebServer::handleSensorDataRequest(AsyncWebServerRequest* request)
{
    // Read latest sensor data
    bool envSuccess = envSensor->read();
    bool soilSuccess = soilSensor->read();
    
    // Create JSON response
    DynamicJsonDocument doc(1024);
    
    // Environmental sensor data
    JsonObject env = doc.createNestedObject("environmental");
    env["success"] = envSuccess;
    
    if (envSuccess) {
        env["temperature"] = envSensor->getTemperature();
        env["humidity"] = envSensor->getHumidity();
        env["pressure"] = envSensor->getPressure();
    } else {
        env["error"] = envSensor->getLastError();
    }
    
    // Soil sensor data
    JsonObject soil = doc.createNestedObject("soil");
    soil["success"] = soilSuccess;
    
    if (soilSuccess) {
        soil["moisture"] = soilSensor->getMoisture();
        soil["temperature"] = soilSensor->getTemperature();
        soil["humidity"] = soilSensor->getHumidity();
        soil["ph"] = soilSensor->getPH();
        soil["ec"] = soilSensor->getEC();
        
        // Only include NPK if available
        float n = soilSensor->getNitrogen();
        float p = soilSensor->getPhosphorus();
        float k = soilSensor->getPotassium();
        
        if (n >= 0) soil["nitrogen"] = n;
        if (p >= 0) soil["phosphorus"] = p;
        if (k >= 0) soil["potassium"] = k;
    } else {
        soil["error"] = soilSensor->getLastError();
    }
    
    // Add timestamp
    doc["timestamp"] = time(nullptr);
    
    String response;
    serializeJson(doc, response);
    return response;
}

String WateringSystemWebServer::handleStatusRequest(AsyncWebServerRequest* request)
{
    DynamicJsonDocument doc(1024);
    
    // System status
    doc["pumpRunning"] = plantPump->isRunning();
    doc["wateringEnabled"] = controller->isWateringEnabled();
    
    if (plantPump->isRunning()) {
        doc["runTime"] = plantPump->getRunTime();
    }
    
    // Reservoir status if available
    if (reservoirPump != nullptr && reservoirPumpStatusCallback && reservoirPumpEnabledCheckCallback) {
        bool isLow = false;
        bool isHigh = false;
        bool isRunning = false;
        
        JsonObject reservoir = doc.createNestedObject("reservoir");
        reservoir["enabled"] = reservoirPumpEnabledCheckCallback();
        
        if (reservoirPumpStatusCallback(&isLow, &isHigh, &isRunning)) {
            reservoir["lowLevelDetected"] = isLow;
            reservoir["highLevelDetected"] = isHigh;
            reservoir["pumpRunning"] = isRunning;
            
            if (isRunning && reservoirPump) {
                reservoir["runTime"] = reservoirPump->getRunTime();
            }
        }
    }
    
    // Configuration
    JsonObject config = doc.createNestedObject("config");
    config["moistureThresholdLow"] = controller->getMoistureThresholdLow();
    config["moistureThresholdHigh"] = controller->getMoistureThresholdHigh();
    config["wateringDuration"] = controller->getWateringDuration();
    config["minWateringInterval"] = controller->getMinWateringInterval();
    
    // Storage
    JsonObject storage = doc.createNestedObject("storage");
    uint32_t totalSpace, usedSpace;
    
    if (dataStorage->getStorageStats(&totalSpace, &usedSpace)) {
        storage["totalKB"] = totalSpace / 1024;
        storage["usedKB"] = usedSpace / 1024;
        storage["percentUsed"] = (usedSpace * 100.0) / totalSpace;
    }
    
    // Network
    JsonObject network = doc.createNestedObject("network");
    
    if (isInApMode) {
        network["mode"] = "AP";
        network["ip"] = WiFi.softAPIP().toString();
        network["ssid"] = WiFi.softAPSSID();
        network["stationCount"] = WiFi.softAPgetStationNum();
    } else {
        network["mode"] = "STA";
        network["ip"] = WiFi.localIP().toString();
        network["rssi"] = WiFi.RSSI();
        network["ssid"] = WiFi.SSID();
        network["connected"] = WiFi.status() == WL_CONNECTED;
    }
    
    // Add timestamp
    doc["timestamp"] = time(nullptr);
    
    String response;
    serializeJson(doc, response);
    return response;
}

String WateringSystemWebServer::handleControlRequest(AsyncWebServerRequest* request)
{
    DynamicJsonDocument doc(256);
    bool success = false;
    String message = "Invalid command";
    
    // Get command parameter
    if (request->hasParam("command", true)) {
        String command = request->getParam("command", true)->value();
        
        if (command == "start") {
            // Get duration parameter (optional)
            int duration = 0;
            if (request->hasParam("duration", true)) {
                duration = request->getParam("duration", true)->value().toInt();
            }
            
            success = controller->manualWatering(duration);
            message = success ? "Watering started" : "Failed to start watering";
        }
        else if (command == "stop") {
            success = controller->stopWatering();
            message = success ? "Watering stopped" : "Failed to stop watering";
        }
        else if (command == "enable") {
            controller->enableWatering(true);
            success = true;
            message = "Automatic watering enabled";
        }
        else if (command == "disable") {
            controller->enableWatering(false);
            success = true;
            message = "Automatic watering disabled";
        }
    }
    
    doc["success"] = success;
    doc["message"] = message;
    
    String response;
    serializeJson(doc, response);
    return response;
}

String WateringSystemWebServer::handleConfigRequest(AsyncWebServerRequest* request)
{
    DynamicJsonDocument doc(256);
    bool success = false;
    String message = "No changes made";
    
    // Process configuration parameters
    bool configChanged = false;
    
    if (request->hasParam("moistureThresholdLow", true)) {
        float value = request->getParam("moistureThresholdLow", true)->value().toFloat();
        controller->setMoistureThresholdLow(value);
        configChanged = true;
    }
    
    if (request->hasParam("moistureThresholdHigh", true)) {
        float value = request->getParam("moistureThresholdHigh", true)->value().toFloat();
        controller->setMoistureThresholdHigh(value);
        configChanged = true;
    }
    
    if (request->hasParam("wateringDuration", true)) {
        int value = request->getParam("wateringDuration", true)->value().toInt();
        controller->setWateringDuration(value);
        configChanged = true;
    }
    
    if (request->hasParam("minWateringInterval", true)) {
        long value = request->getParam("minWateringInterval", true)->value().toInt();
        controller->setMinWateringInterval(value);
        configChanged = true;
    }
    
    if (configChanged) {
        success = true;
        message = "Configuration updated";
    }
    
    doc["success"] = success;
    doc["message"] = message;
    
    String response;
    serializeJson(doc, response);
    return response;
}

String WateringSystemWebServer::handleHistoricalDataRequest(AsyncWebServerRequest* request)
{
    DynamicJsonDocument doc(8192); // Larger doc for historical data
    
    // Get parameters
    String sensorId = "env";  // Default: environmental sensor
    String readingType = "temperature"; // Default: temperature
    time_t startTime = 0;
    time_t endTime = time(nullptr);
    
    if (request->hasParam("sensorId")) {
        sensorId = request->getParam("sensorId")->value();
    }
    
    if (request->hasParam("readingType")) {
        readingType = request->getParam("readingType")->value();
    }
    
    if (request->hasParam("startTime")) {
        startTime = request->getParam("startTime")->value().toInt();
    }
    
    if (request->hasParam("endTime")) {
        endTime = request->getParam("endTime")->value().toInt();
    }
    
    // Get readings from storage
    String readings = dataStorage->getSensorReadings(sensorId, readingType, startTime, endTime);
    
    // Parse readings into JSON array
    doc["sensorId"] = sensorId;
    doc["readingType"] = readingType;
    doc["startTime"] = startTime;
    doc["endTime"] = endTime;
    
    // Parse readings JSON string into JSON array
    DynamicJsonDocument readingsDoc(8192);
    deserializeJson(readingsDoc, readings);
    doc["readings"] = readingsDoc.as<JsonArray>();
    
    String response;
    serializeJson(doc, response);
    return response;
}

String WateringSystemWebServer::handleWiFiScanRequest(AsyncWebServerRequest* request)
{
    DynamicJsonDocument doc(4096); // Large document for WiFi scan results
    JsonArray networks = doc.createNestedArray("networks");
    
    // Scan for WiFi networks
    int networksFound = WiFi.scanNetworks();
    
    for(int i = 0; i < networksFound; i++) {
        if(i >= 20) break; // Limit to 20 networks to prevent memory issues
        
        JsonObject network = networks.createNestedObject();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        network["encryption"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    
    // Clean up scan results
    WiFi.scanDelete();
    
    doc["count"] = networksFound;
    doc["success"] = true;
    doc["apMode"] = isInApMode;
    
    String response;
    serializeJson(doc, response);
    return response;
}

String WateringSystemWebServer::handleWiFiConfigRequest(AsyncWebServerRequest* request)
{
    DynamicJsonDocument doc(512);
    bool success = false;
    String message = "WiFi configuration not updated";
    
    // Only process WiFi configuration if in AP mode and callback is set
    if(!isInApMode) {
        doc["success"] = false;
        doc["message"] = "WiFi configuration only available in AP mode";
        
        String response;
        serializeJson(doc, response);
        return response;
    }
    
    if(!wifiConfigCallback) {
        doc["success"] = false;
        doc["message"] = "WiFi configuration callback not set";
        
        String response;
        serializeJson(doc, response);
        return response;
    }
    
    // Get WiFi parameters
    if(request->hasParam("ssid", true) && request->hasParam("password", true)) {
        String ssid = request->getParam("ssid", true)->value();
        String password = request->getParam("password", true)->value();
        
        // Validate SSID
        if(ssid.length() < 1 || ssid.length() > 32) {
            doc["success"] = false;
            doc["message"] = "Invalid SSID length (1-32 characters required)";
            
            String response;
            serializeJson(doc, response);
            return response;
        }
        
        // Validate password (8+ characters or empty for open networks)
        if(password.length() > 0 && password.length() < 8) {
            doc["success"] = false;
            doc["message"] = "WiFi password must be at least 8 characters";
            
            String response;
            serializeJson(doc, response);
            return response;
        }
        
        // Call the callback to save the configuration
        success = wifiConfigCallback(ssid, password);
        
        if(success) {
            message = "WiFi configuration saved successfully. The system will restart and attempt to connect to the network.";
        } else {
            message = "Failed to save WiFi configuration";
        }
    } else {
        message = "Missing required parameters: ssid and password";
    }
    
    doc["success"] = success;
    doc["message"] = message;
    doc["restartRequired"] = success;
    
    String response;
    serializeJson(doc, response);
    return response;
}

String WateringSystemWebServer::handleReservoirPumpRequest(AsyncWebServerRequest* request)
{
    DynamicJsonDocument doc(256);
    bool success = false;
    String message = "Invalid command";
    
    // Check if reservoir pump feature has been enabled
    bool hasReservoirFeature = reservoirPump != nullptr && reservoirPumpEnabledCheckCallback != nullptr;
    bool isReservoirEnabled = hasReservoirFeature && reservoirPumpEnabledCheckCallback();
    
    // Return error if feature is not available or not enabled
    if (!hasReservoirFeature) {
        doc["success"] = false;
        doc["message"] = "Reservoir pump feature not available";
        
        String response;
        serializeJson(doc, response);
        return response;
    }
    
    // Get command parameter
    if (request->hasParam("command", true)) {
        String command = request->getParam("command", true)->value();
        
        if (command == "enable") {
            // Enable the reservoir pump feature
            if (reservoirPumpEnableCallback) {
                reservoirPumpEnableCallback(true);
                success = true;
                message = "Reservoir pump feature enabled";
            } else {
                message = "Enable callback not set";
            }
        }
        else if (command == "disable") {
            // Disable the reservoir pump feature
            if (reservoirPumpEnableCallback) {
                reservoirPumpEnableCallback(false);
                success = true;
                message = "Reservoir pump feature disabled";
            } else {
                message = "Disable callback not set";
            }
        }
        else if (command == "start" && isReservoirEnabled) {
            // Start manual reservoir filling
            if (reservoirPumpManualFillCallback) {
                // Get duration parameter (optional)
                int duration = 0;
                if (request->hasParam("duration", true)) {
                    duration = request->getParam("duration", true)->value().toInt();
                }
                
                success = reservoirPumpManualFillCallback(duration);
                message = success ? "Reservoir filling started" : "Failed to start reservoir filling";
            } else {
                message = "Start callback not set";
            }
        }
        else if (command == "stop" && isReservoirEnabled) {
            // Stop the reservoir pump
            if (reservoirPumpStopCallback) {
                reservoirPumpStopCallback();
                success = true;
                message = "Reservoir pump stopped";
            } else {
                message = "Stop callback not set";
            }
        }
        else if (command == "status") {
            // Get reservoir status
            if (reservoirPumpStatusCallback) {
                bool isLow = false;
                bool isHigh = false;
                bool isRunning = false;
                
                success = reservoirPumpStatusCallback(&isLow, &isHigh, &isRunning);
                
                if (success) {
                    JsonObject status = doc.createNestedObject("status");
                    status["enabled"] = isReservoirEnabled;
                    status["lowLevelDetected"] = isLow;
                    status["highLevelDetected"] = isHigh;
                    status["pumpRunning"] = isRunning;
                    message = "Status retrieved successfully";
                } else {
                    message = "Failed to get reservoir status";
                }
            } else {
                message = "Status callback not set";
            }
        }
    }
    
    doc["success"] = success;
    doc["message"] = message;
    
    String response;
    serializeJson(doc, response);
    return response;
}

bool WateringSystemWebServer::start()
{
    if (!initialized) {
        if (!initialize()) {
            return false;
        }
    }
    
    // Start the server
    server.begin();
    return true;
}

bool WateringSystemWebServer::stop()
{
    if (!initialized) {
        return false;
    }
    
    server.end();
    return true;
}

bool WateringSystemWebServer::isRunning() const
{
    return initialized;
}

void WateringSystemWebServer::setWiFiConfigCallback(WiFiConfigSaveCallback callback)
{
    wifiConfigCallback = callback;
}

void WateringSystemWebServer::enableApMode(bool enabled)
{
    isInApMode = enabled;
}

bool WateringSystemWebServer::isApModeEnabled() const
{
    return isInApMode;
}

int WateringSystemWebServer::getLastError() const
{
    return lastError;
}

void WateringSystemWebServer::setReservoirPumpEnableCallback(ReservoirPumpEnableCallback callback)
{
    reservoirPumpEnableCallback = callback;
}

void WateringSystemWebServer::setReservoirPumpStatusCallback(ReservoirPumpStatusCallback callback)
{
    reservoirPumpStatusCallback = callback;
}

void WateringSystemWebServer::setReservoirPumpManualFillCallback(ReservoirPumpManualFillCallback callback)
{
    reservoirPumpManualFillCallback = callback;
}

void WateringSystemWebServer::setReservoirPumpStopCallback(ReservoirPumpStopCallback callback)
{
    reservoirPumpStopCallback = callback;
}

void WateringSystemWebServer::setReservoirPumpEnabledCheckCallback(ReservoirPumpEnabledCheckCallback callback)
{
    reservoirPumpEnabledCheckCallback = callback;
}