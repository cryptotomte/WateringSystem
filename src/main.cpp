/**
 * @file main.cpp
 * @brief Main application for the WateringSystem
 * @author Paul Waserbrot
 * @date 2025-04-15
 */

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// Include our components
#include "WateringController.h"
#include "sensors/BME280Sensor.h"
#include "sensors/ModbusSoilSensor.h"
#include "communication/SP3485ModbusClient.h"
#include "actuators/WaterPump.h"
#include "storage/LittleFSStorage.h"
#include "communication/WateringSystemWebServer.h"
#include "hardware/RS485Config.h" // Hardware configuration for RS485

// Pin definitions based on hardware.md specification with optical isolation
#define PIN_I2C_SDA           21
#define PIN_I2C_SCL           22
#define PIN_RS485_TX          16  // Direct connection via optocoupler
#define PIN_RS485_RX          17  // Direct connection via optocoupler  
#define PIN_RS485_DE          25  // Direction control via optocoupler
#define PIN_MAIN_PUMP_CONTROL 26
#define PIN_RESERVOIR_PUMP_CONTROL 27  
#define PIN_RESERVOIR_LOW_LEVEL   32  // Sensor for low water level in reservoir
#define PIN_RESERVOIR_HIGH_LEVEL  33  // Sensor for high water level in reservoir
#define PIN_STATUS_LED        2       
#define PIN_BUTTON_MANUAL     5       
#define PIN_BUTTON_CONFIG     18

// Other constants
#define SOIL_SENSOR_MODBUS_ADDR 0x01
#define BME280_I2C_ADDR       0x77
#define WIFI_TIMEOUT          60000  // 60 seconds timeout for WiFi connection
#define NTP_SERVER            "0.se.pool.ntp.org"
#define STATUS_CHECK_INTERVAL 5000   // 5 seconds between status checks/display
#define WEB_SERVER_PORT       80
#define CONFIG_FILE_PATH      "/wifi_config.json"
#define AP_SSID               "WateringSystem-Setup"
#define AP_PASSWORD           "watering123"
#define DEFAULT_SSID          "CONFIGURE_ME"

// Global component instances
BME280Sensor envSensor(BME280_I2C_ADDR, "BME280");
HardwareSerial rs485Serial(2);  // Using UART2 for RS485
SP3485ModbusClient modbusClient(&rs485Serial, PIN_RS485_DE); // Removed power control pin - now hardware managed
ModbusSoilSensor soilSensor(&modbusClient, SOIL_SENSOR_MODBUS_ADDR, "SoilSensor");
WaterPump plantPump(PIN_MAIN_PUMP_CONTROL, "PlantPump");         // Renamed for clarity
WaterPump reservoirPump(PIN_RESERVOIR_PUMP_CONTROL, "ReservoirPump"); // Added new pump for filling reservoir
LittleFSStorage dataStorage;
WateringController controller(&envSensor, &soilSensor, &plantPump, &dataStorage);
WateringSystemWebServer webServer(&controller, &envSensor, &soilSensor, &plantPump, &dataStorage, WEB_SERVER_PORT, &reservoirPump);

// System state
unsigned long lastStatusUpdate = 0;
unsigned long lastButtonCheck = 0;
bool manualButtonPressed = false;
bool configButtonPressed = false;
bool systemReady = false;
bool apMode = false;

// Reservoir system state
bool reservoirPumpEnabled = false;   // Is reservoir pump feature enabled
bool reservoirLowLevel = false;      // Is water level low in reservoir
bool reservoirHighLevel = false;     // Is water level high in reservoir
bool reservoirPumpRunning = false;   // Is reservoir pump currently running
unsigned long reservoirPumpStartTime = 0; // When the reservoir pump started
#define RESERVOIR_PUMP_MAX_RUNTIME 300000 // Maximum run time for reservoir pump (5 minutes)

// Restart state (moved to global scope to fix the build error)
bool restartScheduled = false;
unsigned long restartTime = 0;

// WiFi configuration
struct WiFiConfig {
    String ssid;
    String password;
};

WiFiConfig wifiConfig;

// WiFi monitoring and stability variables
unsigned long lastWiFiCheck = 0;
unsigned long lastWiFiReconnect = 0;
unsigned long lastWiFiDiagnostic = 0;
int wifiReconnectAttempts = 0;
int wifiDisconnectCount = 0;
bool wifiStable = false;
const int WIFI_CHECK_INTERVAL = 5000;        // Check WiFi every 5 seconds
const int WIFI_RECONNECT_INTERVAL = 10000;   // Wait 10 seconds between reconnect attempts
const int WIFI_DIAGNOSTIC_INTERVAL = 30000;  // Print diagnostics every 30 seconds
const int MAX_RECONNECT_ATTEMPTS = 5;        // Max attempts before longer delay

// Watchdog variables
unsigned long lastLoopTime = 0;
unsigned long loopWatchdogTimeout = 30000;  // 30 seconds watchdog
bool watchdogEnabled = true;

// Forward declarations
void monitorWiFiConnection();
void checkWatchdog();
void feedWatchdog();
bool saveWiFiConfig(const String& ssid, const String& password);
void checkEmergencyWiFiReset();

/**
 * @brief Print detailed WiFi diagnostics
 */
void printWiFiDiagnostics() {
    if (millis() - lastWiFiDiagnostic < WIFI_DIAGNOSTIC_INTERVAL) {
        return;
    }
    lastWiFiDiagnostic = millis();
    
    Serial.println("=== WiFi Diagnostics ===");
    Serial.printf("Status: %s (%d)\n", 
        WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected", 
        WiFi.status());
        
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
        Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("Subnet: %s\n", WiFi.subnetMask().toString().c_str());
        Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
        Serial.printf("Channel: %d\n", WiFi.channel());
        Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());
        
        // Signal quality assessment
        int rssi = WiFi.RSSI();
        String quality;
        if (rssi > -50) quality = "Excellent";
        else if (rssi > -60) quality = "Good";
        else if (rssi > -70) quality = "Fair";
        else if (rssi > -80) quality = "Poor";
        else quality = "Very Poor";
        
        Serial.printf("Signal Quality: %s\n", quality.c_str());
        Serial.printf("Uptime: %lu ms\n", millis());
        
        // Connection stability stats
        Serial.printf("Disconnects: %d\n", wifiDisconnectCount);
        Serial.printf("Reconnect attempts: %d\n", wifiReconnectAttempts);
        Serial.printf("Stable: %s\n", wifiStable ? "Yes" : "No");
    }
    Serial.println("========================");
}

/**
 * @brief Enhanced WiFi connection with better error handling
 */
bool connectToWiFiEnhanced() {
    if (wifiConfig.ssid.isEmpty()) {
        Serial.println("No WiFi configuration available");
        return false;
    }
    
    Serial.printf("Connecting to WiFi network: %s\n", wifiConfig.ssid.c_str());
    
    // Configure WiFi for better stability
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);  // Handle reconnection manually
    WiFi.setSleep(false);          // Disable WiFi sleep mode
    
    // Disconnect first to ensure clean state
    WiFi.disconnect(true);
    delay(100);
    
    // Begin connection
    WiFi.begin(wifiConfig.ssid.c_str(), wifiConfig.password.c_str());
    
    // Wait for connection with timeout
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && 
           millis() - startAttemptTime < WIFI_TIMEOUT) {
        Serial.print(".");
        digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));  // Toggle LED
        delay(500);
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nFailed to connect to WiFi");
        wifiReconnectAttempts++;
        return false;
    }
    
    Serial.println("\nWiFi connected successfully");
    Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Signal strength: %d dBm\n", WiFi.RSSI());
    
    wifiReconnectAttempts = 0;  // Reset counter on successful connection
    wifiStable = true;
    apMode = false;
    
    return true;
}

/**
 * @brief Initialize system hardware
 */
void initHardware() {
  // Initialize serial port for debugging
  Serial.begin(115200);
  Serial.println("\n\nWateringSystem v2.2 - Hardware-Managed Power");
  Serial.println("Architecture: LDO-powered domains with optical isolation");
  
  // Initialize status LED
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, HIGH);  // Turn on while initializing
  
  // Initialize buttons with pull-ups
  pinMode(PIN_BUTTON_MANUAL, INPUT_PULLUP);
  pinMode(PIN_BUTTON_CONFIG, INPUT_PULLUP);
  
  // Initialize reservoir level sensors with pull-ups
  pinMode(PIN_RESERVOIR_LOW_LEVEL, INPUT_PULLUP);
  pinMode(PIN_RESERVOIR_HIGH_LEVEL, INPUT_PULLUP);
  
  // Initialize I2C for BME280 (ESP32 domain)
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Serial.println("I2C initialized for BME280");
  
  // Initialize RS485 UART (hardware managed power)
  rs485Serial.begin(9600, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
  Serial.println("RS485 UART initialized (hardware-managed power)");
  
  // Hardware stabilization delay
  delay(500);
  
  Serial.println("Hardware initialization completed with hardware-managed power");
}

/**
 * @brief Load WiFi configuration from file
 * @return true if valid configuration loaded, false otherwise
 */
bool loadWiFiConfig() {
  if (!LittleFS.exists(CONFIG_FILE_PATH)) {
    Serial.println("No WiFi configuration file found");
    // Create default configuration file
    saveWiFiConfig(DEFAULT_SSID, "");
    return false;
  }
  
  File configFile = LittleFS.open(CONFIG_FILE_PATH, "r");
  if (!configFile) {
    Serial.println("Failed to open WiFi configuration file");
    return false;
  }
  
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();
  
  if (error) {
    Serial.printf("Failed to parse WiFi configuration file: %s\n", error.c_str());
    return false;
  }
  
  wifiConfig.ssid = doc["ssid"].as<String>();
  wifiConfig.password = doc["password"].as<String>();
  
  Serial.printf("Loaded WiFi configuration - SSID: %s\n", wifiConfig.ssid.c_str());
  
  // Check if this is the default configuration
  if (wifiConfig.ssid == DEFAULT_SSID) {
    Serial.println("Default WiFi configuration detected - AP mode required");
    return false;
  }
  
  return true;
}

/**
 * @brief Save WiFi configuration to file
 * @param ssid WiFi network name
 * @param password WiFi password
 * @return true if saved successfully, false otherwise
 */
bool saveWiFiConfig(const String &ssid, const String &password) {
  StaticJsonDocument<256> doc;
  
  doc["ssid"] = ssid;
  doc["password"] = password;
  
  File configFile = LittleFS.open(CONFIG_FILE_PATH, "w");
  if (!configFile) {
    Serial.println("Failed to open WiFi configuration file for writing");
    return false;
  }
  
  if (serializeJson(doc, configFile) == 0) {
    Serial.println("Failed to write WiFi configuration file");
    configFile.close();
    return false;
  }
  
  configFile.close();
  Serial.println("WiFi configuration saved successfully");
  
  // Update current configuration
  wifiConfig.ssid = ssid;
  wifiConfig.password = password;
  
  // Schedule a restart after a short delay
  // This allows the response to be sent back to the client
  Serial.println("Scheduling restart in 3 seconds...");
  delay(100);  // Short delay to ensure serial output is sent
  
  // Set global restart variables
  restartScheduled = true;
  restartTime = millis() + 3000;  // 3 second delay
  
  return true;
}

/**
 * @brief Start ESP32 in Access Point mode for configuration
 */
void startAccessPointMode() {
  Serial.println("Starting Access Point mode for configuration");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  Serial.printf("Access Point started - SSID: %s, IP: %s\n", 
                AP_SSID, WiFi.softAPIP().toString().c_str());
  
  apMode = true;
}

/**
 * @brief Connect to WiFi network using saved configuration (legacy wrapper)
 * @return true if connected successfully, false otherwise
 */
bool connectToWiFi() {
  return connectToWiFiEnhanced();
}

/**
 * @brief Synchronize the system time with NTP server
 */
void syncTimeWithNTP() {
  Serial.println("Synchronizing time with NTP server...");
  configTime(0, 0, NTP_SERVER);  // 0, 0 = GMT, no daylight offset
  
  // Wait for time to be set
  time_t now = 0;
  struct tm timeinfo = {};
  int retry = 0;
  const int maxRetry = 10;
  
  while (timeinfo.tm_year < (2020 - 1900) && ++retry < maxRetry) {
    Serial.printf("Waiting for NTP time sync... (%d/%d)\n", retry, maxRetry);
    delay(1000);
    time(&now);
    localtime_r(&now, &timeinfo);
  }
  
  if (timeinfo.tm_year < (2020 - 1900)) {
    Serial.println("Failed to sync time with NTP server");
    return;
  }
  
  Serial.printf("Current time: %d-%02d-%02d %02d:%02d:%02d\n",
                timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

/**
 * @brief Check and handle button presses
 */
void handleButtons() {
  // Debounce buttons (check every 50ms)
  if (millis() - lastButtonCheck < 50) {
    return;
  }
  lastButtonCheck = millis();
  
  // Check manual watering button
  bool manualState = digitalRead(PIN_BUTTON_MANUAL) == LOW;  // Active LOW with pull-up
  if (manualState && !manualButtonPressed) {
    Serial.println("Manual watering button pressed");
    
    // Toggle the pump
    if (plantPump.isRunning()) {
      controller.stopWatering();
      Serial.println("Manual watering stopped");
    } else {
      controller.manualWatering(20);  // Run for 20 seconds
      Serial.println("Manual watering started for 20 seconds");
    }
  }
  manualButtonPressed = manualState;
  
  // Check configuration button
  bool configState = digitalRead(PIN_BUTTON_CONFIG) == LOW;  // Active LOW with pull-up
  if (configState && !configButtonPressed) {
    Serial.println("Configuration button pressed");
    
    // Toggle automatic watering
    bool currentState = controller.isWateringEnabled();
    controller.enableWatering(!currentState);
    Serial.printf("Automatic watering %s\n", !currentState ? "enabled" : "disabled");
  }
  configButtonPressed = configState;
}

/**
 * @brief Print system status to Serial
 */
void updateStatus() {
  if (millis() - lastStatusUpdate < STATUS_CHECK_INTERVAL) {
    return;
  }
  lastStatusUpdate = millis();
  
  // Report sensor values
  if (envSensor.read()) {
    Serial.printf("Environment - Temp: %.1f°C, Humidity: %.1f%%, Pressure: %.1f hPa\n",
                  envSensor.getTemperature(),
                  envSensor.getHumidity(),
                  envSensor.getPressure());
  } else {
    Serial.printf("Environment sensor read failed, error: %d\n", envSensor.getLastError());
  }
  
  if (soilSensor.read()) {
    Serial.printf("Soil - Moisture: %.1f%%, Temp: %.1f°C, pH: %.1f, EC: %.0f µS/cm\n",
                  soilSensor.getMoisture(),
                  soilSensor.getTemperature(),
                  soilSensor.getPH(),
                  soilSensor.getEC());
    
    // Only display NPK if available
    float n = soilSensor.getNitrogen();
    float p = soilSensor.getPhosphorus();
    float k = soilSensor.getPotassium();
    
    if (n >= 0 && p >= 0 && k >= 0) {
      Serial.printf("Soil NPK - N: %.0f mg/kg, P: %.0f mg/kg, K: %.0f mg/kg\n", n, p, k);
    }
  } else {
    Serial.printf("Soil sensor read failed, error: %d\n", soilSensor.getLastError());
  }
  
  // Report pump status
  Serial.printf("Pump status: %s\n", plantPump.isRunning() ? 
                                     "Running" : "Stopped");
  
  // Report system status
  Serial.printf("Automatic watering: %s\n", controller.isWateringEnabled() ? 
                                           "Enabled" : "Disabled");
  
  // Report storage usage
  uint32_t totalSpace, usedSpace;
  if (dataStorage.getStorageStats(&totalSpace, &usedSpace)) {
    Serial.printf("Storage: %d KB used of %d KB (%.1f%%)\n",
                  usedSpace / 1024, totalSpace / 1024,
                  (usedSpace * 100.0) / totalSpace);
  }
  
  // Report network status if connected
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected - IP: %s, RSSI: %d dBm\n", 
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
  } else {
    Serial.println("WiFi disconnected");
  }
  
  Serial.println("--------------------------------------------");
}

/**
 * @brief Handle reservoir pump control based on water level sensors
 */
void handleReservoirPump() {
  // Skip if reservoir pump feature is disabled
  if (!reservoirPumpEnabled) {
    // Make sure pump is off if feature is disabled
    if (reservoirPump.isRunning()) {
      reservoirPump.stop();
      reservoirPumpRunning = false;
      Serial.println("Reservoir pump stopped (feature disabled)");
    }
    return;
  }
  
  // Read water level sensors (sensors are active LOW with pull-ups)
  reservoirLowLevel = digitalRead(PIN_RESERVOIR_LOW_LEVEL) == LOW;
  reservoirHighLevel = digitalRead(PIN_RESERVOIR_HIGH_LEVEL) == LOW;
  
  // If pump is running, check if it should be stopped
  if (reservoirPumpRunning) {
    bool shouldStop = false;
    
    // Stop if high level is reached
    if (reservoirHighLevel) {
      shouldStop = true;
      Serial.println("Reservoir pump stopped (high water level reached)");
    }
    
    // Safety timeout - stop if pump has been running too long
    if (millis() - reservoirPumpStartTime > RESERVOIR_PUMP_MAX_RUNTIME) {
      shouldStop = true;
      Serial.println("Reservoir pump stopped (safety timeout)");
    }
    
    if (shouldStop) {
      reservoirPump.stop();
      reservoirPumpRunning = false;
    }
  }
  // If pump is not running, check if it should be started
  else if (reservoirLowLevel && !reservoirHighLevel) {
    // Start pump if level is low and not already high
    reservoirPump.start();
    reservoirPumpRunning = true;
    reservoirPumpStartTime = millis();
    Serial.println("Reservoir pump started (low water level detected)");
  }
}

/**
 * @brief Start manual reservoir filling
 * @param seconds Duration in seconds to run the pump, 0 for indefinite (until high level reached)
 * @return true if operation started successfully, false otherwise
 */
bool startManualReservoirFilling(uint16_t seconds) {
  // Only allow if feature is enabled
  if (!reservoirPumpEnabled) {
    Serial.println("Cannot start manual reservoir filling (feature disabled)");
    return false;
  }
  
  // Check if high level is already reached
  reservoirHighLevel = digitalRead(PIN_RESERVOIR_HIGH_LEVEL) == LOW;
  if (reservoirHighLevel) {
    Serial.println("Cannot start manual reservoir filling (reservoir already full)");
    return false;
  }
  
  // Start the pump
  reservoirPump.start();
  reservoirPumpRunning = true;
  reservoirPumpStartTime = millis();
  
  // If a duration was specified, set up automatic stop
  if (seconds > 0) {
    reservoirPumpStartTime = millis() - (RESERVOIR_PUMP_MAX_RUNTIME - (seconds * 1000));
    Serial.printf("Reservoir pump started manually for %d seconds\n", seconds);
  } else {
    Serial.println("Reservoir pump started manually (will run until high level reached)");
  }
  
  return true;
}

/**
 * @brief Stop the reservoir pump
 */
void stopReservoirPump() {
  if (reservoirPumpRunning) {
    reservoirPump.stop();
    reservoirPumpRunning = false;
    Serial.println("Reservoir pump stopped manually");
  }
}

/**
 * @brief Enable or disable the reservoir pump feature
 * @param enabled true to enable, false to disable
 */
void enableReservoirPump(bool enabled) {
  reservoirPumpEnabled = enabled;
  
  // If disabling, make sure pump is stopped
  if (!enabled && reservoirPumpRunning) {
    reservoirPump.stop();
    reservoirPumpRunning = false;
    Serial.println("Reservoir pump stopped (feature disabled)");
  }
  
  Serial.printf("Reservoir pump feature %s\n", enabled ? "enabled" : "disabled");
}

/**
 * @brief Check if reservoir pump feature is enabled
 * @return true if enabled, false otherwise
 */
bool isReservoirPumpEnabled() {
  return reservoirPumpEnabled;
}

/**
 * @brief Get reservoir status information
 * @param isLow Pointer to store low level status
 * @param isHigh Pointer to store high level status
 * @param isRunning Pointer to store pump running status
 * @return true if status was successfully retrieved, false otherwise
 */
bool getReservoirStatus(bool* isLow, bool* isHigh, bool* isRunning) {
  // Read sensors directly for latest status
  if (isLow != nullptr) {
    *isLow = digitalRead(PIN_RESERVOIR_LOW_LEVEL) == LOW;
  }
  
  if (isHigh != nullptr) {
    *isHigh = digitalRead(PIN_RESERVOIR_HIGH_LEVEL) == LOW;
  }
  
  if (isRunning != nullptr) {
    *isRunning = reservoirPumpRunning;
  }
  
  // Return true to indicate success
  return true;
}

/**
 * @brief Initialize the file system
 * @return true if initialized successfully, false otherwise
 */
bool initFileSystem() {
  Serial.println("Initializing LittleFS file system...");
  
  if (!LittleFS.begin(true)) { // Use format_if_failed=true to handle corruption
    Serial.println("Failed to mount LittleFS file system!");
    Serial.println("This could be due to:");
    Serial.println("1. Corrupted file system");
    Serial.println("2. Hardware failure");
    Serial.println("3. Incompatible partition table");
    return false;
  }
  
  Serial.println("LittleFS file system initialized successfully");
  
  // Print out file system info
  size_t totalBytes = LittleFS.totalBytes();
  size_t usedBytes = LittleFS.usedBytes();
  size_t freeBytes = totalBytes - usedBytes;
  Serial.printf("LittleFS: %u bytes total, %u bytes used, %u bytes free (%0.1f%% used)\n", 
                totalBytes, usedBytes, freeBytes, (usedBytes * 100.0) / totalBytes);
  
  // Warn if filesystem is very full
  if (freeBytes < 10240) { // Less than 10KB free
    Serial.println("WARNING: LittleFS is very full! Consider cleaning up files or using a larger partition.");
  }
  
  // Debug: list files
  Serial.println("Files in LittleFS root:");
  File root = LittleFS.open("/");
  if (root && root.isDirectory()) {
    File file = root.openNextFile();
    while (file) {
      char fileTime[20];
      time_t t = file.getLastWrite();
      struct tm* tm = localtime(&t);
      strftime(fileTime, sizeof(fileTime), "%Y-%m-%d %H:%M:%S", tm);
      
      Serial.printf("  %s  %8d bytes  %s%s\n", 
                    fileTime, 
                    file.size(), 
                    file.name(),
                    file.isDirectory() ? "/" : "");
      file = root.openNextFile();
    }
  }
  
  return true;
}

/**
 * @brief Arduino setup function
 */
void setup() {
  // Initialize the hardware
  initHardware();
  
  // Check for emergency WiFi reset (config button held during startup)
  checkEmergencyWiFiReset();
  
  // Initialize file system and data storage
  if (!initFileSystem()) {
    Serial.println("Error initializing file system");
  }
  
  if (!dataStorage.initialize()) {
    Serial.println("Error initializing data storage");
  }
  
  // Initialize controller with all components
  if (controller.initialize()) {
    Serial.println("WateringController initialized successfully");
  } else {
    Serial.printf("WateringController initialization failed, error: %d\n", 
                  controller.getLastError());
  }
  
  // Load WiFi configuration and determine operation mode
  bool validWiFiConfig = loadWiFiConfig();
  
  // Set WiFi configuration save callback in WebServer
  webServer.setWiFiConfigCallback(saveWiFiConfig);
  
  // Tell WebServer if we're in AP mode
  webServer.enableApMode(!validWiFiConfig);
  
  // Connect to WiFi or start AP mode
  if (validWiFiConfig && connectToWiFi()) {
    // Sync time with NTP once we have network connection
    syncTimeWithNTP();
  } else {
    // Start in AP mode for configuration
    startAccessPointMode();
    webServer.enableApMode(true);
  }
  
  // Initialize web server
  if (webServer.initialize()) {
    Serial.println("Web server initialized successfully");
    
    // Register reservoir pump callbacks
    webServer.setReservoirPumpEnableCallback(enableReservoirPump);
    webServer.setReservoirPumpStatusCallback(getReservoirStatus);
    webServer.setReservoirPumpManualFillCallback(startManualReservoirFilling);
    webServer.setReservoirPumpStopCallback(stopReservoirPump);
    webServer.setReservoirPumpEnabledCheckCallback(isReservoirPumpEnabled);
    
    webServer.start();
  } else {
    Serial.printf("Web server initialization failed, error: %d\n", 
                webServer.getLastError());
  }
  
  systemReady = true;
  
  // Turn off status LED once system is ready
  digitalWrite(PIN_STATUS_LED, LOW);
  
  Serial.println("System initialization complete");
  Serial.println("--------------------------------------------");
  
  // Display connection information
  if (apMode) {
    Serial.println("System in AP mode for WiFi configuration");
    Serial.printf("SSID: %s\n", AP_SSID);
    Serial.printf("IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.println("Connect to this network and navigate to the above IP address to configure WiFi");
  } else {
    Serial.printf("Connected to WiFi: %s\n", wifiConfig.ssid.c_str());
    Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
  }
}

/**
 * @brief Arduino loop function
 */
void loop() {
  // Check if restart has been scheduled
  if (restartScheduled && millis() > restartTime) {
    Serial.println("Restarting system now...");
    delay(500);  // Allow cleanup to complete
    ESP.restart();
  }
  
  // Update the controller
  controller.update();
  
  // Handle user input (buttons)
  handleButtons();
  
  // Update status display periodically
  updateStatus();
  
  // Handle reservoir pump control
  handleReservoirPump();
  
  // LED status indication
  if (apMode) {
    // Fast blinking in AP mode
    digitalWrite(PIN_STATUS_LED, ((millis() / 200) % 2) ? HIGH : LOW);
  } else if (plantPump.isRunning()) {
    // Medium blinking when pump is running
    digitalWrite(PIN_STATUS_LED, ((millis() / 500) % 2) ? HIGH : LOW);
  } else {
    // Just a short blink every 3 seconds to show system is running
    digitalWrite(PIN_STATUS_LED, ((millis() % 3000) < 100) ? HIGH : LOW);  }
  
  // Monitor and maintain WiFi connection
  monitorWiFiConnection();
  
  // Check watchdog
  checkWatchdog();
  
  // Allow other tasks to run
  yield();
}

/**
 * @brief Reset WiFi settings and restart in AP mode
 */
void resetWiFiSettings() {
    Serial.println("Resetting WiFi settings and restarting in AP mode...");
    
    // Stop current connections
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    // Remove WiFi config file
    if (LittleFS.exists(CONFIG_FILE_PATH)) {
        LittleFS.remove(CONFIG_FILE_PATH);
        Serial.println("WiFi configuration file removed");
    }
    
    // Create default configuration
    saveWiFiConfig(DEFAULT_SSID, "");
    
    Serial.println("System will restart in AP mode for reconfiguration");
    delay(2000);
    ESP.restart();
}

/**
 * @brief Emergency WiFi recovery - force AP mode if button held during startup
 */
void checkEmergencyWiFiReset() {
    // Check if config button is held down during startup
    int configButtonHoldTime = 0;
    while (digitalRead(PIN_BUTTON_CONFIG) == LOW && configButtonHoldTime < 50) {
        delay(100);
        configButtonHoldTime++;
        digitalWrite(PIN_STATUS_LED, configButtonHoldTime % 2);  // Blink LED
    }
    
    if (configButtonHoldTime >= 50) {  // Button held for 5+ seconds
        Serial.println("Emergency WiFi reset triggered!");
        resetWiFiSettings();
    }
}

/**
 * @brief Simple software watchdog to prevent system hangs
 */
void feedWatchdog() {
    lastLoopTime = millis();
}

/**
 * @brief Check if system is hanging and restart if needed
 */
void checkWatchdog() {
    if (!watchdogEnabled) return;
    
    if (millis() - lastLoopTime > loopWatchdogTimeout) {
        Serial.println("WATCHDOG: System appears to be hanging - restarting...");
        delay(1000);
        ESP.restart();
    }
}

/**
 * @brief Monitor and maintain WiFi connection
 */
void monitorWiFiConnection() {
    if (apMode) return;  // Skip monitoring in AP mode
    
    if (millis() - lastWiFiCheck < WIFI_CHECK_INTERVAL) {
        return;
    }
    lastWiFiCheck = millis();
    
    // Print diagnostics periodically
    printWiFiDiagnostics();
    
    // Check connection status
    if (WiFi.status() != WL_CONNECTED) {
        if (wifiStable) {
            Serial.println("WiFi connection lost!");
            wifiDisconnectCount++;
            wifiStable = false;
        }
        
        // Attempt reconnection with exponential backoff
        if (millis() - lastWiFiReconnect > WIFI_RECONNECT_INTERVAL) {
            lastWiFiReconnect = millis();
            
            Serial.printf("Attempting WiFi reconnection (%d/%d)...\n", 
                         wifiReconnectAttempts + 1, MAX_RECONNECT_ATTEMPTS);
            
            if (connectToWiFiEnhanced()) {
                Serial.println("WiFi reconnected successfully");
            } else if (wifiReconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
                Serial.println("Max reconnect attempts reached, waiting longer...");
                lastWiFiReconnect = millis() + 60000;  // Wait 1 minute before trying again
                wifiReconnectAttempts = 0;  // Reset counter
            }
        }
    } else {
        // Connection is good, check signal quality
        int rssi = WiFi.RSSI();
        if (rssi < -80 && wifiStable) {
            Serial.printf("WARNING: Weak WiFi signal (%d dBm)\n", rssi);
        }
        
        if (!wifiStable) {
            Serial.println("WiFi connection restored and stable");
            wifiStable = true;
        }
    }
}