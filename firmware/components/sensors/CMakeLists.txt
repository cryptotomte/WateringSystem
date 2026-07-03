# sensors — sensor driver layer (RS485 Modbus soil sensor + I2C BME280 +
# reservoir level sensors + I2C INA226 power monitor).
#
# ModbusSoilSensor.cpp, Bme280Sensor.cpp, DebouncedLevelSensor.cpp and
# Ina226Sensor.cpp are pure C++ (decode/validation/calibration resp.
# probe/compensation resp. settle/debounce/polarity resp. identity/scaling
# logic) and build on the linux preview target used by the host test suite.
# EspModbusClient.cpp (esp-modbus master + UART RS485 half-duplex + RX
# pull-up), EspI2cBus.cpp (i2c_master bus owner) and GpioLevelSensor.cpp
# (raw GPIO level input) are the only hardware touchpoints and are
# excluded — together with their driver/esp-modbus dependencies — when
# building for linux (research.md R7/005 R6/006 R1, same mechanism as
# storage/actuators).
#
# Ina226Sensor.cpp is additionally board-gated on device targets (006
# analyze finding I1, FR-011): only INA226-equipped boards
# (CONFIG_BOARD_REV2) compile it — the rev1 binary provably contains no
# INA226 code. The linux target keeps it unconditionally for the host
# tests (the test app defines no board choice at all).
if(${IDF_TARGET} STREQUAL "linux")
    idf_component_register(
        SRCS "src/ModbusSoilSensor.cpp" "src/Bme280Sensor.cpp"
             "src/DebouncedLevelSensor.cpp" "src/Ina226Sensor.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces board
    )
else()
    # Target build only: the esp-modbus client, the I2C bus and the GPIO
    # level input. esp-modbus is a managed component (pinned in this
    # component's idf_component.yml, rule-gated to non-linux targets;
    # registered under its namespaced component name). PRIV: esp-modbus,
    # driver and i2c_master headers appear only in src/EspModbusClient.cpp
    # / src/EspI2cBus.cpp / src/GpioLevelSensor.cpp / private code, never
    # in this component's public headers (same rule as storage's littlefs).
    set(srcs "src/ModbusSoilSensor.cpp" "src/Bme280Sensor.cpp"
             "src/DebouncedLevelSensor.cpp"
             "src/EspModbusClient.cpp" "src/EspI2cBus.cpp"
             "src/GpioLevelSensor.cpp")
    if(CONFIG_BOARD_REV2)
        list(APPEND srcs "src/Ina226Sensor.cpp")
    endif()
    idf_component_register(
        SRCS ${srcs}
        INCLUDE_DIRS "include"
        REQUIRES interfaces board
        PRIV_REQUIRES espressif__esp-modbus esp_driver_uart esp_driver_gpio
                      esp_driver_i2c
    )
endif()
