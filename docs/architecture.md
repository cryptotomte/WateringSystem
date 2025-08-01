# WateringSystem Architecture

## Overview

This document describes the architectural design of the WateringSystem project. The architecture follows a modular, layered approach with clear separation of concerns and interface-based design principles.

## System Architecture

The WateringSystem follows a layered architecture pattern to ensure modularity, testability, and maintainability. The system is divided into the following layers:

```
┌─────────────────────────────────────────────┐
│               User Interface                │
│         (Web Server, HTML/CSS/JS)           │
└───────────────────────┬─────────────────────┘
                        │
┌───────────────────────▼─────────────────────┐
│             Application Logic                │
│      (Watering Rules, Scheduling, etc.)     │
└───────────────────────┬─────────────────────┘
                        │
┌───────────────────────▼─────────────────────┐
│           Service Layer                      │
│  (Sensor Data Processing, Data Storage)      │
└───────────────────────┬─────────────────────┘
                        │
┌───────────────────────▼─────────────────────┐
│        Hardware Abstraction Layer            │
│    (Sensor & Actuator Interface Classes)     │
└───────────────────────┬─────────────────────┘
                        │
┌───────────────────────▼─────────────────────┐
│             Hardware Drivers                 │
│     (BME280, RS485, Soil Sensor, Pump)      │
└─────────────────────────────────────────────┘
```

### Layers Description

1. **User Interface Layer**
   - Provides web-based interface for monitoring and control
   - Implements a lightweight web server
   - Serves HTML, CSS, and JavaScript content from LittleFS
   - Handles user authentication and authorization

2. **Application Logic Layer**
   - Implements watering rules and decision logic
   - Manages scheduling and timing of watering events
   - Handles alerts and notifications for out-of-range conditions
   - Processes user configuration changes

3. **Service Layer**
   - Processes raw sensor data into meaningful values
   - Manages data persistence (configuration, logs, history)
   - Implements Modbus RTU protocol for soil sensor communication
   - Provides data access services to upper layers

4. **Hardware Abstraction Layer**
   - Defines interfaces for all hardware components
   - Provides concrete implementations for each hardware device
   - Abstracts hardware details from upper layers
   - Enables mock implementations for testing

5. **Hardware Drivers Layer**
   - Implements low-level communication with hardware
   - Handles I2C, RS485, and GPIO communications
   - Manages power control and timing requirements
   - Reports hardware errors to upper layers

## Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         WateringSystem                          │
└───────────────────────────────┬─────────────────────────────────┘
                                │
        ┌─────────────────────────────────────────────┐
        │                                             │
┌───────▼────────┐    ┌────────────────┐    ┌─────────▼─────────┐
│    WebServer   │    │ ConfigManager  │    │   SensorManager   │
└───────┬────────┘    └────────┬───────┘    └─────────┬─────────┘
        │                      │                      │
        │             ┌────────▼───────┐     ┌────────▼────────┐
        │             │ DataRepository │     │  SensorFactory  │
        │             └────────────────┘     └────────┬────────┘
        │                                             │
┌───────▼────────┐                           ┌────────▼────────┐
│   HTTPHandler  │                           │   ISensor       │◄────┐
└────────────────┘                           └────────┬────────┘     │
                                                      │              │
                          ┌─────────────────┬─────────┼──────────┐   │
                          │                 │         │          │   │
                 ┌────────▼───────┐ ┌───────▼────┐ ┌─▼────────┐ │   │
                 │ BME280Sensor   │ │ SoilSensor │ │ MockSensor│ │   │
                 └────────────────┘ └───────┬────┘ └───────────┘ │   │
                                            │                    │   │
                                    ┌───────▼────────┐           │   │
                                    │ ModbusClient   │           │   │
                                    └────────────────┘           │   │
                                                                 │   │
┌────────────────┐                                   ┌───────────▼─┐ │
│ WateringRules  │◄──────────────────────────────────┤ SensorReading│ │
└───────┬────────┘                                   └───────────┬─┘ │
        │                                                        │   │
┌───────▼────────┐                                               │   │
│ PumpController │◄──────────────────────────────────────────────┘   │
└───────┬────────┘                                                    │
        │                                                             │
┌───────▼────────┐     ┌────────────────┐                            │
│   IActuator    │◄────┤  MockActuator  │────────────────────────────┘
└───────┬────────┘     └────────────────┘
        │
┌───────▼────────┐
│    WaterPump   │
└────────────────┘
```

## Key Interfaces

The architecture is built around a set of key interfaces that enable modularity and testability:

### Sensor Interfaces

```cpp
/**
 * Base interface for all sensors
 */
class ISensor {
public:
    virtual ~ISensor() = default;
    
    /**
     * Initialize the sensor
     * @return true if initialization successful, false otherwise
     */
    virtual bool initialize() = 0;
    
    /**
     * Read sensor data
     * @return true if reading successful, false otherwise
     */
    virtual bool read() = 0;
    
    /**
     * Check if sensor is available and working
     * @return true if sensor is available, false otherwise
     */
    virtual bool isAvailable() = 0;
    
    /**
     * Get last error code
     * @return error code, 0 if no error
     */
    virtual int getLastError() = 0;
};

/**
 * Interface for environmental sensors
 */
class IEnvironmentalSensor : public ISensor {
public:
    /**
     * Get temperature in Celsius
     * @return temperature value
     */
    virtual float getTemperature() = 0;
    
    /**
     * Get relative humidity in percent
     * @return humidity value
     */
    virtual float getHumidity() = 0;
};

/**
 * Interface for soil sensors
 */
class ISoilSensor : public ISensor {
public:
    /**
     * Get soil moisture in percent
     * @return moisture value
     */
    virtual float getMoisture() = 0;
    
    /**
     * Get soil temperature in Celsius
     * @return temperature value
     */
    virtual float getTemperature() = 0;
    
    /**
     * Get soil pH level
     * @return pH value
     */
    virtual float getPH() = 0;
    
    /**
     * Get electrical conductivity in µS/cm
     * @return EC value
     */
    virtual float getEC() = 0;
    
    /**
     * Get nitrogen level
     * @return nitrogen value
     */
    virtual float getNitrogen() = 0;
    
    /**
     * Get phosphorus level
     * @return phosphorus value
     */
    virtual float getPhosphorus() = 0;
    
    /**
     * Get potassium level
     * @return potassium value
     */
    virtual float getPotassium() = 0;
};
```

### Actuator Interfaces

```cpp
/**
 * Base interface for all actuators
 */
class IActuator {
public:
    virtual ~IActuator() = default;
    
    /**
     * Initialize the actuator
     * @return true if initialization successful, false otherwise
     */
    virtual bool initialize() = 0;
    
    /**
     * Check if actuator is available and working
     * @return true if actuator is available, false otherwise
     */
    virtual bool isAvailable() = 0;
    
    /**
     * Get last error code
     * @return error code, 0 if no error
     */
    virtual int getLastError() = 0;
};

/**
 * Interface for water pump control
 */
class IWaterPump : public IActuator {
public:
    /**
     * Start the pump
     * @return true if operation successful, false otherwise
     */
    virtual bool start() = 0;
    
    /**
     * Stop the pump
     * @return true if operation successful, false otherwise
     */
    virtual bool stop() = 0;
    
    /**
     * Run the pump for a specific duration
     * @param seconds Duration to run in seconds
     * @return true if operation successful, false otherwise
     */
    virtual bool runFor(unsigned int seconds) = 0;
    
    /**
     * Check if the pump is currently running
     * @return true if running, false otherwise
     */
    virtual bool isRunning() = 0;
};
```

### Communication Interfaces

```cpp
/**
 * Interface for Modbus communication
 * 
 * Implementation Note (July 2025): SP3485ModbusClient provides production-ready
 * RS485 communication using TXS0108E level shifter with verified 50µs timing delays.
 * Supports full Modbus RTU protocol with error handling and automatic retry logic.
 */
class IModbusClient {
public:
    virtual ~IModbusClient() = default;
    
    /**
     * Initialize the Modbus client
     * @return true if initialization successful, false otherwise
     */
    virtual bool initialize() = 0;
    
    /**
     * Read holding registers from a Modbus device
     * @param deviceAddress Modbus device address
     * @param startRegister First register to read
     * @param count Number of registers to read
     * @param buffer Buffer to store read values
     * @return true if operation successful, false otherwise
     */
    virtual bool readHoldingRegisters(uint8_t deviceAddress, uint16_t startRegister, 
                                      uint16_t count, uint16_t* buffer) = 0;
    
    /**
     * Write single register to a Modbus device
     * @param deviceAddress Modbus device address
     * @param registerAddress Register address to write
     * @param value Value to write
     * @return true if operation successful, false otherwise
     */
    virtual bool writeSingleRegister(uint8_t deviceAddress, uint16_t registerAddress, 
                                    uint16_t value) = 0;
    
    /**
     * Get last error code
     * @return error code, 0 if no error
     */
    virtual int getLastError() = 0;
};
```

### Storage Interfaces

```cpp
/**
 * Interface for data storage
 */
class IDataStorage {
public:
    virtual ~IDataStorage() = default;
    
    /**
     * Initialize the storage
     * @return true if initialization successful, false otherwise
     */
    virtual bool initialize() = 0;
    
    /**
     * Store configuration data
     * @param key Configuration key
     * @param data Data to store
     * @return true if operation successful, false otherwise
     */
    virtual bool storeConfig(const String& key, const String& data) = 0;
    
    /**
     * Retrieve configuration data
     * @param key Configuration key
     * @param defaultValue Default value if key not found
     * @return Retrieved data or default value
     */
    virtual String getConfig(const String& key, const String& defaultValue = "") = 0;
    
    /**
     * Store sensor reading
     * @param sensorId ID of the sensor
     * @param readingType Type of reading (temperature, humidity, etc.)
     * @param value Reading value
     * @param timestamp Time of reading
     * @return true if operation successful, false otherwise
     */
    virtual bool storeSensorReading(const String& sensorId, const String& readingType, 
                                  float value, time_t timestamp) = 0;
    
    /**
     * Get sensor readings for a specific period
     * @param sensorId ID of the sensor
     * @param readingType Type of reading
     * @param startTime Start time of the period
     * @param endTime End time of the period
     * @return JSON string with readings
     */
    virtual String getSensorReadings(const String& sensorId, const String& readingType, 
                                   time_t startTime, time_t endTime) = 0;
};
```

## Data Flow

1. **Sensor Data Collection**
   - Sensors are read at configured intervals
   - Raw data is processed and converted to appropriate units
   - Processed data is stored in temporary storage
   - Readings are checked against configured thresholds

2. **Decision Making**
   - Soil moisture and other parameters are evaluated
   - If watering criteria are met, watering process is triggered
   - Historical data is considered for decision making
   - Scheduling rules are applied

3. **Actuation**
   - Water pump is activated for a calculated duration
   - Pump activity is monitored and logged
   - Safety checks are performed during operation

4. **User Interaction**
   - Web interface displays current status and historical data
   - User configures system parameters and schedules
   - System responds to manual commands
   - Alerts are displayed for out-of-range conditions

## Testing Approach

The architecture supports comprehensive testing at multiple levels:

1. **Unit Testing**
   - Each class is tested in isolation
   - Mock implementations of interfaces are used
   - Focus on individual component behavior

2. **Integration Testing**
   - Tests interaction between components
   - Verifies proper communication between layers
   - Focuses on interface compliance

3. **System Testing**
   - Tests the complete system behavior
   - Verifies end-to-end functionality
   - Simulates real-world scenarios

## Deployment

The system is deployed as firmware on the ESP32 microcontroller:

1. **Build Process**
   - PlatformIO builds the firmware
   - Web assets are processed and included in the binary
   - Configuration is embedded in the firmware

2. **Installation**
   - Firmware is flashed to the ESP32
   - Initial configuration is performed
   - System performs self-test and calibration

3. **Updates**
   - OTA (Over The Air) updates are supported
   - Configuration is preserved during updates
   - Version control ensures compatibility

## Future Extensions

The architecture is designed to support future extensions:

1. **Additional Sensors**
   - New sensor types can be added through interface implementation
   - Minimal impact on existing code

2. **Advanced Control Algorithms**
   - More sophisticated watering algorithms can be implemented
   - Machine learning integration for predictive watering

3. **External Integrations**
   - MQTT support for IoT integration
   - REST API for external system integration

4. **Mobile Application**
   - Companion mobile app could be developed
   - Would communicate with system via web API

## Hardware Architecture - LDO-Managed System

The WateringSystem implements a hardware-managed power architecture optimized for greenhouse automation applications. The system uses LDO voltage regulators with common ground and optical signal isolation, providing reliable operation without software power control complexity.

### Hardware-Managed Power Overview

The system uses an always-on LDO architecture with optical signal isolation:

```
┌─────────────────────────────────────────────────────────────────┐
│                 ESP32 Domain (3.3V LDO)                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐             │
│  │    ESP32    │  │   BME280    │  │  Status     │             │
│  │  DevKit v1  │  │   Sensor    │  │  LEDs       │             │
│  └─────────────┘  └─────────────┘  └─────────────┘             │
│                         │                                       │
│                         │ I2C (Direct connection)               │
│                         │                                       │
│  Reference: GND_COMMON                                          │
└───────────────────┬─────────────────────────────────────────────┘                    │ Level Shifting
                    │ (TXS0108E)
                    │ 3.3V ↔ 5V
┌───────────────────▼─────────────────────────────────────────────┐
│                 FIELD Domain (5V LDO Always-On)                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                SP3485EN IC                              │   │
│  │           RS485 Transceiver                             │   │
│  │          (Always powered)                               │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐             │
│  │    Soil     │  │   Water     │  │  Reservoir  │             │
│  │   Sensors   │  │   Pumps     │  │   Sensors   │             │
│  │  (RS485)    │  │  (Relays)   │  │  (Digital)  │             │
│  └─────────────┘  └─────────────┘  └─────────────┘             │
│                                                                 │
│  Reference: GND_COMMON (Shared ground)                         │
└─────────────────────────────────────────────────────────────────┘
```

### Hardware Management Strategy

#### LDO Power Management: Always-On Design
- **Type**: AMS1117 LDO voltage regulators
- **ESP32 Domain**: 3.3V always-on power supply
- **Field Domain**: 5V always-on power supply
- **Ground**: Common ground plane (GND_COMMON)
- **Control**: Hardware-only, no software power management

#### TXS0108E Level Shifter: Fast Voltage Translation
- **Type**: 8-bit bidirectional voltage translator
- **Data Rate**: Up to 110 Mbps capability
- **Purpose**: Signal isolation between domains (signals only, not power)
- **Signals Isolated**: TX, RX, DE/RE control
- **Cost**: ~60 SEK total (3 units × 20 SEK)

### Hardware Benefits

1. **Simplicity**: No software power control eliminates complexity and failure modes
2. **Reliability**: Always-on design eliminates startup delays and power sequencing issues
3. **Stability**: LDO regulators provide excellent voltage regulation and low noise
4. **Cost Effectiveness**: Eliminates need for power control GPIO pins and monitoring circuits
5. **Common Ground**: Shared ground plane simplifies PCB design and eliminates ground loops

### Power Supply Architecture

```
12V Battery ──┬── AMS1117-3.3 LDO ──► ESP32 Domain (3.3V Always-On)
              │   (GND_COMMON)        Control logic, WiFi, BME280
              │
              └── AMS1117-5.0 LDO ──► FIELD Domain (5V Always-On)
                  (GND_COMMON)        RS485, sensors, pumps
```
