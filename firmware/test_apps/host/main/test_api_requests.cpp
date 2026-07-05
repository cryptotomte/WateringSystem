// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_api_requests.cpp
 * @brief Host suite for the pure API request parsers/validators (feature 009).
 *
 * Registers the parseConfigSet / parsePumpCommand accept/reject tests (T012):
 * an in-range config subset is accepted; each out-of-range field is rejected
 * all-or-nothing (a valid companion field is NOT applied); malformed JSON and
 * wrong-typed fields are rejected. Pump commands accept start/run(1..300)/stop
 * and reject a bad action, an out-of-range duration and malformed JSON.
 */

#include <string>

#include "unity.h"

#include "api/ApiDtos.h"
#include "api/ApiRequests.h"

namespace {

// --- parseConfigSet ------------------------------------------------------

void test_config_accept_in_range_subset(void)
{
    // A subset of settable fields, all in range. Absent fields stay unset.
    std::string body =
        "{\"moistureThresholdLow\":30,\"wateringDurationS\":20}";
    api::ConfigSetResult r = api::parseConfigSet(body);

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.request.moistureThresholdLow.has_value());
    TEST_ASSERT_EQUAL_FLOAT(30.0f, r.request.moistureThresholdLow.value());
    TEST_ASSERT_TRUE(r.request.wateringDurationS.has_value());
    TEST_ASSERT_EQUAL_UINT32(20, r.request.wateringDurationS.value());
    // Fields not present in the body remain unset.
    TEST_ASSERT_FALSE(r.request.moistureThresholdHigh.has_value());
    TEST_ASSERT_FALSE(r.request.wateringEnabled.has_value());
    TEST_ASSERT_FALSE(r.request.sensorReadIntervalMs.has_value());
}

void test_config_reject_moisture_out_of_range(void)
{
    // moisture 101 (> 100). A valid companion field must NOT be applied.
    std::string body =
        "{\"moistureThresholdLow\":101,\"wateringDurationS\":20}";
    api::ConfigSetResult r = api::parseConfigSet(body);

    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.request.moistureThresholdLow.has_value());
    TEST_ASSERT_FALSE(r.request.wateringDurationS.has_value());  // no partial apply
    TEST_ASSERT_TRUE(r.error.find("moistureThresholdLow") != std::string::npos);
}

void test_config_reject_duration_zero(void)
{
    std::string body =
        "{\"wateringDurationS\":0,\"moistureThresholdLow\":30}";
    api::ConfigSetResult r = api::parseConfigSet(body);

    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.request.wateringDurationS.has_value());
    TEST_ASSERT_FALSE(r.request.moistureThresholdLow.has_value());
}

void test_config_reject_duration_over_max(void)
{
    std::string body = "{\"wateringDurationS\":301}";
    api::ConfigSetResult r = api::parseConfigSet(body);

    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.request.wateringDurationS.has_value());
    TEST_ASSERT_TRUE(r.error.find("wateringDurationS") != std::string::npos);
}

void test_config_reject_interval_zero(void)
{
    std::string body =
        "{\"minWateringIntervalS\":0,\"wateringEnabled\":true}";
    api::ConfigSetResult r = api::parseConfigSet(body);

    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.request.minWateringIntervalS.has_value());
    TEST_ASSERT_FALSE(r.request.wateringEnabled.has_value());  // no partial apply
}

void test_config_reject_sensor_read_below_floor(void)
{
    // sensorReadIntervalMs 999 (< 1000 ms floor).
    std::string body = "{\"sensorReadIntervalMs\":999}";
    api::ConfigSetResult r = api::parseConfigSet(body);

    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.request.sensorReadIntervalMs.has_value());
    TEST_ASSERT_TRUE(r.error.find("sensorReadIntervalMs") != std::string::npos);
}

void test_config_reject_data_log_below_floor(void)
{
    // dataLogIntervalMs 59999 (< 60000 ms floor).
    std::string body = "{\"dataLogIntervalMs\":59999}";
    api::ConfigSetResult r = api::parseConfigSet(body);

    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.request.dataLogIntervalMs.has_value());
    TEST_ASSERT_TRUE(r.error.find("dataLogIntervalMs") != std::string::npos);
}

void test_config_reject_malformed_json(void)
{
    std::string body = "{not valid json";
    api::ConfigSetResult r = api::parseConfigSet(body);

    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.request.moistureThresholdLow.has_value());
}

void test_config_reject_wrong_type(void)
{
    // A string where a number is expected.
    std::string body = "{\"wateringDurationS\":\"20\"}";
    api::ConfigSetResult r = api::parseConfigSet(body);

    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.request.wateringDurationS.has_value());
    TEST_ASSERT_TRUE(r.error.find("wateringDurationS") != std::string::npos);
}

// --- parsePumpCommand ----------------------------------------------------

void test_pump_accept_start_min_duration(void)
{
    std::string body = "{\"action\":\"start\",\"durationS\":1}";
    api::PumpCommandResult r = api::parsePumpCommand(body);

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.command.action == api::PumpAction::Start);
    TEST_ASSERT_TRUE(r.command.durationS.has_value());
    TEST_ASSERT_EQUAL_UINT32(1, r.command.durationS.value());
}

void test_pump_accept_run_max_duration(void)
{
    std::string body = "{\"action\":\"run\",\"durationS\":300}";
    api::PumpCommandResult r = api::parsePumpCommand(body);

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.command.action == api::PumpAction::Run);
    TEST_ASSERT_TRUE(r.command.durationS.has_value());
    TEST_ASSERT_EQUAL_UINT32(300, r.command.durationS.value());
}

void test_pump_accept_stop(void)
{
    std::string body = "{\"action\":\"stop\"}";
    api::PumpCommandResult r = api::parsePumpCommand(body);

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.command.action == api::PumpAction::Stop);
    TEST_ASSERT_FALSE(r.command.durationS.has_value());
}

void test_pump_reject_bad_action(void)
{
    std::string body = "{\"action\":\"bogus\"}";
    api::PumpCommandResult r = api::parsePumpCommand(body);

    TEST_ASSERT_FALSE(r.ok);
}

void test_pump_reject_run_duration_zero(void)
{
    std::string body = "{\"action\":\"run\",\"durationS\":0}";
    api::PumpCommandResult r = api::parsePumpCommand(body);

    TEST_ASSERT_FALSE(r.ok);
}

void test_pump_reject_run_duration_over_max(void)
{
    std::string body = "{\"action\":\"run\",\"durationS\":301}";
    api::PumpCommandResult r = api::parsePumpCommand(body);

    TEST_ASSERT_FALSE(r.ok);
}

void test_pump_reject_malformed_json(void)
{
    std::string body = "not json at all";
    api::PumpCommandResult r = api::parsePumpCommand(body);

    TEST_ASSERT_FALSE(r.ok);
}

}  // namespace

void run_api_requests_tests(void)
{
    RUN_TEST(test_config_accept_in_range_subset);
    RUN_TEST(test_config_reject_moisture_out_of_range);
    RUN_TEST(test_config_reject_duration_zero);
    RUN_TEST(test_config_reject_duration_over_max);
    RUN_TEST(test_config_reject_interval_zero);
    RUN_TEST(test_config_reject_sensor_read_below_floor);
    RUN_TEST(test_config_reject_data_log_below_floor);
    RUN_TEST(test_config_reject_malformed_json);
    RUN_TEST(test_config_reject_wrong_type);
    RUN_TEST(test_pump_accept_start_min_duration);
    RUN_TEST(test_pump_accept_run_max_duration);
    RUN_TEST(test_pump_accept_stop);
    RUN_TEST(test_pump_reject_bad_action);
    RUN_TEST(test_pump_reject_run_duration_zero);
    RUN_TEST(test_pump_reject_run_duration_over_max);
    RUN_TEST(test_pump_reject_malformed_json);
}
