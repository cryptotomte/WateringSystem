/**
 * @file main.cpp
 * @brief Main application for the WateringSystem
 * @author WateringSystem Team
 * @date 2025-04-15
 */

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Wire.h>

// Include our components
#include "WateringController.h"
#include "sensors/BME280Sensor.h"
#include "sensors/ModbusSoilSensor.h"
#include "communication/SP3485ModbusClient.h"
#include "actuators/WaterPump.h"
#include "storage/LittleFSStorage.h"

// Pin definitions based on hardware.md specification
#define PIN_I2C_SDA           21
#define PIN_I2C_SCL           22
#define PIN_RS485_TX          16
#define PIN_RS485_RX          17
#define PIN_RS485_DE          25
#define PIN_PUMP_CONTROL      26
#define PIN_STATUS_LED        27
#define PIN_BUTTON_MANUAL     4
#define PIN_BUTTON_CONFIG     5

// Other constants
#define SOIL_SENSOR_MODBUS_ADDR 0x01
#define BME280_I2C_ADDR       0x76
#define WIFI_TIMEOUT          60000  // 60 seconds timeout for WiFi connection
#define NTP_SERVER            "pool.ntp.org"
#define STATUS_CHECK_INTERVAL 5000   // 5 seconds between status checks/display

// Global component instances
BME280Sensor envSensor(BME280_I2C_ADDR, "BME280");
HardwareSerial rs485Serial(2);  // Using UART2 for RS485
SP3485ModbusClient modbusClient(&rs485Serial, PIN_RS485_DE);
ModbusSoilSensor soilSensor(&modbusClient, SOIL_SENSOR_MODBUS_ADDR, "SoilSensor");
WaterPump waterPump(PIN_PUMP_CONTROL, "MainPump");
LittleFSStorage dataStorage;
WateringController controller(&envSensor, &soilSensor, &waterPump, &dataStorage);

// System state
unsigned long lastStatusUpdate = 0;
unsigned long lastButtonCheck = 0;
bool manualButtonPressed = false;
bool configButtonPressed = false;
bool systemReady = false;

// WiFi credentials - in production, load from secure storage
const char* ssid = "YourNetworkName";
const char* password = "YourPassword";

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
 * @brief Connect to WiFi network
 * @return true if connected successfully, false otherwise
 */
bool connectToWiFi() {
  Serial.printf("Connecting to WiFi network: %s\n", ssid);
  
  // Set WiFi mode and begin connection
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
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
    if (waterPump.isRunning()) {
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
  Serial.printf("Pump status: %s\n", waterPump.isRunning() ? 
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
  
  // Connect to WiFi (optional, system can work without it)
  if (connectToWiFi()) {
    // Sync time with NTP once we have network connection
    syncTimeWithNTP();
  }
  
  systemReady = true;
  
  // Turn off status LED once system is ready
  digitalWrite(PIN_STATUS_LED, LOW);
  
  Serial.println("System initialization complete");
  Serial.println("--------------------------------------------");
}

/**
 * @brief Arduino loop function
 */
void loop() {
  // Update the controller
  controller.update();
  
  // Handle user input (buttons)
  handleButtons();
  
  // Update status display periodically
  updateStatus();
  
  // If pump is running, blink status LED
  if (waterPump.isRunning()) {
    digitalWrite(PIN_STATUS_LED, ((millis() / 500) % 2) ? HIGH : LOW);
  } else {
    // Just a short blink every 3 seconds to show system is running
    digitalWrite(PIN_STATUS_LED, ((millis() % 3000) < 100) ? HIGH : LOW);
  }
  
  // Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt > 30000) {  // Try every 30 seconds
      lastReconnectAttempt = millis();
      connectToWiFi();
    }
  }
  
  // Allow other tasks to run
  yield();
}