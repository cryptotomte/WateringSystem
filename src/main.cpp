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
#include "communication/WateringSystemWebServer.h" // This includes our WateringSystemWebServer class

// Pin definitions based on hardware.md specification
#define PIN_I2C_SDA           21
#define PIN_I2C_SCL           22
#define PIN_RS485_TX          16
#define PIN_RS485_RX          17
#define PIN_RS485_DE          25
#define PIN_MAIN_PUMP_CONTROL 26
#define PIN_RESERVOIR_PUMP_CONTROL 27  // Added new pin for reservoir pump
#define PIN_STATUS_LED        2       // Changed to a new pin since PIN_RESERVOIR_PUMP_CONTROL now uses pin 27
#define PIN_BUTTON_MANUAL     4
#define PIN_BUTTON_CONFIG     5

// Other constants
#define SOIL_SENSOR_MODBUS_ADDR 0x01
#define BME280_I2C_ADDR       0x76
#define WIFI_TIMEOUT          60000  // 60 seconds timeout for WiFi connection
#define NTP_SERVER            "pool.ntp.org"
#define STATUS_CHECK_INTERVAL 5000   // 5 seconds between status checks/display
#define WEB_SERVER_PORT       80
#define CONFIG_FILE_PATH      "/wifi_config.json"
#define AP_SSID               "WateringSystem-Setup"
#define AP_PASSWORD           "watering123"
#define DEFAULT_SSID          "CONFIGURE_ME"

// Global component instances
BME280Sensor envSensor(BME280_I2C_ADDR, "BME280");
HardwareSerial rs485Serial(2);  // Using UART2 for RS485
SP3485ModbusClient modbusClient(&rs485Serial, PIN_RS485_DE);
ModbusSoilSensor soilSensor(&modbusClient, SOIL_SENSOR_MODBUS_ADDR, "SoilSensor");
WaterPump plantPump(PIN_MAIN_PUMP_CONTROL, "PlantPump");         // Renamed for clarity
WaterPump reservoirPump(PIN_RESERVOIR_PUMP_CONTROL, "ReservoirPump"); // Added new pump for filling reservoir
LittleFSStorage dataStorage;
WateringController controller(&envSensor, &soilSensor, &plantPump, &dataStorage);
WateringSystemWebServer webServer(&controller, &envSensor, &soilSensor, &plantPump, &dataStorage, WEB_SERVER_PORT);

// System state
unsigned long lastStatusUpdate = 0;
unsigned long lastButtonCheck = 0;
bool manualButtonPressed = false;
bool configButtonPressed = false;
bool systemReady = false;
bool apMode = false;

// Restart state (moved to global scope to fix the build error)
bool restartScheduled = false;
unsigned long restartTime = 0;

// WiFi configuration
struct WiFiConfig {
    String ssid;
    String password;
};

WiFiConfig wifiConfig;

// Forward declarations of functions
bool saveWiFiConfig(const String &ssid, const String &password);

/**
 * @brief Initialize system hardware
 */
void initHardware() {
  // Initialize serial port for debugging
  Serial.begin(115200);
  Serial.println("\n\nWateringSystem v1.0 starting...");
  
  // Initialize status LED
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, HIGH);  // Turn on while initializing
  
  // Initialize buttons with pull-ups
  pinMode(PIN_BUTTON_MANUAL, INPUT_PULLUP);
  pinMode(PIN_BUTTON_CONFIG, INPUT_PULLUP);
  
  // Initialize I2C for BME280
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  
  // Initialize RS485 UART
  rs485Serial.begin(9600, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
  
  // Allow hardware to stabilize
  delay(100);
  
  Serial.println("Hardware initialized");
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
 * @brief Connect to WiFi network using saved configuration
 * @return true if connected successfully, false otherwise
 */
bool connectToWiFi() {
  if (wifiConfig.ssid.isEmpty()) {
    Serial.println("No WiFi configuration available");
    return false;
  }
  
  Serial.printf("Connecting to WiFi network: %s\n", wifiConfig.ssid.c_str());
  
  // Set WiFi mode and begin connection
  WiFi.mode(WIFI_STA);
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
    return false;
  }
  
  Serial.println("\nWiFi connected");
  Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
  apMode = false;
  return true;
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
 * @brief Arduino setup function
 */
void setup() {
  // Initialize the hardware
  initHardware();
  
  // Initialize file system and data storage
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
    delay(100);  // Short delay to ensure serial output is sent
    ESP.restart();
  }
  
  // Update the controller
  controller.update();
  
  // Handle user input (buttons)
  handleButtons();
  
  // Update status display periodically
  updateStatus();
  
  // LED status indication
  if (apMode) {
    // Fast blinking in AP mode
    digitalWrite(PIN_STATUS_LED, ((millis() / 200) % 2) ? HIGH : LOW);
  } else if (plantPump.isRunning()) {
    // Medium blinking when pump is running
    digitalWrite(PIN_STATUS_LED, ((millis() / 500) % 2) ? HIGH : LOW);
  } else {
    // Just a short blink every 3 seconds to show system is running
    digitalWrite(PIN_STATUS_LED, ((millis() % 3000) < 100) ? HIGH : LOW);
  }
  
  // Reconnect WiFi if needed and not in AP mode
  if (!apMode && WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt > 30000) {  // Try every 30 seconds
      lastReconnectAttempt = millis();
      connectToWiFi();
    }
  }
  
  // Allow other tasks to run
  yield();
}