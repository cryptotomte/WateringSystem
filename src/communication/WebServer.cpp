/**
 * @file WebServer.cpp
 * @brief Implementation of the WebServer class
 * @author WateringSystem Team
 * @date 2025-04-15
 */

#include "communication/WebServer.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <time.h>

WebServer::WebServer(WateringController* controller, 
                   IEnvironmentalSensor* environmental,
                   ISoilSensor* soil,
                   IWaterPump* pump,
                   IDataStorage* storage,
                   int port)
    : controller(controller)
    , envSensor(environmental)
    , soilSensor(soil)
    , waterPump(pump)
    , dataStorage(storage)
    , server(port)
    , initialized(false)
    , lastError(0)
{
}

WebServer::~WebServer()
{
    // Stop the server in case it's still running
    if (initialized) {
        stop();
    }
}

bool WebServer::initialize()
{
    if (initialized) {
        return true;
    }
    
    // Check that all required components are provided
    if (!controller || !envSensor || !soilSensor || !waterPump || !dataStorage) {
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

void WebServer::setupEndpoints()
{
    // Serve static files from LittleFS
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    
    // Initialize the AsyncElegantOTA
    AsyncElegantOTA.begin(&server);
    
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
    
    // Handle 404 (Not Found)
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(404, "text/plain", "Not found");
    });
}

String WebServer::handleSensorDataRequest(AsyncWebServerRequest* request)
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

String WebServer::handleStatusRequest(AsyncWebServerRequest* request)
{
    DynamicJsonDocument doc(1024);
    
    // System status
    doc["pumpRunning"] = waterPump->isRunning();
    doc["wateringEnabled"] = controller->isWateringEnabled();
    
    if (waterPump->isRunning()) {
        doc["runTime"] = waterPump->getRunTime();
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
    network["ip"] = WiFi.localIP().toString();
    network["rssi"] = WiFi.RSSI();
    network["ssid"] = WiFi.SSID();
    
    // Add timestamp
    doc["timestamp"] = time(nullptr);
    
    String response;
    serializeJson(doc, response);
    return response;
}

String WebServer::handleControlRequest(AsyncWebServerRequest* request)
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

String WebServer::handleConfigRequest(AsyncWebServerRequest* request)
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

String WebServer::handleHistoricalDataRequest(AsyncWebServerRequest* request)
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

bool WebServer::start()
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

bool WebServer::stop()
{
    if (!initialized) {
        return false;
    }
    
    server.end();
    return true;
}

bool WebServer::isRunning() const
{
    return initialized;
}

int WebServer::getLastError() const
{
    return lastError;
}