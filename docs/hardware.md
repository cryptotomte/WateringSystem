# Hardware Specifications

## Overview

This document provides detailed specifications for the hardware components used in the WateringSystem project. It includes technical requirements, pin assignments, connection diagrams, and power considerations.

## Main Components

### 1. Microcontroller

**ESP32-WROOM-32E**

| Specification | Value |
|---------------|-------|
| CPU | Dual-core Xtensa LX6 microprocessor |
| Clock Speed | Up to 240 MHz |
| Flash Memory | 4 MB |
| RAM | 520 KB SRAM |
| Wireless | Wi-Fi 802.11 b/g/n (2.4 GHz) + Bluetooth 4.2 |
| GPIO Pins | 34 programmable GPIO pins |
| ADC | 12-bit SAR ADC, up to 18 channels |
| Power Supply | 3.3V |
| Operating Temperature | -40°C to +85°C |

**PIN Assignments**

| GPIO | Connected To | Description |
|------|--------------|-------------|
| GPIO 21 | I2C SDA | BME280 Data Line |
| GPIO 22 | I2C SCL | BME280 Clock Line |
| GPIO 16 | TX2 | RS485 Converter TX |
| GPIO 17 | RX2 | RS485 Converter RX |
| GPIO 25 | DE/RE | RS485 Converter Direction Control |
| GPIO 26 | Plant Pump Control | Main Water Pump MOSFET Gate |
| GPIO 27 | Reservoir Pump Control | Reservoir Filling Pump MOSFET Gate |
| GPIO 32 | Reservoir Low Level | Water Reservoir Low Level Sensor |
| GPIO 33 | Reservoir High Level | Water Reservoir High Level Sensor |
| GPIO 2 | Status LED | System Status Indicator |
| GPIO 4 | Button 1 | Manual Mode Button |
| GPIO 5 | Button 2 | Configuration Button |

### 2. Environmental Sensor

**BME280**

| Specification | Value |
|---------------|-------|
| Interface | I2C (Address: 0x76 or 0x77) |
| Temperature Range | -40°C to +85°C |
| Temperature Accuracy | ±0.5°C |
| Humidity Range | 0% to 100% RH |
| Humidity Accuracy | ±3% RH |
| Pressure Range | 300 to 1100 hPa |
| Pressure Accuracy | ±1 hPa |
| Supply Voltage | 1.71V to 3.6V |
| Current Consumption | 3.6 μA at 1 Hz |

**Connection**

| BME280 | ESP32 |
|--------|-------|
| VCC | 3.3V |
| GND | GND |
| SCL | GPIO 22 |
| SDA | GPIO 21 |

### 3. Soil Sensor

**Modbus RS485 Soil Moisture Temperature Humidity EC PH NPK Sensor**

| Specification | Value |
|---------------|-------|
| Interface | RS485 Modbus RTU |
| Baud Rate | 9600 bps (default) |
| Data Format | 8N1 (8 data bits, no parity, 1 stop bit) |
| Device Address | Configurable (default: 0x01) |
| Moisture Range | 0% to 100% |
| Temperature Range | -40°C to +80°C |
| pH Range | 3 to 9 pH |
| EC Range | 0 to 20000 μS/cm |
| NPK Range | Nitrogen: 0-1999 mg/kg, Phosphorus: 0-1999 mg/kg, Potassium: 0-1999 mg/kg |
| Supply Voltage | 12V DC |
| Current Consumption | < 50 mA (typical) |
| Cable Length | Up to 1200 meters (with proper termination) |

**Modbus Register Map**

| Register | Description | Unit | Format |
|----------|-------------|------|--------|
| 0x0000 | Soil Moisture | % | Unsigned 16-bit |
| 0x0001 | Soil Temperature | 0.1°C | Signed 16-bit |
| 0x0002 | Soil pH | 0.1 pH | Unsigned 16-bit |
| 0x0003 | Soil EC | μS/cm | Unsigned 16-bit |
| 0x0004 | Nitrogen | mg/kg | Unsigned 16-bit |
| 0x0005 | Phosphorus | mg/kg | Unsigned 16-bit |
| 0x0006 | Potassium | mg/kg | Unsigned 16-bit |
| 0x0007 | Soil Humidity | % | Unsigned 16-bit |

### 4. RS485 Interface

**SP3485 RS485 to TTL Converter Module**

| Specification | Value |
|---------------|-------|
| Interface | UART to RS485 |
| Signal Rate | Up to 10Mbps |
| Direction Control | Automatic or Manual (DE/RE pin) |
| Chipset | SP3485C |
| Supply Voltage | 3.3V (compatible with 5V logic) |
| Protection | Driver Output Short-Circuit Protection |
| Common Mode Range | -7V to +12V |
| Bus Load | Supports up to 32 transceivers on the serial bus |
| Connector Options | RJ-45, 3.5mm screw terminal, and 0.1" pitch header |
| Dimensions | 0.9 x 1.0 inches |

**Connection**

| RS485 Converter | ESP32 |
|-----------------|-------|
| VCC | 3.3V |
| GND | GND |
| TXD | GPIO 16 (TX2) |
| RXD | GPIO 17 (RX2) |
| DE/RE | GPIO 25 |
| A | RS485 Bus A (to Soil Sensor) |
| B | RS485 Bus B (to Soil Sensor) |

**RS485 Bus Configuration**

- Use twisted pair cable for A and B signals
- Apply 120Ω termination resistors at both ends of the bus for long cable runs
- Use shielded cable for outdoor or noisy environments
- Maximum cable length: 1200 meters

### 5. Water Pump

**12V DC Water Pump**

| Specification | Value |
|---------------|-------|
| Operating Voltage | 12V DC |
| Current | 0.5A - 1.5A (depending on pressure) |
| Flow Rate | 1-3 liters per minute |
| Maximum Head | 2-3 meters |
| Power Consumption | 6-18W |
| Duty Cycle | 30 minutes max continuous operation |

**Pump Control Circuit**

- Uses N-channel MOSFET (IRLZ44N or similar)
- Includes flyback diode for back-EMF protection
- Optionally includes current sensing for pump monitoring
- Gate driven by ESP32 GPIO with level shifting if necessary

**Connection**

| Component | ESP32 |
|-----------|-------|
| MOSFET Gate | GPIO 26 (via 1kΩ resistor) |
| MOSFET Source | GND |
| MOSFET Drain | Pump Negative Terminal |
| Pump Positive Terminal | 12V Supply |

### 6. Power Supply

**Power Requirements**

| Component | Voltage | Current (Peak) |
|-----------|---------|----------------|
| ESP32 | 3.3V | 250mA |
| BME280 | 3.3V | <1mA |
| RS485 Converter | 3.3V | 50mA |
| Soil Sensor | 12V | 50mA |
| Water Pump | 12V | 1.5A |
| **Total 3.3V** | 3.3V | ~300mA |
| **Total 12V** | 12V | ~1.6A |

**Power Design**

- Input: 12V DC power adapter (2A minimum rating)
- 3.3V regulator: Low-dropout (LDO) regulator for digital components
- Power filtering: Appropriate decoupling capacitors on all supply lines
- Protection: Polarity protection diode, resettable fuse for overcurrent protection
- Optional battery backup with charging circuit

### 7. PCB Design Considerations

**General Requirements**

- 2-layer PCB minimum, 4-layer recommended for better ground plane and signal integrity
- Minimum trace width: 0.2mm for signal traces, 1.0mm for power traces
- Separate analog and digital ground planes, connected at a single point
- Keep sensitive analog components away from switching components
- Use proper bypass/decoupling capacitors near ICs

**Environmental Protection**

- Conformal coating for moisture protection
- Operating temperature range: 0°C to 50°C
- Storage temperature range: -10°C to 60°C
- Consider adding TVS diodes on external connections for ESD protection

## Connection Diagram

```
                        ┌─────────────────┐
                        │                 │
                        │      ESP32      │
                        │                 │
                        └─┬───┬───┬───┬───┘
                          │   │   │   │
                          │   │   │   │
┌─────────────┐           │   │   │   │           ┌────────────┐
│             │◄──SDA─────┘   │   │   └───TX────►│            │
│   BME280    │               │   │               │   RS485    │
│             │◄──SCL─────────┘   └───RX────────►│ Converter  │
└─────────────┘                   │               │            │
                                  └───DE/RE─────►└──────┬─────┘
                                                        │
┌─────────────┐                                    ┌────▼─────┐
│  Reservoir  │                                    │          │
│  Low Level  │◄──GPIO32───┐                       │  RS485   │
│   Sensor    │            │                       │   Bus    │
└─────────────┘            │                       │          │
                           │                       └────┬─────┘
┌─────────────┐            │                            │
│  Reservoir  │            │                            │
│  High Level │◄──GPIO33───┤                       ┌────▼─────┐
│   Sensor    │            │                       │          │
└─────────────┘            │                       │  Soil    │
                           │                       │  Sensor  │
                           │                       │          │
┌─────────────┐            │                       └──────────┘
│   12V DC    │            │
│   Power     ├──12V───────┼───────────────┬──────────────┐
│   Supply    │            │               │              │
└──────┬──────┘            │               │              │
       │                   │               │              │
       │                   │               │              │
┌──────▼──────┐            │          ┌────▼────┐    ┌────▼────┐
│   3.3V      │            │          │         │    │         │
│  Regulator  ├──3.3V──────┼──────────┤  Plant  │    │Reservoir│
│             │            │          │  Pump   │    │  Pump   │
└─────────────┘            │          │ Control │    │ Control │
                           │          │         │    │         │
                           │          └────┬────┘    └────┬────┘
┌─────────────┐            │               │              │
│   User      │            │               │              │
│  Interface  │◄───GPIO4/5─┤          ┌────▼────┐    ┌────▼────┐
│  (Buttons)  │            │          │         │    │         │
└─────────────┘            │          │  Plant  │    │Reservoir│
                           │          │  Water  │    │  Water  │
                           │          │  Pump   │    │  Pump   │
┌─────────────┐            │          │         │    │         │
│             │            │          └─────────┘    └─────────┘
│   Status    │◄───GPIO2───┘
│    LED      │
│             │
└─────────────┘
```

## BOM (Bill of Materials)

| Item | Description | Quantity | Notes |
|------|-------------|----------|-------|
| 1 | ESP32-WROOM-32E Module | 1 | Main microcontroller |
| 2 | BME280 Sensor Module | 1 | Environmental sensing |
| 3 | RS485 Soil Sensor | 1 | Soil parameters measurement |
| 4 | SP3485C RS485 to TTL Converter | 1 | Full-featured RS485 transceiver |
| 5 | 12V DC Water Pump | 1 | 1-3 LPM flow rate for plant watering |
| 6 | 12V DC Water Pump | 1 | 1-3 LPM flow rate for reservoir filling |
| 7 | IRLZ44N N-Channel MOSFET | 2 | Pump control (1 per pump) |
| 8 | 1N4007 Diode | 2 | Flyback protection (1 per pump) |
| 9 | Water Level Sensor | 2 | For reservoir low and high water level detection |
| 10 | 12V DC Power Supply (2A) | 1 | Main power source |
| 11 | 3.3V LDO Voltage Regulator | 1 | For digital components |
| 12 | Capacitors (various values) | ~10 | Power filtering |
| 13 | Resistors (various values) | ~12 | Pull-up/down, current limiting |
| 14 | Status LEDs | 3 | Power, status, error indication |
| 15 | Push Buttons | 2 | User interface |
| 16 | Terminal Blocks | 6 | For power and pump connections |
| 17 | PCB | 1 | Custom designed |
| 18 | Enclosure | 1 | Waterproof, IP65 or better |
| 19 | Water Tubing | 2-3m | Food-grade silicone |
| 20 | Shielded Twisted Pair Cable | As needed | For RS485 connection |
| 21 | Water Reservoir | 1 | Main water storage container |

## Safety Considerations

- All electrical components must be properly isolated from water
- Use waterproof enclosure for outdoor installations
- Include proper grounding for lightning protection in outdoor installations
- Power supply must be isolated and conform to safety standards
- Include fuse or circuit breaker for overcurrent protection
- Design pump control to fail-safe (pump off on system failure)

## Future Hardware Expansions

The hardware design includes provision for future expansions:

1. **Additional Sensors**
   - Spare I2C pins for additional environmental sensors
   - Multiple soil sensors support on the RS485 bus

2. **Actuator Expansion**
   - Additional MOSFET outputs for controlling multiple pumps or valves
   - Support for PWM control of variable-speed pumps

3. **Connectivity Options**
   - Space for optional LoRa or Zigbee module
   - Provision for external antenna connector

4. **Power Options**
   - Circuit design to accommodate solar power input
   - Battery backup support
