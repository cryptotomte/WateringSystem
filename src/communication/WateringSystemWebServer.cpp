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
    
    // We no longer initialize LittleFS here since it's already initialized in LittleFSStorage
    // Just verify that it's mounted by checking if a file exists
    if (!LittleFS.exists("/index.html")) {
        Serial.println("Warning: index.html not found in LittleFS. Web interface may not work correctly.");
        // Continue anyway, as we might be in AP mode using wifi_setup.html
    }
    
    // Setup web server endpoints
    setupEndpoints();
    
    initialized = true;
    lastError = 0;
    return true;
}

void WateringSystemWebServer::setupEndpoints()
{
    // Set up static file serving
    if (isInApMode) {
        // In AP mode, use wifi_setup.html as the default page
        server.serveStatic("/", LittleFS, "/").setDefaultFile("wifi_setup.html");
    } else {
        // Normal operation mode, use index.html
        server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    }
    
    // Explicitly serve common static files
    server.serveStatic("/index.html", LittleFS, "/index.html");
    server.serveStatic("/wifi_setup.html", LittleFS, "/wifi_setup.html");
    server.serveStatic("/script.js", LittleFS, "/script.js");
    server.serveStatic("/styles.css", LittleFS, "/styles.css");
    server.serveStatic("/favicon.ico", LittleFS, "/favicon.ico");
    
    // *** IMPORTANT FIX: Handle the "/api" route differently, accepting it as a valid route prefix
    // This fixes the 400 Bad Request issue when clients call /api/sensors
    server.on("/api", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Instead of returning 400, return instructions about available endpoints
        request->send(200, "application/json", "{\"message\":\"API root. Available endpoints: /api/sensors, /api/status, etc.\"}");
    });
    
    // API endpoints - match both endpoints from client code for compatibility
    server.on("/api/sensor-data", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String response = handleSensorDataRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Add a compatible endpoint for /api/sensors that client uses
    server.on("/api/sensors", HTTP_GET, [this](AsyncWebServerRequest *request) {
        // Add explicit debug message
        Serial.println("API endpoint /api/sensors called");
        
        String response = handleSensorDataRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Also provide endpoint without /api prefix for flexibility
    server.on("/sensors", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String response = handleSensorDataRequest(request);
        request->send(200, "application/json", response);
    });
    
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String response = handleStatusRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Also provide status endpoint without /api prefix
    server.on("/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String response = handleStatusRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Add compatible endpoints for the control operations with /api prefix
    server.on("/api/control/water/start", HTTP_POST, [this](AsyncWebServerRequest *request) {
        Serial.println("API endpoint /api/control/water/start called");
        
        // Extract duration parameter if present in form data
        int duration = 20; // Default duration to 20 seconds
        if (request->hasParam("duration", true)) {
            duration = request->getParam("duration", true)->value().toInt();
            Serial.printf("Duration parameter from form data: %d seconds\n", duration);
        }
        
        // Check for content-type to see if it's a JSON request
        bool isJson = false;
        if (request->contentType() == "application/json") {
            isJson = true;
        }
        
        // If it's JSON but we didn't find duration in form params, look for JSON body
        // This needs to be handled through AsyncCallbackJsonWebHandler instead
        
        bool success = controller->manualWatering(duration);
        String message = success ? "Watering started" : "Failed to start watering";
        Serial.printf("Starting watering for %d seconds, result: %s\n", duration, success ? "success" : "failed");
        
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->printf("{\"success\":%s,\"message\":\"%s\",\"duration\":%d}", 
                        success ? "true" : "false", 
                        message.c_str(),
                        duration);
        request->send(response);
    });
    
    // Add same endpoint without /api prefix
    server.on("/control/water/start", HTTP_POST, [this](AsyncWebServerRequest *request) {
        Serial.println("Endpoint /control/water/start called");
        
        // Extract duration parameter if present in form data
        int duration = 20; // Default duration to 20 seconds
        if (request->hasParam("duration", true)) {
            duration = request->getParam("duration", true)->value().toInt();
            Serial.printf("Duration parameter from form data: %d seconds\n", duration);
        }
        
        // Check for content-type to see if it's a JSON request
        bool isJson = false;
        if (request->contentType() == "application/json") {
            isJson = true;
        }
        
        // If it's JSON but we didn't find duration in form params, look for JSON body
        // This needs to be handled through AsyncCallbackJsonWebHandler instead
        
        bool success = controller->manualWatering(duration);
        String message = success ? "Watering started" : "Failed to start watering";
        Serial.printf("Starting watering for %d seconds, result: %s\n", duration, success ? "success" : "failed");
        
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->printf("{\"success\":%s,\"message\":\"%s\",\"duration\":%d}", 
                        success ? "true" : "false", 
                        message.c_str(),
                        duration);
        request->send(response);
    });
    
    // Create JSON handlers for the water start endpoints
    AsyncCallbackJsonWebHandler* waterStartHandler = new AsyncCallbackJsonWebHandler(
        "/api/control/water/start", 
        [this](AsyncWebServerRequest *request, JsonVariant &json) {
            Serial.println("DEBUG-WATERING: JSON API endpoint /api/control/water/start called with JSON data");
            
            // Default value
            int duration = 20; // Default to 20 seconds
            
            if (json.is<JsonObject>()) {
                JsonObject jsonObj = json.as<JsonObject>();
                
                if (jsonObj.containsKey("duration")) {
                    duration = jsonObj["duration"].as<int>();
                    Serial.printf("DEBUG-WATERING: JSON duration parameter: %d seconds\n", duration);
                }
            }
            
            Serial.println("DEBUG-WATERING: About to call controller->manualWatering()");
            bool success = controller->manualWatering(duration);
            String message = success ? "Watering started" : "Failed to start watering";
            Serial.printf("DEBUG-WATERING: Starting watering from JSON handler for %d seconds, result: %s\n", 
                         duration, success ? "success" : "failed");
            
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            response->printf("{\"success\":%s,\"message\":\"%s\",\"duration\":%d}", 
                            success ? "true" : "false", 
                            message.c_str(),
                            duration);
            Serial.println("DEBUG-WATERING: Sending response to client");
            request->send(response);
        }
    );
    server.addHandler(waterStartHandler);
    
    // Add handler for the endpoint without /api prefix
    AsyncCallbackJsonWebHandler* waterStartHandlerNoPrefix = new AsyncCallbackJsonWebHandler(
        "/control/water/start", 
        [this](AsyncWebServerRequest *request, JsonVariant &json) {
            Serial.println("DEBUG-WATERING: JSON endpoint /control/water/start called with JSON data");
            
            // Default value
            int duration = 20; // Default to 20 seconds
            
            if (json.is<JsonObject>()) {
                JsonObject jsonObj = json.as<JsonObject>();
                
                if (jsonObj.containsKey("duration")) {
                    duration = jsonObj["duration"].as<int>();
                    Serial.printf("DEBUG-WATERING: JSON duration parameter: %d seconds\n", duration);
                }
            }
            
            Serial.println("DEBUG-WATERING: About to call controller->manualWatering()");
            bool success = controller->manualWatering(duration);
            String message = success ? "Watering started" : "Failed to start watering";
            Serial.printf("DEBUG-WATERING: Starting watering from JSON handler for %d seconds, result: %s\n", 
                         duration, success ? "success" : "failed");
            
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            response->printf("{\"success\":%s,\"message\":\"%s\",\"duration\":%d}", 
                            success ? "true" : "false", 
                            message.c_str(),
                            duration);
            Serial.println("DEBUG-WATERING: Sending response to client");
            request->send(response);
        }
    );
    server.addHandler(waterStartHandlerNoPrefix);
    
    server.on("/api/control/water/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        bool success = controller->stopWatering();
        String message = success ? "Watering stopped" : "Failed to stop watering";
        
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->printf("{\"success\":%s,\"message\":\"%s\"}", success ? "true" : "false", message.c_str());
        request->send(response);
    });
    
    // Add same endpoint without /api prefix
    server.on("/control/water/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        bool success = controller->stopWatering();
        String message = success ? "Watering stopped" : "Failed to stop watering";
        
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->printf("{\"success\":%s,\"message\":\"%s\"}", success ? "true" : "false", message.c_str());
        request->send(response);
    });
    
    // Create JSON handlers for the auto watering endpoint
    AsyncCallbackJsonWebHandler* autoWateringHandler = new AsyncCallbackJsonWebHandler(
        "/api/control/auto", 
        [this](AsyncWebServerRequest *request, JsonVariant &json) {
            Serial.println("JSON API endpoint /api/control/auto called");
            handleAutoWateringJsonRequest(request, json);
        }
    );
    server.addHandler(autoWateringHandler);
    
    // Add handler for the endpoint without /api prefix
    AsyncCallbackJsonWebHandler* autoWateringHandlerNoPrefix = new AsyncCallbackJsonWebHandler(
        "/control/auto", 
        [this](AsyncWebServerRequest *request, JsonVariant &json) {
            Serial.println("JSON endpoint /control/auto called");
            handleAutoWateringJsonRequest(request, json);
        }
    );
    server.addHandler(autoWateringHandlerNoPrefix);
    
    // Add fallback handlers for form-based requests (for backward compatibility)
    server.on("/api/control/auto", HTTP_POST, [this](AsyncWebServerRequest *request) {
        Serial.println("Form-based API endpoint /api/control/auto called");
        handleAutoWateringFormRequest(request);
    });
    
    server.on("/control/auto", HTTP_POST, [this](AsyncWebServerRequest *request) {
        Serial.println("Form-based endpoint /control/auto called");
        handleAutoWateringFormRequest(request);
    });
    
    // Keep the original endpoint for compatibility
    server.on("/api/control", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String response = handleControlRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Add endpoint without /api prefix 
    server.on("/control", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String response = handleControlRequest(request);
        request->send(200, "application/json", response);
    });
    
    server.on("/api/config", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String response = handleConfigRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Add endpoint without /api prefix
    server.on("/config", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String response = handleConfigRequest(request);
        request->send(200, "application/json", response);
    });
    
    server.on("/api/history", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String response = handleHistoricalDataRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Add endpoint without /api prefix
    server.on("/history", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String response = handleHistoricalDataRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Keep original endpoint for compatibility
    server.on("/api/historical-data", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String response = handleHistoricalDataRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Reservoir pump control endpoint
    server.on("/api/reservoir", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String response = handleReservoirPumpRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Add endpoint without /api prefix
    server.on("/reservoir", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String response = handleReservoirPumpRequest(request);
        request->send(200, "application/json", response);
    });
    
    // WiFi configuration endpoints (always available, but only functional in AP mode)
    server.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String response = handleWiFiScanRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Add endpoint without /api prefix
    server.on("/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String response = handleWiFiScanRequest(request);
        request->send(200, "application/json", response);
    });
    
    server.on("/api/wifi/config", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String response = handleWiFiConfigRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Add endpoint without /api prefix
    server.on("/wifi/config", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String response = handleWiFiConfigRequest(request);
        request->send(200, "application/json", response);
    });
    
    // Handle 404 (Not Found)
    server.onNotFound([](AsyncWebServerRequest *request) {
        // Log the unhandled request
        Serial.print("Unhandled request: ");
        Serial.println(request->url());
        
        // Check if it's an API request to give a more helpful error
        if (request->url().startsWith("/api/")) {
            request->send(404, "application/json", "{\"success\":false,\"message\":\"API endpoint not found\"}");
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });
}

/**
 * Handle auto watering request with JSON data
 */
void WateringSystemWebServer::handleAutoWateringJsonRequest(AsyncWebServerRequest *request, JsonVariant &json)
{
    // Default value (only used if no valid parameter is found)
    bool enable = false;
    bool paramFound = false;
    
    if (json.is<JsonObject>()) {
        JsonObject jsonObj = json.as<JsonObject>();
        
        if (jsonObj.containsKey("enabled")) {
            enable = jsonObj["enabled"].as<bool>();
            paramFound = true;
            Serial.print("Auto watering JSON parameter 'enabled' received: ");
            Serial.println(enable ? "true" : "false");
        }
    }
    
    // Only proceed if we found a parameter, otherwise log an error
    if (paramFound) {
        // Log the action being taken
        Serial.print("Setting auto watering to: ");
        Serial.println(enable ? "Enabled" : "Disabled");
        
        controller->enableWatering(enable);
        String message = enable ? "Automatic watering enabled" : "Automatic watering disabled";
        
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->printf("{\"success\":true,\"message\":\"%s\",\"enabled\":%s}", 
                         message.c_str(), 
                         enable ? "true" : "false");
        request->send(response);
    } else {
        Serial.println("No valid auto watering parameter found in JSON");
        
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->printf("{\"success\":false,\"message\":\"No valid parameter found in JSON request\"}");
        request->send(response);
    }
}

/**
 * Handle auto watering request with form data
 */
void WateringSystemWebServer::handleAutoWateringFormRequest(AsyncWebServerRequest *request)
{
    // Default value (only used if no valid parameter is found)
    bool enable = false;
    bool paramFound = false;
    
    // Check for 'enabled' parameter (used by client, form-encoded)
    if (request->hasParam("enabled", true)) {
        String value = request->getParam("enabled", true)->value();
        enable = (value == "true" || value == "1");
        paramFound = true;
        Serial.print("Auto watering form parameter 'enabled' received: ");
        Serial.println(value);
    }
    // Also check for 'enable' parameter (backward compatibility)
    else if (request->hasParam("enable", true)) {
        String value = request->getParam("enable", true)->value();
        enable = (value == "true" || value == "1");
        paramFound = true;
        Serial.print("Auto watering form parameter 'enable' received: ");
        Serial.println(value);
    }
    
    // Only proceed if we found a parameter, otherwise log an error
    if (paramFound) {
        // Log the action being taken
        Serial.print("Setting auto watering to: ");
        Serial.println(enable ? "Enabled" : "Disabled");
        
        controller->enableWatering(enable);
        String message = enable ? "Automatic watering enabled" : "Automatic watering disabled";
        
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->printf("{\"success\":true,\"message\":\"%s\",\"enabled\":%s}", 
                         message.c_str(), 
                         enable ? "true" : "false");
        request->send(response);
    } else {
        Serial.println("No valid auto watering parameter found in form data");
        
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->printf("{\"success\":false,\"message\":\"No valid parameter found in form request\"}");
        request->send(response);
    }
}

String WateringSystemWebServer::handleSensorDataRequest(AsyncWebServerRequest* request)
{
    // Add debug log
    Serial.println("handleSensorDataRequest called");

    // Read latest sensor data
    bool envSuccess = envSensor->read();
    
    Serial.print("Environment sensor read result: ");
    Serial.println(envSuccess ? "Success" : "Failed");
    
    if (envSuccess) {
        Serial.printf("  Temp: %.1f°C, Humidity: %.1f%%, Pressure: %.1f hPa\n", 
            envSensor->getTemperature(), 
            envSensor->getHumidity(), 
            envSensor->getPressure());
    } else {
        Serial.printf("  Error code: %d\n", envSensor->getLastError());
    }
    
    bool soilSuccess = soilSensor->read();
    
    Serial.print("Soil sensor read result: ");
    Serial.println(soilSuccess ? "Success" : "Failed");
    
    if (!soilSuccess) {
        Serial.printf("  Error code: %d\n", soilSensor->getLastError());
    }
    
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
    
    // Debug: print the response
    Serial.println("Sensor data response:");
    Serial.println(response);
    
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
        
        // Calculate and include remaining time if pump is running with a duration set
        unsigned int duration = plantPump->getRunDuration();
        if (duration > 0) {
            unsigned int elapsed = plantPump->getRunTime();
            if (elapsed < duration) {
                doc["remainingTime"] = duration - elapsed;
            } else {
                doc["remainingTime"] = 0;
            }
        }
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