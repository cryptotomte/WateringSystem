// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_main.cpp
 * @brief Unity runner for the host test app (linux preview target).
 *
 * Each suite file registers its tests inside a run_*_tests() function;
 * this runner calls every suite between UNITY_BEGIN/UNITY_END. The process
 * exit code equals the Unity failure count and is the CI gate.
 */

#include <cstdlib>

#include "unity.h"

void run_water_pump_tests(void);
void run_config_store_tests(void);
void run_data_storage_tests(void);
void run_soil_sensor_tests(void);
void run_bme280_tests(void);
void run_level_sensor_tests(void);
void run_ina226_tests(void);
void run_wifi_tests(void);
void run_time_tests(void);
void run_event_logger_tests(void);
void run_api_serialize_tests(void);
void run_api_requests_tests(void);
void run_api_routes_tests(void);

// Unity requires setUp/tearDown definitions (shared by all suites).
extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

extern "C" void app_main(void)
{
    UNITY_BEGIN();
    run_water_pump_tests();
    run_config_store_tests();
    run_data_storage_tests();
    run_soil_sensor_tests();
    run_bme280_tests();
    run_level_sensor_tests();
    run_ina226_tests();
    run_wifi_tests();
    run_time_tests();
    run_event_logger_tests();
    run_api_serialize_tests();
    run_api_requests_tests();
    run_api_routes_tests();
    std::exit(UNITY_END());
}
