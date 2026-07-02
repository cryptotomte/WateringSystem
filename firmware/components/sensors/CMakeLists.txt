# sensors — sensor driver layer (RS485 Modbus soil sensor + I2C BME280).
#
# ModbusSoilSensor.cpp and Bme280Sensor.cpp are pure C++ (decode/validation/
# calibration resp. probe/compensation logic) and build on every target,
# including the linux preview target used by the host test suite.
# EspModbusClient.cpp (esp-modbus master + UART RS485 half-duplex + RX
# pull-up) and EspI2cBus.cpp (i2c_master bus owner) are the only hardware
# touchpoints and are excluded — together with their driver/esp-modbus
# dependencies — when building for linux (research.md R7/005 R6, same
# mechanism as storage/actuators).
if(${IDF_TARGET} STREQUAL "linux")
    idf_component_register(
        SRCS "src/ModbusSoilSensor.cpp" "src/Bme280Sensor.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces board
    )
else()
    # Target build only: the esp-modbus client and the I2C bus. esp-modbus
    # is a managed component (pinned in this component's idf_component.yml,
    # rule-gated to non-linux targets; registered under its namespaced
    # component name). PRIV: esp-modbus, driver and i2c_master headers
    # appear only in src/EspModbusClient.cpp / src/EspI2cBus.cpp / private
    # code, never in this component's public headers (same rule as
    # storage's littlefs).
    idf_component_register(
        SRCS "src/ModbusSoilSensor.cpp" "src/Bme280Sensor.cpp"
             "src/EspModbusClient.cpp" "src/EspI2cBus.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces board
        PRIV_REQUIRES espressif__esp-modbus esp_driver_uart esp_driver_gpio
                      esp_driver_i2c
    )
endif()
