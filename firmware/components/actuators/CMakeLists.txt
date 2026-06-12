# actuators — pump driver layer.
#
# WaterPump.cpp is pure C++ (all timing/safety logic) and builds on every
# target, including the linux preview target used by the host test suite.
# GpioWaterPump.cpp is the only hardware touchpoint (gpio_set_level) and is
# excluded — together with its esp_driver_gpio dependency — when building
# for linux (research.md D2).
if(${IDF_TARGET} STREQUAL "linux")
    idf_component_register(
        SRCS "src/WaterPump.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces
    )
else()
    idf_component_register(
        SRCS "src/WaterPump.cpp" "src/GpioWaterPump.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces
        PRIV_REQUIRES esp_driver_gpio
    )
endif()
