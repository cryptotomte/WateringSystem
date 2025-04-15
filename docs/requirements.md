# System Requirements Specification

## 1. Introduction
### 1.1 Purpose
This document outlines the requirements for the WateringSystem project, an automated plant watering system designed to maintain optimal soil moisture levels for indoor or garden plants. The purpose of this document is to define the functional and non-functional requirements of the system to guide development and testing.

### 1.2 Scope
The WateringSystem project includes hardware and software development for an ESP32-based plant watering controller. The system will monitor environmental conditions, control a water pump, and provide a web-based user interface for configuration and monitoring.

Within scope:
- Hardware selection and integration
- Firmware development for the ESP32 controller
- Web interface for system control and monitoring
- Basic scheduling and automation features

Out of scope:
- Mobile applications
- Integration with commercial smart home systems
- Water quality monitoring
- Fertilizer distribution

### 1.3 Project Context
- **Business or user needs addressed**: The system addresses the need for automated plant care to maintain healthy plants during absence, busy schedules, or for users who want to optimize plant growing conditions.
- **Relationship to other systems**: Standalone system with potential future expansion for smart home integration.
- **Environmental considerations**: Designed for indoor or covered outdoor use with access to a power source and water supply.
- **Budget and timeline constraints**: Focus on cost-effective components while ensuring reliability and safety.

### 1.4 Definitions and Acronyms
| Term | Definition |
|------|------------|
| ESP32 | ESP32-WROOM-32E microcontroller used as the main control unit |
| BME280 | Environmental sensor for temperature and humidity measurements |
| LittleFS | Lightweight file system used for storing configuration and web files on the ESP32 |
| UI | User Interface |
| PCB | Printed Circuit Board |
| RS485 | Serial communication standard for long-distance and noise-resistant data transmission |
| Modbus | Communication protocol commonly used with industrial devices, including RS485 |
| EC | Electrical Conductivity, a measure of soil's ability to conduct electricity (related to nutrient content) |
| PH | Measure of soil acidity or alkalinity |
| NPK | Nitrogen, Phosphorus, and Potassium - primary nutrients for plants |
| TTL | Transistor-Transistor Logic, a digital logic standard |

## 2. System Description
The WateringSystem is an automated plant watering controller that uses environmental sensors to monitor conditions and control a water pump to maintain ideal moisture levels for plants. The system offers a web-based interface for monitoring and configuration.

### 2.1 Product Perspective
The WateringSystem operates as a standalone device that can be placed near plants requiring watering. It connects to the local network to provide a web interface accessible from any browser-capable device on the same network.

### 2.2 User Classes and Characteristics
- **Home gardeners**: Users with limited technical knowledge who want a simple solution for watering plants during absence.
- **Plant enthusiasts**: Users with specific knowledge of plant needs who want to fine-tune watering schedules and conditions.
- **System administrators**: Users with technical knowledge who will install and maintain the system.

### 2.3 Operating Environment
- **Hardware**: 
  - ESP32-WROOM-32E microcontroller
  - BME280 temperature and humidity sensor
  - Soil Moisture Temperature Humidity EC PH NPK sensor with RS485/Modbus interface
  - RS485 to TTL converter (high-quality alternative to MAX485)
  - 12V water pump with appropriate driver circuit
  - Power supply for both ESP32 and water pump
  - Water reservoir and tubing

- **Software**:
  - ESP32 firmware written in C/C++ using PlatformIO
  - Web interface using standard HTML/CSS/JavaScript
  - LittleFS for file storage on the ESP32
  - Modbus RTU protocol implementation for soil sensor communication

## 3. Functional Requirements
### 3.1 User Interface
- The system shall provide a web-based user interface accessible via standard browsers
- The UI shall display current environmental readings (temperature, humidity)
- The UI shall allow users to configure watering schedules
- The UI shall display system status and alert conditions
- The UI shall provide historical data of environmental conditions and watering events

### 3.2 Hardware Interface
- The system shall interface with a BME280 sensor via I2C protocol
- The system shall interface with the soil sensor via RS485/Modbus RTU protocol
- The system shall include a high-quality RS485 to TTL converter for reliable communication with the soil sensor
- The system shall control a 12V water pump via appropriate driver circuit
- The system shall include status LEDs to indicate operation status
- The system shall include manual override buttons for direct control

### 3.3 Software Interface
- The firmware shall utilize the ESP32 Arduino core libraries
- The system shall implement Modbus RTU protocol for soil sensor communication
- The system shall use LittleFS for storing configuration and web files
- The system shall implement a lightweight web server for the user interface

### 3.4 Communication Interfaces
- The system shall connect to local Wi-Fi networks
- The system shall serve web content via HTTP
- The system shall implement basic authentication for administrative functions
- The system shall provide the option for secure communications via HTTPS

### 3.5 Software Development Requirements
- The system shall be developed using C++ with strict adherence to modern C++ standards
- The system firmware shall be developed using the PlatformIO ecosystem with Arduino framework
- The code shall follow object-oriented programming principles with proper use of constructors and destructors
- The system shall use interface-based design for all sensor and actuator components
- All hardware interactions shall be abstracted through interfaces to ensure modularity and testability
- Each sensor type shall have a dedicated interface that defines the standard interaction methods
- Concrete implementations of interfaces shall be provided for each physical sensor (BME280, RS485 soil sensor)
- The system shall use dependency injection principles to allow for easy component replacement and testing

## 4. Non-Functional Requirements
### 4.1 Performance
- The system shall respond to web interface requests within 2 seconds
- The system shall read sensor data at least once per minute
- The system shall be capable of controlling the pump with precision of at least 1 second

### 4.2 Reliability
- The system shall operate continuously for at least 6 months without requiring restart
- The system shall store configuration data in non-volatile memory
- The system shall recover from power outages without user intervention
- The system shall include watchdog functionality to detect and recover from software failures

### 4.3 Security
- The system shall implement basic authentication for administrative functions
- The system shall validate all user inputs to prevent injection attacks
- The system shall support firmware updates via the web interface
- The system shall provide options to restrict network access to the device

### 4.4 Usability
- The user interface shall be responsive and usable on mobile devices
- The system configuration shall be achievable without requiring technical knowledge
- The system shall provide clear status indicators for current operation
- The system shall notify users of error conditions via the web interface

### 4.5 Maintainability
- The firmware shall be modular to allow for easy component updates
- The system shall maintain logs of operations and errors
- The system shall provide a mechanism for backing up and restoring configuration
- The system shall be designed to allow for future sensor additions

### 4.6 Code Quality
- All code shall adhere to consistent coding style guidelines
- All code shall include appropriate documentation in English
- All interfaces shall be fully documented with function descriptions and parameter details
- The system shall maintain a high level of unit test coverage for all critical components
- All code shall be version controlled using Git
- Code reviews shall be conducted for all significant changes
- Memory management shall follow RAII principles (Resource Acquisition Is Initialization)
- The code shall avoid dynamic memory allocation where possible due to embedded constraints
- Error handling shall be consistent and robust throughout the codebase

## 5. System Features
### 5.1 Environmental Monitoring
- The system shall monitor ambient temperature and humidity using the BME280 sensor
- The system shall monitor soil conditions (moisture, temperature, humidity, EC, pH, NPK) using the RS485 soil sensor
- The system shall record and display trends of both ambient and soil environmental data
- The system shall allow for setting acceptable ranges for all environmental conditions
- The system shall alert when environmental conditions are outside acceptable ranges
- The system shall provide calibration options for the soil sensor measurements

### 5.2 Automated Watering
- The system shall control the water pump based on configurable schedules
- The system shall support time-based and sensor-based watering triggers
- The system shall use soil moisture readings as primary triggers for automated watering
- The system shall consider other soil parameters (EC, pH) when determining watering needs
- The system shall prevent overwatering through configurable limits
- The system shall provide manual override capabilities for immediate watering

### 5.3 Web Interface
- The system shall provide a responsive web interface accessible from the local network
- The system shall display current status and historical data via the web interface
- The system shall allow configuration changes through the web interface
- The system shall support data export via the web interface

## 6. Constraints
- The system must operate using the specified ESP32-WROOM-32E microcontroller
- The system must reliably communicate with the soil sensor via RS485 interface
- The system must use a high-quality RS485 to TTL converter instead of MAX485 for improved reliability
- The system must be powered by standard residential power supply
- The system must be designed with cost-effectiveness in mind
- The system must be safe to operate near water and electricity
- The firmware must operate within the memory constraints of the ESP32

## 7. Quality Attributes
- **Safety**: The system shall implement appropriate electrical isolation between water components and electronic components
- **Energy efficiency**: The system shall minimize power consumption during idle periods
- **Water efficiency**: The system shall optimize water usage through precise control based on soil moisture levels
- **Communication reliability**: The system shall implement error detection and recovery for RS485 communication
- **Measurement accuracy**: The system shall provide accurate soil measurements with appropriate calibration
- **Extensibility**: The system architecture shall support future addition of sensors and features

## 8. Appendices
### 8.1 Hardware Reference
Detailed specifications of the hardware components will be provided in the hardware.md document.

### 8.2 Safety Guidelines
Safety considerations for installation and operation will be detailed in the safety.md document.

## 9. Development Approach
### 9.1 Software Architecture
The system software shall follow a modular, layered architecture with the following components:
- **Hardware Abstraction Layer**: Interfaces and implementations for all hardware devices
- **Sensor Management**: Classes for reading, processing, and storing sensor data
- **Control Logic**: Decision-making components for watering and other actions
- **Communication Layer**: Web server and Modbus RTU protocol implementations
- **Storage Layer**: Configuration and data persistence using LittleFS
- **User Interface Layer**: Web-based interface for monitoring and control

### 9.2 Object-Oriented Design
- Each major component shall be represented by an abstract interface class
- Concrete implementations shall inherit from these interfaces
- Dependency injection shall be used to provide implementations to higher-level components
- Memory management shall follow RAII principles with proper use of constructors/destructors
- Standard C++ containers and algorithms shall be used where appropriate
- Static polymorphism (templates) shall be preferred over dynamic polymorphism where appropriate to reduce runtime overhead

### 9.3 Interface Examples
The system shall include interfaces such as:
- `ISensor`: Base interface for all sensors with methods for initialization, reading, and status
- `IActuator`: Base interface for all actuators with methods for control and status
- `IEnvironmentalSensor`: Interface for ambient condition sensors (extending ISensor)
- `ISoilSensor`: Interface for soil condition sensors (extending ISensor)
- `IWaterPump`: Interface for water pump control (extending IActuator)
- `IModbusDevice`: Interface for Modbus protocol communication devices
- `IDataStorage`: Interface for data persistence operations

### 9.4 Development Tools
- PlatformIO shall be used for project management, building, and deployment
- Git shall be used for version control
- Unit testing shall be implemented using appropriate C++ testing frameworks
- Static code analysis tools shall be used to ensure code quality
- Continuous integration shall be implemented to automate testing

### 9.5 Coding Standards
- The code shall follow consistent naming conventions:
  - CamelCase for class names
  - camelCase for method and variable names
  - UPPER_CASE for constants
  - Interface classes shall be prefixed with 'I'
- All public methods shall include documentation comments
- Error handling shall use structured exception handling or error codes consistently
- The codebase shall minimize global variables and state
- All code shall be in English, including comments, variable names, and documentation
