# CLAUDE.md

> ⚠️ **MAINTENANCE BRANCH — FROZEN LEGACY FIRMWARE**
> This branch (`arduino-maintenance`) preserves the Arduino/PlatformIO firmware (v2.3.x)
> that runs the production greenhouse unit. It is NEVER merged into `main`.
> Active development happens on `main` under `firmware/` (ESP-IDF).
> All platform and library dependencies are pinned exactly in `platformio.ini`
> (do not relax the pins), and CI (`.github/workflows/arduino-legacy.yml`) is the
> canonical build. Patch flow: worktree → fix → CI build (pinned deps) → deploy to the
> production unit (serial flash — this firmware has no web-OTA endpoint) →
> tag `arduino-v2.3.x`.

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WateringSystem v2.3 is a production-ready embedded IoT system for automated greenhouse plant watering. This is a sophisticated ESP32-based system with hardware-managed power architecture, RS485 Modbus communication, and comprehensive safety features.

**Key Architecture:** Hardware-managed LDO power domains with TXS0108E level shifting for reliable 5kV optical isolation between ESP32 (3.3V) and field devices (5V).

## Development Environment & Build System

### PlatformIO Configuration
- **Environment:** `wateringsystem` (defined in platformio.ini)
- **Platform:** ESP32 (espressif32)
- **Framework:** Arduino
- **Board:** esp32dev
- **Filesystem:** LittleFS

### Essential Build Commands

**CI is the canonical build:** every push to `arduino-maintenance` runs
`.github/workflows/arduino-legacy.yml`, which builds with exactly pinned
dependencies and uploads a fully flashable artifact set (`firmware.bin`,
`littlefs.bin`, `bootloader.bin`, `partitions.bin`). Local PlatformIO builds
are optional and only needed for flashing/monitoring over USB.

```bash
# Build the project
platformio run --environment wateringsystem

# Upload firmware to device
platformio run --target upload --environment wateringsystem

# Upload filesystem (web interface files)
platformio run --target uploadfs --environment wateringsystem

# Clean build
platformio run --target clean

# Monitor serial output
platformio device monitor --port /dev/ttyUSB0 --baud 115200
```

### Windows PowerShell Commands
```powershell
# Build project (Windows with full path)
& "C:\Users\crypt\.platformio\penv\Scripts\pio.exe" run --environment wateringsystem

# Upload to device
& "C:\Users\crypt\.platformio\penv\Scripts\pio.exe" run --target upload --environment wateringsystem
```

## Code Architecture & Design Patterns

### Interface-Based Architecture
The codebase follows strict interface-based design with clean separation of concerns:

**Core Interfaces:**
- `ISensor` - Base for all sensors (BME280, Modbus soil sensor)
- `IEnvironmentalSensor` - Temperature, humidity, pressure sensors
- `ISoilSensor` - Soil moisture, pH, EC, NPK sensors  
- `IActuator` - Base for all actuators
- `IWaterPump` - Water pump control with manual/automatic modes
- `IModbusClient` - RS485 Modbus RTU communication
- `IDataStorage` - Configuration and sensor data persistence

### Hardware Abstraction Layers
- **Sensors:** `sensors/` - BME280Sensor, ModbusSoilSensor with interface implementations
- **Actuators:** `actuators/` - WaterPump with safety features and timed operation
- **Communication:** `communication/` - SP3485ModbusClient, WateringSystemWebServer
- **Storage:** `storage/` - LittleFSStorage for configuration and data logging

### FreeRTOS Integration
- **Sensor Task:** Dedicated FreeRTOS task for non-blocking sensor reads (5-second intervals)
- **Thread Safety:** Mutex-protected shared data between sensor task and main loop
- **Task Management:** Proper task lifecycle management with cleanup

## Hardware-Software Integration

### Critical Pin Mapping (Verified July 2025)
```cpp
#define PIN_I2C_SDA           21  // BME280 data
#define PIN_I2C_SCL           22  // BME280 clock
#define PIN_RS485_TX          17  // ESP32 TX -> TXS0108E A1 -> RS485 DI
#define PIN_RS485_RX          16  // ESP32 RX <- TXS0108E A2 <- RS485 RO  
#define PIN_RS485_DE          25  // Direction control via TXS0108E A3
#define PIN_MAIN_PUMP_CONTROL 26  // Main Water Pump MOSFET Gate
#define PIN_RESERVOIR_PUMP_CONTROL 27  // Reservoir Pump MOSFET Gate
```

### RS485 Communication Architecture
- **Hardware:** SP3485EN transceiver with TXS0108E level shifter (3.3V↔5V)
- **Protocol:** Modbus RTU at 9600 baud, 8N1 format
- **Timing:** 50µs switching delays for reliable level shifting
- **Devices:** Soil sensors (NPK, pH, EC, moisture, temperature)

### Power Architecture
- **12V LiFePO4 Battery** → Always-on LDO regulators
- **ESP32 Domain (3.3V):** Control logic, WiFi, BME280
- **Field Domain (5V):** RS485 transceivers, soil sensors
- **Common Ground:** No isolation, simplified design for greenhouse use

## Safety & Reliability Features

### Critical Safety Systems
- **Sensor Validation:** Automatic pump shutdown if sensor data invalid in automatic mode
- **Manual Mode Override:** Manual pump operation bypasses sensor safety checks
- **Fail-Safe Design:** Hardware-managed power, no software power control
- **Watchdog Protection:** Software watchdog prevents system hangs
- **Emergency Stops:** Multiple safety mechanisms for pump control

### Error Handling Strategy
- Graceful degradation when sensors fail
- Retry mechanisms for RS485 communication
- WiFi reconnection with exponential backoff
- Persistent error logging and status reporting

## Web Interface & Configuration

### Development Server
- **Framework:** ESPAsyncWebServer with LittleFS file storage
- **Features:** Real-time sensor monitoring, pump control, configuration management
- **Firmware Updates:** No web-OTA endpoint exists in this firmware; updates are flashed over serial (use the CI artifacts: firmware.bin, littlefs.bin, bootloader.bin, partitions.bin)
- **WiFi Configuration:** AP mode setup at 192.168.4.1 when unconfigured

### API Endpoints Structure
- GET/POST form-data for critical pump operations (reliability)
- JSON responses for status and configuration data
- Callback-based architecture for reservoir pump integration

## Serial Diagnostic Commands

The system includes comprehensive diagnostic commands accessible via Serial monitor:

```cpp
// Available commands (case-insensitive):
"rs485test" or "test"  // Comprehensive RS485 diagnostic tests
"soil" or "sensor"     // Test main application soil sensor
"help"                 // Show available commands
```

## Development Guidelines

### Code Standards (from .github/copilot-instructions.md)
- **Language:** Swedish for commit messages and discussions, English for all code
- **C++ Standard:** Strict modern C++ with constructors/destructors
- **Interface Design:** Always use interfaces with inheritance for modularity
- **Include Guards:** Format: `WATERINGSYSTEM_PATHNAME_FILENAME_H`
- **Memory Management:** RAII principles, avoid dynamic allocation

### Architecture Documents
- `docs/requirements.md` - System requirements specification
- `docs/architecture.md` - Detailed software architecture
- `docs/hardware.md` - Complete hardware specifications and pin mapping
- `docs/future-improvements.md` - Post-production enhancement roadmap

### Testing Strategy
- Hardware-in-the-loop testing for RS485 communication
- Mock implementations for all hardware interfaces  
- Serial diagnostic commands for field testing
- Test files in `test/` directory for specific components

## Deployment & Production

### Build Flags & Configuration
```cpp
-D VERSION=0.1.0
-D DEBUG=1
-D NO_GLOBAL_HTTPMETHOD  // Prevents HTTP method conflicts
```

### Hardware Deployment
- **Target Hardware:** ESP32-WROOM-32E in IP65 enclosure
- **Power Supply:** 12V 20Ah LiFePO4 battery (>48h operation)
- **Environmental Rating:** 0°C to 50°C operation, IP65 protection
- **Communication:** RS485 up to 200m for greenhouse applications

### Production Readiness
- **License:** AGPL-3.0-or-later for commercial use
- **Safety Certified:** 5kV optical isolation, fail-safe design
- **Field Tested:** RS485 communication verified, power management validated
- **Documentation:** Complete user guides and technical specifications

## Key Dependencies

All dependencies are pinned to exact versions (the known-good set that built the
production binary). Never reintroduce `^`/`~` ranges or unpinned entries on this branch.

```ini
platform = espressif32@6.12.0
lib_deps =
    adafruit/Adafruit BME280 Library@2.2.4
    adafruit/Adafruit Unified Sensor@1.1.15
    bblanchon/ArduinoJson@6.21.5
    me-no-dev/AsyncTCP@3.3.2
    me-no-dev/ESPAsyncWebServer@3.6.0
    adafruit/Adafruit BusIO@1.17.0   ; transitive pin (BME280 dep)
```

Note: ESPAsyncWebServer also installs ESP32Async/AsyncTCP transitively, but only the
direct `me-no-dev/AsyncTCP@3.3.2` copy is linked (same as the production build). Do not
add the esp32async copy to `lib_deps` — it causes multiple-definition linker errors.

## System Monitoring & Maintenance

### Status Indicators
- **Serial Output:** Comprehensive system status every 5 seconds
- **LED Status:** Different blink patterns for system states
- **Web Dashboard:** Real-time sensor data and system health
- **Storage Monitoring:** Automatic filesystem usage reporting

### Maintenance Commands
- **WiFi Reset:** Hold config button during startup for emergency AP mode
- **System Restart:** Automatic restart after WiFi configuration changes
- **Diagnostic Mode:** Serial commands for field troubleshooting

This system represents a production-ready IoT embedded solution with comprehensive safety features, designed for reliable greenhouse automation deployment.