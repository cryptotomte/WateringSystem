/**
 * @file ModbusSoilSensor.h
 * @brief Modbus soil sensor implementation
 * @author WateringSystem Team
 * @date 2025-04-15
 */

#pragma once

#include "sensors/ISoilSensor.h"
#include "communication/IModbusClient.h"

/**
 * @brief Implementation of the soil sensor interface for RS485 Modbus sensors
 * 
 * This class provides a concrete implementation of the ISoilSensor
 * interface for RS485 Modbus soil sensors that measure soil moisture,
 * temperature, pH, EC, and NPK values.
 */
class ModbusSoilSensor : public ISoilSensor {
private:
    IModbusClient* modbusClient;
    bool initialized;
    int lastError;
    const char* name;
    uint8_t deviceAddress;
    
    // Sensor readings
    float moisture;
    float temperature;
    float humidity;
    float ph;
    float ec;
    float nitrogen;
    float phosphorus;
    float potassium;
    
    // Register map
    static const uint16_t REG_MOISTURE = 0x0000;
    static const uint16_t REG_TEMPERATURE = 0x0001;
    static const uint16_t REG_PH = 0x0002;
    static const uint16_t REG_EC = 0x0003;
    static const uint16_t REG_NITROGEN = 0x0004;
    static const uint16_t REG_PHOSPHORUS = 0x0005;
    static const uint16_t REG_POTASSIUM = 0x0006;
    static const uint16_t REG_HUMIDITY = 0x0007;

public:
    /**
     * @brief Constructor for ModbusSoilSensor
     * @param client Pointer to ModbusClient for communication
     * @param address Modbus device address (default: 0x01)
     * @param sensorName Name to identify this sensor
     */
    ModbusSoilSensor(IModbusClient* client, uint8_t address = 0x01, 
                    const char* sensorName = "ModbusSoil");
    
    /**
     * @brief Destructor
     */
    virtual ~ModbusSoilSensor();
    
    // ISensor interface implementations
    bool initialize() override;
    bool read() override;
    bool isAvailable() override;
    int getLastError() override;
    const char* getName() const override;
    
    // ISoilSensor interface implementations
    float getMoisture() override;
    float getTemperature() override;
    float getHumidity() override;
    float getPH() override;
    float getEC() override;
    float getNitrogen() override;
    float getPhosphorus() override;
    float getPotassium() override;
};