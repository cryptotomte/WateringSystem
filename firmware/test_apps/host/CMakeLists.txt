# Host test app for the pump actuator layer (IDF linux preview target).
#
# Built and run with no ESP32 attached:
#   idf.py --preview set-target linux && idf.py build && ./build/pump_host_tests.elf
# The executable's exit code equals the Unity failure count (CI gate).
cmake_minimum_required(VERSION 3.22)

# Reuse the firmware components (interfaces, actuators) without copying.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

# Component isolation: only main + its requirements are built, so the
# esp32 firmware sdkconfig is never polluted by the linux target.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pump_host_tests)
