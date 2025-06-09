# Hardware Specifications - WateringSystem v2.3 (Hardware-Managed LDO Architecture)

## Overview

This document provides complete hardware specifications for the hardware-managed WateringSystem designed for cost-effective greenhouse automation. The system uses LDO voltage converters with common ground and optical signal isolation - optimized for always-on 12V applications with hardware-only power management.

## Main Components

### 1. Microcontroller - ESP32-WROOM-32E

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

### 2. Environmental Sensor - BME280

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

### 3. Soil Sensor - RS485 Modbus Soil Sensor

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
| Supply Voltage | 5V DC (LDO-managed always-on) |
| Current Consumption | < 50 mA (typical) |
| Cable Length | Up to 200 meters (adequate for greenhouse) |

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

### 4. RS485 Interface - MIKROE-4156 with Optical Isolation

**MikroElektronika RS485 5 Click (MIKROE-4156)**

| Specification | Value |
|---------------|-------|
| Interface | UART to RS485 |
| Transceiver IC | ADM3485ARZ (Analog Devices) |
| Signal Rate | Up to 12 Mbps |
| Operating Voltage | 5V (LDO-managed) |
| ESD Protection | ±15kV on bus pins |
| Bus Load | Supports up to 32 transceivers |
| Interface | mikroBUS™ connector |
| Dimensions | 25.4 x 28.58 mm |

**FOD817BSD Optocouplers (Optical Isolation)**

| Specification | Value |
|---------------|-------|
| Type | Phototransistor optocoupler |
| Isolation Voltage | 5000 VRMS |
| CTR (Current Transfer Ratio) | 130-260% (typical) |
| Forward Voltage | 1.2V (typical) |
| Response Time | 18μs typical |
| Package | 4-pin DIP/SMD |
| Operating Temperature | -55°C to +110°C |
| Quantity | 3 pieces (TX, RX, DE/RE) |

### 5. Water Pumps - 12V DC

| Specification | Value |
|---------------|-------|
| Operating Voltage | 12V DC |
| Current | 0.5A - 1.5A (depending on pressure) |
| Flow Rate | 1-3 liters per minute |
| Maximum Head | 2-3 meters |
| Power Consumption | 6-18W |
| Duty Cycle | 30 minutes max continuous operation |
| Quantity | 2 pieces (plant pump + reservoir pump) |

### 6. Power Supply Components

**LiFePO4 Battery**

| Specification | Value |
|---------------|-------|
| Voltage | 12V nominal |
| Capacity | 20Ah (240Wh) |
| Type | LiFePO4 (safe chemistry) |
| Operating Temperature | 0°C to 45°C |
| Cycle Life | >2000 cycles |

**LDO Voltage Converters (Always-On Hardware Management)**

| Component | Input | Output | Current | Efficiency | Type |
|-----------|-------|--------|---------|------------|------|
| AMS1117-3.3 (ESP32 domain) | 12V | 3.3V | 1A | >85% | LDO |
| AMS1117-5.0 (Field domain) | 12V | 5V | 1A | >85% | LDO |

**Key Features:**
- Hardware-only power management (no software control)
- Always-on operation with low dropout voltage
- Common ground design eliminates ground loop issues
- Thermal protection and current limiting
- Stable output voltage regulation

## Pin Assignments - Hardware-Managed LDO Architecture

| GPIO | Connected To | Description |
|------|--------------|-------------|
| GPIO 21 | I2C SDA | BME280 Data Line |
| GPIO 22 | I2C SCL | BME280 Clock Line |
| GPIO 16 | TX2 | RS485 TX (via FOD817BSD optocoupler) |
| GPIO 17 | RX2 | RS485 RX (via FOD817BSD optocoupler) |
| GPIO 25 | DE/RE | RS485 Direction Control (via FOD817BSD) |
| GPIO 26 | Plant Pump Control | Main Water Pump MOSFET Gate |
| GPIO 27 | Reservoir Pump Control | Reservoir Filling Pump MOSFET Gate |
| GPIO 32 | Reservoir Low Level | Water Reservoir Low Level Sensor |
| GPIO 33 | Reservoir High Level | Water Reservoir High Level Sensor |
| GPIO 2 | Status LED | System Status Indicator |
| GPIO 5 | Button 1 | Manual Mode Button |
| GPIO 18 | Button 2 | Configuration Button |

**Removed Pins (No Longer Used):**
- GPIO 4 (FIELD_PWR) - Power now hardware-managed by LDO
- GPIO 19 (SENSOR_PWR) - Sensors always powered via LDO
- GPIO 23 (ISO_MONITOR) - Hardware isolation monitoring removed
- GPIO 15 (ISO_FAULT_LED) - Software fault detection removed

## Power Architecture - Hardware-Managed LDO System

### Power Domains

| Domain | Voltage | Ground | Control | Components |
|--------|---------|--------|---------|------------|
| **ESP32** | 3.3V | GND_COMMON | LDO Always-On | ESP32, BME280, Control logic |
| **FIELD** | 5V | GND_COMMON | LDO Always-On | RS485 module, Soil sensor |

### Power Distribution

```
12V LiFePO4 Battery
├── LDO 3.3V → ESP32 Domain (control, WiFi, sensors)
└── LDO 5V   → Field Domain (RS485 + soil sensor)
     └── Common Ground (GND_COMMON) - No isolation required
```

### Hardware-Only Management Strategy

- **Always-On Design:** Both LDO converters provide continuous regulated power
- **No Software Control:** Power domains managed entirely by hardware
- **Common Ground:** Single ground plane simplifies design and eliminates ground loops
- **Optical Signal Isolation:** FOD817BSD optocouplers isolate RS485 signals only
- **Safety Level:** Practical for enclosed 12V systems in ASA IP65 box

## Connection Diagram

```
                        ┌─────────────────┐
                        │                 │
                        │      ESP32      │
                        │   (3.3V LDO)    │
                        └─┬───┬───┬───┬───┘
                          │   │   │   │
                          │   │   │   │
┌─────────────┐           │   │   │   │           ┌────────────┐
│             │◄──SDA─────┘   │   │   └───TX────►│ FOD817BSD  │
│   BME280    │               │   │               │(Signal ISO)│
│             │◄──SCL─────────┘   └───RX────────►│            │
└─────────────┘                   │               └──────┬─────┘
                                  └───DE/RE─────────────►│
                                                         │
┌─────────────┐                                    ┌─────▼─────┐
│  Reservoir  │                                    │           │
│  Low Level  │◄──GPIO32───┐                       │  MIKROE   │
│   Sensor    │            │                       │   4156    │
└─────────────┘            │                       │  (5V LDO  │
                           │                       │Always-On) │
┌─────────────┐            │                       └─────┬─────┘
│  Reservoir  │            │                             │
│  High Level │◄──GPIO33───┤                             │
│   Sensor    │            │                        ┌────▼─────┐
└─────────────┘            │                        │          │
                           │                        │  RS485   │
┌─────────────┐            │                        │   Soil   │
│   12V DC    │            │                        │  Sensor  │
│   Battery   ├──12V───────┼───────────────┬────────┤ (5V pow) │
│  (LiFePO4)  │            │               │        └──────────┘
└──────┬──────┘            │               │
       │                   │               │
       │                   │               │
┌──────▼──────┐            │          ┌────▼────┐
│   3.3V      │            │          │         │
│Buck Convert ├──3.3V──────┼──────────┤  Plant  │
│   LM2596    │            │          │  Pump   │
└─────────────┘            │          │ Control │
                           │          │         │
┌──────▼──────┐            │          └────┬────┘
│    5V       │            │               │
│Buck Convert ├──5V────────┼──────────┌────▼────┐
│   LM2596    │            │          │         │
└─────────────┘            │          │Reservoir│
                           │          │  Pump   │
┌─────────────┐            │          │ Control │
│   User      │            │          │         │
│  Interface  │◄───GPIO5/18┤          └────┬────┘
│  (Buttons)  │            │               │
└─────────────┘            │               │
                           │          ┌────▼────┐
┌─────────────┐            │          │         │
│   Status    │◄───GPIO2───┘          │  Water  │
│    LED      │                       │  Pumps  │
│             │                       │ (12V)   │
└─────────────┘                       └─────────┘
```

## Bill of Materials (BOM) - Simplified v2.2

| Item | Description | Quantity | Unit Price (SEK) | Total (SEK) | Notes |
|------|-------------|----------|------------------|-------------|-------|
| 1 | ESP32-WROOM-32E Module | 1 | 120 | 120 | Main microcontroller |
| 2 | BME280 Sensor Module | 1 | 50 | 50 | Environmental sensing |
| 3 | RS485 Soil Sensor (NPK) | 1 | 600 | 600 | Soil parameters measurement |
| 4 | MIKROE-4156 RS485 5 Click | 1 | 200 | 200 | RS485 transceiver |
| 5 | FOD817BSD Optocoupler | 3 | 15 | 45 | 5kV optical isolation |
| 6 | 12V DC Water Pump | 2 | 150 | 300 | Plant + reservoir pumps |
| 7 | IRLZ44N N-Channel MOSFET | 2 | 10 | 20 | Pump control |
| 8 | 1N4007 Diode | 2 | 2 | 4 | Flyback protection |
| 9 | Water Level Sensor | 2 | 25 | 50 | Reservoir level detection |
| 10 | LiFePO4 12V 20Ah Battery | 1 | 800 | 800 | Main power source |
| 11 | AMS1117-3.3 LDO Regulator | 1 | 25 | 25 | 3.3V always-on power |
| 12 | AMS1117-5.0 LDO Regulator | 1 | 25 | 25 | 5V always-on power |
| 13 | Capacitors (various) | 15 | 3 | 45 | Power filtering |
| 14 | Resistors (various) | 20 | 1 | 20 | Current limiting, pull-up/down |
| 15 | Status LEDs | 3 | 5 | 15 | Status indication |
| 16 | Push Buttons | 2 | 10 | 20 | User interface |
| 17 | Terminal Blocks | 8 | 8 | 64 | Connections |
| 18 | PCB (2-layer) | 1 | 150 | 150 | Custom designed |
| 19 | ASA Enclosure IP65 | 1 | 200 | 200 | Weather protection |
| 20 | Water Tubing (silicone) | 3m | 20 | 60 | Food-grade |
| 21 | Shielded Twisted Pair | 5m | 15 | 75 | RS485 connection |
| 22 | Water Reservoir | 1 | 100 | 100 | Storage container |
| 23 | Misc (screws, wire, etc.) | 1 | 100 | 100 | Assembly materials |
| | | | **TOTAL** | **2998 SEK** | |

## Power Requirements

| Component | Voltage | Current (Peak) | Power (W) |
|-----------|---------|----------------|-----------|
| ESP32 | 3.3V | 250mA | 0.8 |
| BME280 | 3.3V | 1mA | 0.003 |
| FOD817BSD (3x) | 3.3V | 30mA | 0.1 |
| RS485 Module | 5V | 100mA | 0.5 |
| Soil Sensor | 5V | 50mA | 0.25 |
| Water Pump (active) | 12V | 1.5A | 18 |
| **Total (pump off)** | | | **1.65W** |
| **Total (pump on)** | | | **19.65W** |

**Battery Life Calculation:**
- Normal operation: 240Wh ÷ 1.65W = ~145 hours (6 days)
- With 30min pumping/day: ~130 hours (5.4 days)

## Safety Features

### Optical Isolation
- **5kV isolation** via FOD817BSD optocouplers
- **Signal separation** between ESP32 and field domains
- **Ground loop prevention** in soil sensor applications

### Hardware-Managed Power
- **Always-on LDO design** provides continuous regulated power
- **No software power control** eliminates complexity and failure points
- **Common ground design** prevents ground loop issues

### Environmental Protection
- **IP65 enclosure** for outdoor installations
- **Operating temperature:** 0°C to 50°C
- **Storage temperature:** -10°C to 60°C

## Installation Notes

### Greenhouse Deployment
1. Mount in ASA IP65 enclosure
2. Position away from direct sunlight
3. Ensure adequate ventilation
4. Waterproof all external connections

### RS485 Bus Configuration
- Use twisted pair cable for A and B signals
- Maximum cable length: 200m for greenhouse applications
- No termination resistors needed for short runs (<50m)

### Power Optimization
- LDO regulators provide stable voltage regulation
- LiFePO4 battery safe for indoor/greenhouse use
- Always-on design eliminates startup delays
- Solar charging capability (MC4 connectors planned)

## Future Expansions

### Planned Additions
- Solar charging controller (10W panel)
- Additional soil sensors (up to 8 on RS485 bus)
- Weather station integration
- LoRa communication for remote monitoring

### Hardware Provisions
- Spare GPIO pins for expansion
- Extra power capacity for additional sensors
- Modular PCB design for easy modifications

This simplified 2-domain architecture provides cost-effective greenhouse automation while maintaining essential safety features through optical isolation. The design prioritizes practical implementation over complex isolation schemes.
