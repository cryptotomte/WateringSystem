# sensors — Modbus soil sensor driver layer.
#
# ModbusSoilSensor.cpp is pure C++ (decode/validation/calibration logic) and
# builds on every target, including the linux preview target used by the host
# test suite. EspModbusClient.cpp is the only hardware touchpoint (esp-modbus
# master + UART RS485 half-duplex + RX pull-up) and is excluded — together
# with its driver/esp-modbus dependencies — when building for linux
# (research.md R7, same mechanism as storage/actuators).
if(${IDF_TARGET} STREQUAL "linux")
    idf_component_register(
        SRCS "src/ModbusSoilSensor.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces board
    )
else()
    # Target build only: the esp-modbus client. esp-modbus is a managed
    # component (pinned in this component's idf_component.yml, rule-gated to
    # non-linux targets; registered under its namespaced component name).
    # PRIV: esp-modbus and driver headers appear only in
    # src/EspModbusClient.cpp / private code, never in this component's
    # public headers (same rule as storage's littlefs).
    idf_component_register(
        SRCS "src/ModbusSoilSensor.cpp" "src/EspModbusClient.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces board
        PRIV_REQUIRES espressif__esp-modbus esp_driver_uart esp_driver_gpio
    )
endif()
