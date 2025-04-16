# User Guide

## Table of Contents
- [Introduction](#introduction)
- [Getting Started](#getting-started)
- [Installation](#installation)
- [Configuration](#configuration)
- [Basic Usage](#basic-usage)
- [Web Interface](#web-interface)
- [Advanced Features](#advanced-features)
- [Troubleshooting](#troubleshooting)
- [FAQ](#faq)
- [Appendices](#appendices)

## Introduction
The WateringSystem is an automated plant irrigation solution designed for home gardeners, agricultural applications, and indoor plant enthusiasts. This system monitors environmental and soil conditions in real-time and provides both automated and manual watering capabilities through a user-friendly web interface.

![WateringSystem Overview](../images/watering_system_overview.png)
*Figure 1: The WateringSystem with controller unit, sensors, and water pump*

## Getting Started
### Prerequisites
Before using the WateringSystem, you'll need:
- A compatible WiFi network
- A computer, tablet, or smartphone with a web browser
- A power supply for the WateringSystem device
- The included sensors and water pump properly installed

### System Requirements
- WiFi network (2.4GHz)
- Modern web browser (Chrome, Firefox, Safari, or Edge)
- Stable power source for the WateringSystem device
- Water reservoir connected to the water pump

## Installation
Follow these steps to install your WateringSystem:

1. Place the WateringSystem controller in a suitable location near your plants and water source
2. Install the soil moisture sensor in the soil of the plant you wish to monitor
3. Position the environmental sensor in a location representative of your growing area
4. Connect the water pump to the controller and place the pump inlet in your water reservoir
5. Connect the power supply to the WateringSystem controller
6. Follow the WiFi configuration steps in the next section

## Configuration
### WiFi Setup
When you first power on your WateringSystem, it will create its own WiFi access point:

1. On your computer or mobile device, connect to the WiFi network named "WateringSystem-Setup"
2. When prompted for a password, enter "watering123"
3. Open a web browser and navigate to http://192.168.4.1
4. You will see the WiFi setup page with available networks

![WiFi Setup Screen](../images/wifi_setup_screen.png)
*Figure 2: The WiFi setup page showing available networks*

5. Click the "Scan for Networks" button to refresh the list
6. Select your home WiFi network from the list or enter the details manually
7. Enter your WiFi password and click "Save and Connect"
8. The device will restart and connect to your WiFi network
9. Connect your device to the same WiFi network
10. Access the WateringSystem control panel at the IP address shown after setup (or find it in your router's connected devices list)

### Basic Configuration
After connecting to your WiFi network, you can configure the essential settings for your system:

1. Access the control panel through your web browser
2. Navigate to the "System Settings" section
3. Configure the following settings:
   - **Moisture threshold low (%)**: The soil moisture level at which automatic watering will begin
   - **Moisture threshold high (%)**: The soil moisture level at which automatic watering will stop
   - **Default watering duration (seconds)**: How long each watering cycle lasts
   - **Minimum interval between waterings (hours)**: Minimum time that must pass between automatic watering cycles
4. Click "Save Settings" to apply your changes

### Advanced Configuration
For advanced users, additional settings can be configured by accessing the system's configuration files directly. Please refer to the developer documentation for more information.

## Basic Usage
### Accessing the Control Panel
To access the WateringSystem control panel:

1. Ensure your device is connected to the same WiFi network as the WateringSystem
2. Open a web browser and enter the IP address of the WateringSystem (displayed during setup)
3. The control panel dashboard will load, showing the current status and readings

### Understanding the Dashboard
The control panel dashboard provides a comprehensive overview of your system:

#### Status Indicators
At the top of the dashboard, you'll see two status indicators:
- **Connection Status**: Shows if the web interface is connected to the controller
- **Watering Status**: Indicates whether the system is currently watering

#### Current Readings
The "Current Readings" card displays real-time sensor data:

**Environmental Data**:
- Temperature (°C)
- Humidity (%)
- Pressure (hPa)

**Soil Data**:
- Moisture (%)
- Temperature (°C)
- pH level
- EC (Electrical Conductivity) in µS/cm

Click the "Refresh Data" button to update these readings manually.

### Manual Watering Control
To manually control watering:

1. In the "System Control" section, find the "Manual Controls" area
2. Set your desired watering duration in seconds
3. Click "Start Watering" to begin manual watering
4. If needed, click "Stop Watering" to halt the process immediately

### Automatic Watering
To enable automatic watering:

1. In the "System Control" section, find the "Automatic Watering" area
2. Toggle the switch to enable automatic watering
3. The system will now water automatically when soil moisture drops below your configured threshold

When automatic watering is active, the system will:
- Monitor soil moisture continuously
- Begin watering when moisture falls below the low threshold
- Stop watering when moisture rises above the high threshold
- Respect the minimum interval between watering cycles

### Reservoir Management
The WateringSystem includes an automatic reservoir filling feature that maintains water levels in your main reservoir. This ensures your plants always have access to water, even during extended periods of absence.

To use the reservoir management feature:

1. In the "System Control" section, find the "Reservoir Control" area
2. Toggle the switch to enable the reservoir pump feature
3. Observe the current water level indicator in the reservoir status display

![Reservoir Controls](../images/reservoir_controls.png)
*Figure 8: Reservoir management controls and status display*

When the reservoir management feature is enabled:
- The system automatically monitors water levels using sensors
- When water level drops too low, the reservoir pump activates to refill
- When water level reaches the high mark, the pump automatically stops
- A safety timeout prevents continuous pumping if something goes wrong

**Manual Reservoir Control:**
You can also manually control the reservoir pump:

1. Ensure the reservoir pump feature is enabled (toggle switch on)
2. Set your desired fill duration in seconds (or use 0 for automatic stop at high level)
3. Click "Start Filling" to begin pumping water into the reservoir
4. If needed, click "Stop Filling" to halt the process immediately

**Understanding the Water Level Display:**
The water level indicator provides visual feedback about your reservoir's status:
- **Low** (Yellow): Water level is below the low sensor - will trigger automatic filling
- **Medium** (Blue): Water level is between sensors - no action needed
- **Full** (Green): Water level has reached the high sensor - pump will not activate

This feature helps ensure uninterrupted operation of your automatic watering system by maintaining adequate water supply in the main reservoir.

## Web Interface
The WateringSystem provides an intuitive web interface that allows you to monitor and control your system from any device on your local network.

### Dashboard Overview

![Dashboard Overview](../images/dashboard_overview.png)
*Figure 3: Main dashboard of the WateringSystem control panel*

The dashboard is divided into several key sections:

1. **Header Area**
   - System name and title
   - Connection status indicator (shows if connected to the controller)
   - Watering status indicator (shows if watering is currently active)

2. **Current Readings Card**
   - Displays real-time data from all sensors
   - Divided into Environmental Data and Soil Data sections
   - Last update timestamp
   - Refresh button to manually update readings

3. **System Control Card**
   - Manual watering controls
   - Automatic watering settings
   - Configuration options

4. **Historical Data Card**
   - Visualizes sensor readings over time
   - Configurable to show different sensors and time periods

### Status Indicators

![Status Indicators](../images/status_indicators.png)
*Figure 4: System status indicators showing connection and watering state*

The status indicators in the header provide at-a-glance information:

- **Connected** (🟢): Web interface is successfully communicating with the system
- **Disconnected** (🔴): Communication issues between web interface and system
- **Watering Active** (🔵): Water pump is currently running
- **Watering Inactive** (⚪): Water pump is currently off

### Current Readings Display

![Sensor Readings](../images/sensor_readings.png)
*Figure 5: Current sensor readings display*

The Current Readings card provides comprehensive information about your environment:

**Environmental Data:**
- **Temperature**: Ambient air temperature in Celsius
- **Humidity**: Relative air humidity percentage
- **Pressure**: Atmospheric pressure in hectopascals

**Soil Data:**
- **Moisture**: Soil water content percentage (key value for watering decisions)
- **Temperature**: Soil temperature in Celsius
- **pH**: Soil acidity/alkalinity level
- **EC**: Electrical conductivity in microSiemens/cm (indicates nutrient levels)

The "Last updated" timestamp shows when readings were last fetched from sensors, and the "Refresh Data" button allows you to manually request new readings.

### Manual Controls

![Manual Controls](../images/manual_controls.png)
*Figure 6: Manual watering controls*

The Manual Controls section allows you to:

1. Start watering immediately by clicking the "Start Watering" button
2. Stop watering at any time by clicking the "Stop Watering" button
3. Set a specific duration for the manual watering cycle (in seconds)

This is useful for:
- Testing the system
- Providing additional water during hot periods
- Administering fertilizer or treatments via the irrigation system

### Automatic Watering Controls

![Automatic Controls](../images/automatic_controls.png)
*Figure 7: Automatic watering controls and settings*

The Automatic Watering section allows you to:

1. Enable/disable the automatic watering function with the toggle switch
2. Configure the moisture thresholds that trigger watering:
   - **Low threshold**: When moisture falls below this value, watering starts
   - **High threshold**: When moisture rises above this value, watering stops
3. Set the default watering duration for each automatic cycle
4. Configure the minimum time between watering cycles to prevent overwatering

### Historical Data Visualization

![Historical Data](../images/historical_data.png)
*Figure 8: Historical data chart showing sensor readings over time*

The Historical Data visualization allows you to:

1. Select the type of data to display (environmental or soil)
2. Choose the specific sensor reading to track
3. Select the time period to display (1 hour to 30 days)
4. Analyze trends in your growing environment
5. Correlate environmental conditions with plant health
6. Validate that your watering schedule is maintaining desired moisture levels

The system stores sensor readings at regular intervals, providing valuable insights into your growing conditions over time.

## Advanced Features
### Historical Data Visualization
The system records sensor data over time, allowing you to visualize trends:

1. Navigate to the "Historical Data" section
2. Select the desired sensor type (Environmental or Soil)
3. Choose the specific reading you want to view (Temperature, Humidity, Moisture, etc.)
4. Select your preferred time range (Last hour, 6 hours, 24 hours, 7 days, or 30 days)
5. The chart will update to display the selected data over time

This feature helps you understand patterns in your growing environment and assess the performance of your watering schedule.

### System Settings
Fine-tune your system's behavior through the settings panel:

1. **Moisture threshold low (%)**: Set this to determine when automatic watering begins. Lower values mean drier soil before watering starts.
2. **Moisture threshold high (%)**: Set this to determine when automatic watering stops. Higher values result in wetter soil after watering.
3. **Default watering duration (seconds)**: Controls how long each automatic watering cycle lasts. Adjust based on your plants' needs and water flow rate.
4. **Minimum interval between waterings (hours)**: Prevents over-watering by enforcing a waiting period between cycles. Recommended minimum: 6 hours.

### System Information
At the bottom of the control panel, you can find:
- System version
- IP address
- Storage usage

This information is helpful when troubleshooting or seeking support.

### System Settings Interface

![System Settings](../images/system_settings.png)
*Figure 9: System settings configuration panel*

The System Settings panel allows you to fine-tune all aspects of your WateringSystem:

1. **Moisture Thresholds**
   - Low threshold: Determines when automatic watering begins (default: 30%)
   - High threshold: Determines when automatic watering stops (default: 60%)

2. **Watering Parameters**
   - Default duration: Length of each watering cycle in seconds (default: 20 seconds)
   - Minimum interval: Enforced waiting period between cycles in hours (default: 6 hours)

3. **Sensor Calibration**
   - Options for advanced users to calibrate soil and environmental sensors
   - Ability to set offset values to match professional testing equipment

4. **Notification Settings**
   - Configure alerts for important events (low water, sensor failures, etc.)
   - Set notification methods if your system supports them

### Data Export
For advanced analysis, you can export your system's data:

1. Navigate to the Historical Data section
2. Select the desired data and time range
3. Click the "Export Data" button
4. Choose CSV or JSON format
5. Save the file to your device

This allows you to perform custom analysis, create your own visualizations, or maintain records of growing conditions.

## Troubleshooting
### Common Issues
#### Cannot Connect to WiFi
- **Symptom**: Device fails to connect to WiFi or creates its own access point after configuration
- **Possible causes**: 
  - Incorrect WiFi password
  - WiFi network not in range
  - Incompatible WiFi network settings
- **Solution**:
  1. Reset the device by pressing the reset button for 10 seconds
  2. The device will create its setup access point again
  3. Reconnect to the setup network and verify your WiFi credentials
  4. Ensure the device is within good range of your WiFi router

#### Sensor Readings Show "--"
- **Symptom**: One or more sensor readings display dashes instead of values
- **Possible causes**:
  - Sensor disconnected
  - Sensor malfunction
  - Communication error
- **Solution**:
  1. Check that all sensors are properly connected
  2. Refresh the data by clicking the "Refresh Data" button
  3. Power cycle the device by disconnecting and reconnecting power
  4. If the issue persists, check sensor placement and wiring

#### Automatic Watering Not Working
- **Symptom**: System does not water automatically despite low moisture readings
- **Possible causes**:
  - Automatic watering is disabled
  - Moisture threshold set incorrectly
  - Minimum interval between waterings not yet elapsed
  - Water pump issue
- **Solution**:
  1. Verify automatic watering is enabled (toggle switch should be on)
  2. Check your moisture threshold settings
  3. Verify the last watering time to ensure minimum interval has passed
  4. Test the pump using manual watering to ensure it's functional

#### Reservoir Pump Not Working
- **Symptom**: Reservoir pump doesn't activate or reservoir doesn't fill properly
- **Possible causes**:
  - Reservoir pump feature is disabled
  - Water level sensors are disconnected or malfunctioning
  - Pump is disconnected or damaged
  - Tubing is blocked or kinked
- **Solution**:
  1. Verify the reservoir pump feature is enabled (toggle switch should be on)
  2. Check the water level sensor connections
  3. Test the pump using manual filling to isolate the issue
  4. Inspect tubing for blockages or damage
  5. Verify the water source for the reservoir pump has sufficient water

### Diagnostics
To help diagnose issues:
1. Note the exact sensor readings at the time of the problem
2. Check the historical data to see if there are any patterns or anomalies
3. Verify system information (IP, storage usage) for any unusual values
4. Test manual watering to isolate whether the issue is with sensors or the pump
5. Restart the system and see if the issue persists

### Visual Troubleshooting Guide

![Troubleshooting Guide](../images/troubleshooting.png)
*Figure 10: Visual guide to common issues and their solutions*

The most common issues can be diagnosed by observing the system status:

1. **Connection Problems** - Check the connection status indicator in the header:
   - Red indicator means the web interface cannot communicate with the controller
   - Verify the device and your computer are on the same network
   - Try refreshing the page or restarting the device

2. **Sensor Reading Errors** - If readings show "--" or unreasonable values:
   - Check physical connections to sensors
   - Verify sensor placement (soil sensors must be properly inserted)
   - Look for damage to sensor probes or wires

3. **Watering System Issues** - If the pump doesn't activate:
   - Test manual watering to isolate the problem
   - Check water reservoir level
   - Inspect tubing for kinks or blockages
   - Verify pump power connection

## FAQ
### How often should I water my plants?
The optimal watering frequency depends on your specific plants, soil type, and environmental conditions. As a general guideline:
- Set the moisture threshold low to 30% for most house plants
- Set the moisture threshold high to 60% for most house plants
- Set the minimum interval to at least 6 hours to prevent overwatering

### Can I access the system remotely?
The basic system is designed for local network access only. For remote access, you would need to set up port forwarding on your router or use a VPN. Note that enabling remote access introduces security considerations.

### How accurate are the soil sensors?
The soil moisture sensors provide relative readings that are useful for automation but may not match professional soil testing equipment. For most home gardening purposes, the relative readings are sufficient for automated watering decisions.

### Will the system work during a power outage?
No, the WateringSystem requires continuous power to operate. When power is restored, the system will automatically reconnect to your WiFi network and resume operation.

## Appendices
### Glossary
- **EC (Electrical Conductivity)**: A measure of the soil's ability to conduct electricity, which correlates with nutrient content
- **Moisture Threshold**: The moisture percentage that triggers automatic watering actions
- **pH**: A measure of soil acidity or alkalinity on a scale of 0-14 (7 is neutral)
- **hPa (Hectopascal)**: A unit of atmospheric pressure

### Reference Materials
- [Product Website](https://wateringsystem.example.com)
- [Support Forum](https://support.wateringsystem.example.com)
- [Developer Documentation](https://docs.wateringsystem.example.com)

### System Specifications
- **Controller**: ESP32 microcontroller with WiFi capabilities
- **Environmental Sensor**: BME280 (temperature, humidity, pressure)
- **Soil Sensors**: Capacitive moisture, temperature, pH, and EC sensors
- **Water Pump**: 12V DC pump with flow rate of approximately 100-300 ml/minute
- **Power Requirements**: 12V DC, 2A power supply
- **Operating Range**: 0-40°C (32-104°F)
- **Storage Capacity**: Up to 30 days of sensor data (varies by sensor frequency)

### Image Reference Guide
This guide includes multiple figures to help you understand the WateringSystem:

- Figure 1: Complete system hardware overview
- Figure 2: WiFi setup interface
- Figure 3: Main dashboard layout
- Figure 4: Status indicator examples
- Figure 5: Sensor readings display
- Figure 6: Manual watering controls
- Figure 7: Automatic watering settings
- Figure 8: Historical data visualization
- Figure 9: System settings panel
- Figure 10: Troubleshooting visual guide
