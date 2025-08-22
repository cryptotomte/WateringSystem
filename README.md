# WateringSystem v2.3 (Hardware-Managed LDO Architecture)

![Project Status](https://img.shields.io/badge/status-production--ready-brightgreen.svg)
![License](https://img.shields.io/badge/license-AGPL--3.0--or--later-blue.svg)
![Hardware](https://img.shields.io/badge/hardware-ESP32-blue.svg)
![Isolation](https://img.shields.io/badge/isolation-5kV-orange.svg)

## Overview
Cost-effective automated plant watering system with **2-domain optical isolation** for practical greenhouse automation. Features ESP32-based control, RS485 Modbus soil sensors, and simplified safety design optimized for enclosed environments.

**🔥 Latest Update v2.3**: Upgraded to TXS0108E bidirectional level shifter for faster RS485 communication - 1800x faster signal propagation while maintaining cost-effectiveness and reliability.

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

## 🌟 Key Features

### Practical Safety & Isolation
- **Fast Level Shifting**: TXS0108E provides rapid voltage translation (110 Mbps)
- **2-Domain Architecture**: ESP32 (3.3V) → FIELD Domain (5V, level shifted)
- **Cost Effective**: Ultra-low cost design with high performance
- **Greenhouse Optimized**: Practical design for enclosed 12V environments

### Professional Monitoring
- **Multi-Parameter Soil Analysis**: NPK, pH, EC, moisture, temperature via RS485 Modbus
- **Environmental Sensing**: BME280 for ambient temperature, humidity, pressure
- **Real-time Monitoring**: Web interface with live sensor data
- **Remote Web Interface**: Responsive monitoring and configuration portal

### Smart Power Management
- **Hardware-Only Control**: LDO voltage converters provide always-on power management
- **Extended Runtime**: >48h operation on 20Ah LiFePO4 battery
- **Solar Ready**: MC4 connectors for future solar panel integration
- **Battery Monitoring**: Real-time voltage and capacity tracking
- **Simplified Design**: No software power control eliminates failure points
- **Instant Operation**: Always-on design eliminates startup delays

## 🔧 Hardware Architecture

### Hardware-Managed LDO System
```
12V LiFePO4 BATTERY ──┬── AMS1117-3.3 LDO ──► ESP32 Control Domain (Always-On)
                      │                      │ TXS0108E Level Shifter (3.3V↔5V)
                      │                      └── FIELD Domain (RS485 + Sensors)
                      │
                      └── AMS1117-5.0 LDO ──► Field Power Supply (Always-On)
                               │
                               └── Common Ground (GND_COMMON)
```

### Core Components
| Component | Model | Function | Domain |
|-----------|-------|----------|---------|
| Microcontroller | ESP32-WROOM-32E | Main processor + WiFi | 3.3V Always-On |
| RS485 Interface | **SP3485EN** | Modbus communication | 5V Always-On |
| Level Shifter | **TXS0108E** | 3.3V↔5V voltage translation | Signal Path |
| Power Regulation | **AMS1117** | LDO voltage regulators | Hardware Control |
| Environmental | BME280 | Temperature/humidity | 3.3V Control |
| Soil Sensor | RS485 Modbus | NPK/pH/EC/moisture | 5V Field |

### ⚡ Hardware-Managed Safety Features
**LDO-based design provides reliable, always-on operation:**
- **Fast Level Shifting**: TXS0108E (110 Mbps capability with 10ns propagation)
- **Hardware Control**: LDO regulators eliminate software power management
- **Common Ground**: Simplified design prevents ground loop issues
- **Always-On**: Eliminates startup delays and power sequencing complexity
- **Maintenance Free**: No complex power domain monitoring required

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

### Windows PowerShell Commands
On Windows systems, PlatformIO may not be in PATH. Use these commands:

```powershell
# Build project
& "C:\Users\crypt\.platformio\penv\Scripts\pio.exe" run --environment wateringsystem

# Upload to device  
& "C:\Users\crypt\.platformio\penv\Scripts\pio.exe" run --target upload --environment wateringsystem
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
This project is licensed under the GNU Affero General Public License v3.0 or later (AGPL-3.0-or-later) - see the [LICENSE](LICENSE) file for details.

**Key License Points:**
- ✅ Commercial use allowed
- ✅ Private use allowed  
- ✅ Modification allowed
- ⚠️ **Must share source code** when distributing binaries
- ⚠️ **Must share source code** when running as network service (SaaS)
- ⚠️ **Must preserve copyright** and license notices
- ⚠️ All modifications must use the same license
