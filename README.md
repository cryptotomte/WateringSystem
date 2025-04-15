# WateringSystem

![Project Status](https://img.shields.io/badge/status-active-green.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)

## Overview
The WateringSystem is an ESP32-based automated plant watering controller designed to maintain optimal soil moisture levels for indoor or garden plants. It monitors environmental conditions, controls water delivery, and provides a web-based interface for configuration and monitoring.

## Table of Contents
- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Software Dependencies](#software-dependencies)
- [Installation](#installation)
- [Usage](#usage)
- [Documentation](#documentation)
- [Testing](#testing)
- [Safety](#safety)
- [Contributing](#contributing)
- [License](#license)

## Features
- **Environmental Monitoring**: Monitors ambient temperature and humidity using BME280 sensor
- **Soil Condition Monitoring**: Tracks soil moisture, temperature, pH, EC, and NPK using RS485/Modbus soil sensor
- **Automated Watering**: Controls water pump based on configurable schedules and sensor readings
- **Web Interface**: Provides a responsive web interface for monitoring and configuration
- **OTA Updates**: Supports Over-The-Air firmware updates through AsyncElegantOTA
- **Data Logging**: Records historical environmental and soil data
- **Modular Design**: Interface-based architecture for easy component replacement and testing

## Hardware Requirements
- **Microcontroller**: ESP32-WROOM-32E
- **Environmental Sensor**: BME280 (temperature, humidity, pressure)
- **Soil Sensor**: RS485/Modbus soil moisture, temperature, humidity, EC, pH, NPK sensor
- **Communication**: SP3485 RS485 to TTL converter (high-quality alternative to MAX485)
- **Actuator**: 12V DC water pump with MOSFET control circuit
- **Power Supply**: 12V DC (2A minimum) with 3.3V regulation for digital components

For complete hardware specifications and wiring diagrams, see [Hardware Documentation](docs/hardware.md).

## Software Dependencies
- **Framework**: Arduino framework for ESP32
- **Build System**: PlatformIO
- **Libraries**:
  - Wire (for I2C communication)
  - Adafruit BME280 (for environmental sensor)
  - ModbusMaster (for RS485 soil sensor)
  - ESPAsyncWebServer (for web interface)
  - AsyncElegantOTA (for OTA updates)
  - LittleFS (for file storage)
  - ArduinoJson (for data processing)

## Installation
1. Clone the repository:
```bash
git clone https://github.com/cryptotomte/WateringSystem.git
cd WateringSystem
```

2. Install PlatformIO (if not already installed):
```bash
pip install platformio
```

3. Build and upload the firmware:
```bash
platformio run --target upload
```

4. Upload the file system (for web interface):
```bash
platformio run --target uploadfs
```

## Usage
1. **Initial Setup**
   - Power on the device
   - Connect to the WiFi network "WateringSystem-XXXX"
   - Navigate to 192.168.4.1 in your web browser
   - Follow the setup wizard to configure your home WiFi network

2. **Web Interface**
   - Access the web interface by navigating to the IP address of the device
   - Monitor current environmental and soil conditions
   - Configure watering schedules and thresholds
   - View historical data and system logs

3. **OTA Updates**
   - Access the update page at http://[device-ip]/update
   - Upload new firmware files through the web interface

## Documentation
Detailed documentation is available in the `docs` directory:
- [System Requirements Specification](docs/requirements.md)
- [Architecture Documentation](docs/architecture.md)
- [Hardware Documentation](docs/hardware.md)
- [User Guide](docs/user-guide.md)
- [Testing Documentation](docs/testing.md)
- [Safety Documentation](docs/safety.md)
- [Release Notes](docs/release-notes.md)

## Testing
The system includes unit tests and integration tests that can be executed using PlatformIO:

```bash
# Run all tests
platformio test

# Run specific test
platformio test -e test_sensors
```

For complete testing documentation, see [Testing Documentation](docs/testing.md).

## Safety
The WateringSystem interfaces with both water and electricity. Important safety considerations:
- All electrical components must be properly isolated from water
- Use waterproof enclosure for outdoor installations
- Include proper grounding for lightning protection in outdoor installations
- Power supply must be isolated and conform to safety standards

For complete safety information, see [Safety Documentation](docs/safety.md).

## Contributing
Contributions to the WateringSystem project are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for details on our code of conduct and the process for submitting pull requests.

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
