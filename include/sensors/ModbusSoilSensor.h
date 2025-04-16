/**
 * @file ModbusSoilSensor.h
 * @brief Modbus soil sensor implementation
 * @author WateringSystem Team
 * @date 2025-04-16
 */

#ifndef WATERINGSYSTEM_SENSORS_MODBUSSOILSENSOR_H
#define WATERINGSYSTEM_SENSORS_MODBUSSOILSENSOR_H

#include "sensors/ISoilSensor.h"
#include "communication/IModbusClient.h"
#include <map>
#include <string>

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
    
    // Calibration factors for each parameter
    float moistureCalibrationFactor;
    float phCalibrationFactor;
    float ecCalibrationFactor;
    
    // Valid ranges for sensor readings
    struct ValidRange {
        float min;
        float max;
        bool isSet;
        
        ValidRange() : min(0.0f), max(0.0f), isSet(false) {}
        ValidRange(float minVal, float maxVal) : min(minVal), max(maxVal), isSet(true) {}
    };
    
    std::map<std::string, ValidRange> validRanges;
    
    // Register map for sensor readings
    static const uint16_t REG_MOISTURE = 0x0000;
    static const uint16_t REG_TEMPERATURE = 0x0001;
    static const uint16_t REG_PH = 0x0002;
    static const uint16_t REG_EC = 0x0003;
    static const uint16_t REG_NITROGEN = 0x0004;
    static const uint16_t REG_PHOSPHORUS = 0x0005;
    static const uint16_t REG_POTASSIUM = 0x0006;
    static const uint16_t REG_HUMIDITY = 0x0007;
    
    // Register map for calibration
    static const uint16_t REG_MOISTURE_CALIB = 0x0100;
    static const uint16_t REG_PH_CALIB = 0x0101;
    static const uint16_t REG_EC_CALIB = 0x0102;
    
    // Utility methods
    float convertRegisterToFloat(uint16_t registerValue, float scale);
    uint16_t convertFloatToRegister(float value, float scale);
    
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
    
    // Calibration methods
    bool calibrateMoisture(float referenceValue) override;
    bool calibratePH(float referenceValue) override;
    bool calibrateEC(float referenceValue) override;
    
    // Range validation methods
    bool setValidRange(const char* parameter, float minValue, float maxValue) override;
    bool isWithinValidRange(const char* parameter, float value) override;
};

#endif // WATERINGSYSTEM_SENSORS_MODBUSSOILSENSOR_H