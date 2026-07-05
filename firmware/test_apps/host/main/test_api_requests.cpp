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

#include <cstdint>
#include <optional>
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

void test_pump_accept_start_max_duration(void)
{
    // Upper boundary of the 1..300 s window for "start".
    std::string body = "{\"action\":\"start\",\"durationS\":300}";
    api::PumpCommandResult r = api::parsePumpCommand(body);

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.command.action == api::PumpAction::Start);
    TEST_ASSERT_TRUE(r.command.durationS.has_value());
    TEST_ASSERT_EQUAL_UINT32(300, r.command.durationS.value());
}

void test_pump_accept_run_min_duration(void)
{
    // Lower boundary of the 1..300 s window for "run".
    std::string body = "{\"action\":\"run\",\"durationS\":1}";
    api::PumpCommandResult r = api::parsePumpCommand(body);

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.command.action == api::PumpAction::Run);
    TEST_ASSERT_TRUE(r.command.durationS.has_value());
    TEST_ASSERT_EQUAL_UINT32(1, r.command.durationS.value());
}

void test_pump_reject_run_missing_duration(void)
{
    // "run" without the required durationS is a rejection.
    std::string body = "{\"action\":\"run\"}";
    api::PumpCommandResult r = api::parsePumpCommand(body);

    TEST_ASSERT_FALSE(r.ok);
}

void test_pump_reject_run_duration_wrong_type(void)
{
    // durationS as a string, not a number.
    std::string body = "{\"action\":\"run\",\"durationS\":\"30\"}";
    api::PumpCommandResult r = api::parsePumpCommand(body);

    TEST_ASSERT_FALSE(r.ok);
}

void test_pump_reject_missing_action(void)
{
    // An empty object has no action at all.
    std::string body = "{}";
    api::PumpCommandResult r = api::parsePumpCommand(body);

    TEST_ASSERT_FALSE(r.ok);
}

void test_pump_reject_non_string_action(void)
{
    // action must be a string; a number is a rejection, not a crash.
    std::string body = "{\"action\":5}";
    api::PumpCommandResult r = api::parsePumpCommand(body);

    TEST_ASSERT_FALSE(r.ok);
}

// --- parseConfigSet extra branches ---------------------------------------

void test_config_reject_wrong_type_bool(void)
{
    // wateringEnabled as a string, not a JSON bool. Nothing is applied.
    std::string body = "{\"wateringEnabled\":\"yes\"}";
    api::ConfigSetResult r = api::parseConfigSet(body);

    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.request.wateringEnabled.has_value());
    TEST_ASSERT_TRUE(r.error.find("wateringEnabled") != std::string::npos);
}

void test_config_accept_moisture_high_bounds(void)
{
    // moistureThresholdHigh accepts both ends of the 0..100 range.
    api::ConfigSetResult lo =
        api::parseConfigSet("{\"moistureThresholdHigh\":0}");
    TEST_ASSERT_TRUE(lo.ok);
    TEST_ASSERT_TRUE(lo.request.moistureThresholdHigh.has_value());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, lo.request.moistureThresholdHigh.value());

    api::ConfigSetResult hi =
        api::parseConfigSet("{\"moistureThresholdHigh\":100}");
    TEST_ASSERT_TRUE(hi.ok);
    TEST_ASSERT_TRUE(hi.request.moistureThresholdHigh.has_value());
    TEST_ASSERT_EQUAL_FLOAT(100.0f, hi.request.moistureThresholdHigh.value());
}

void test_config_reject_moisture_low_negative(void)
{
    // moistureThresholdLow below the 0 floor.
    std::string body = "{\"moistureThresholdLow\":-1}";
    api::ConfigSetResult r = api::parseConfigSet(body);

    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FALSE(r.request.moistureThresholdLow.has_value());
    TEST_ASSERT_TRUE(r.error.find("moistureThresholdLow") != std::string::npos);
}

void test_config_accept_interval_floors(void)
{
    // Each interval field accepts its exact floor value.
    api::ConfigSetResult interval =
        api::parseConfigSet("{\"minWateringIntervalS\":1}");
    TEST_ASSERT_TRUE(interval.ok);
    TEST_ASSERT_TRUE(interval.request.minWateringIntervalS.has_value());
    TEST_ASSERT_EQUAL_UINT32(1, interval.request.minWateringIntervalS.value());

    api::ConfigSetResult sensorRead =
        api::parseConfigSet("{\"sensorReadIntervalMs\":1000}");
    TEST_ASSERT_TRUE(sensorRead.ok);
    TEST_ASSERT_TRUE(sensorRead.request.sensorReadIntervalMs.has_value());
    TEST_ASSERT_EQUAL_UINT32(
        1000, sensorRead.request.sensorReadIntervalMs.value());

    api::ConfigSetResult dataLog =
        api::parseConfigSet("{\"dataLogIntervalMs\":60000}");
    TEST_ASSERT_TRUE(dataLog.ok);
    TEST_ASSERT_TRUE(dataLog.request.dataLogIntervalMs.has_value());
    TEST_ASSERT_EQUAL_UINT32(60000, dataLog.request.dataLogIntervalMs.value());
}

// --- resolveEventCount ---------------------------------------------------

void test_resolve_event_count_bounds(void)
{
    // Absent -> default 50.
    TEST_ASSERT_EQUAL_UINT32(
        50u, static_cast<uint32_t>(api::resolveEventCount(std::nullopt)));
    // In-range values pass through.
    TEST_ASSERT_EQUAL_UINT32(
        1u, static_cast<uint32_t>(api::resolveEventCount(1)));
    TEST_ASSERT_EQUAL_UINT32(
        200u, static_cast<uint32_t>(api::resolveEventCount(200)));
    // Above the cap clamps to 200.
    TEST_ASSERT_EQUAL_UINT32(
        200u, static_cast<uint32_t>(api::resolveEventCount(201)));
    TEST_ASSERT_EQUAL_UINT32(
        200u, static_cast<uint32_t>(api::resolveEventCount(100000)));
    // Non-positive values default rather than error.
    TEST_ASSERT_EQUAL_UINT32(
        50u, static_cast<uint32_t>(api::resolveEventCount(0)));
    TEST_ASSERT_EQUAL_UINT32(
        50u, static_cast<uint32_t>(api::resolveEventCount(-5)));
}

// --- resolveWindow -------------------------------------------------------

void test_resolve_window_range_precedence(void)
{
    // A named range wins over explicit start/end when both are supplied.
    const uint32_t now = 1700000000u;
    api::WindowResult r = api::resolveWindow(
        std::optional<std::string>("24h"), std::optional<uint32_t>(1000u),
        std::optional<uint32_t>(2000u), now);

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_UINT32(now, r.t1);
    TEST_ASSERT_EQUAL_UINT32(now - 86400u, r.t0);
}

void test_resolve_window_explicit_both(void)
{
    const uint32_t now = 1700000000u;
    api::WindowResult r = api::resolveWindow(
        std::nullopt, std::optional<uint32_t>(1000u),
        std::optional<uint32_t>(2000u), now);

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_UINT32(1000u, r.t0);
    TEST_ASSERT_EQUAL_UINT32(2000u, r.t1);
}

void test_resolve_window_start_only(void)
{
    // start-only -> end defaults to now.
    const uint32_t now = 1700000000u;
    api::WindowResult r = api::resolveWindow(
        std::nullopt, std::optional<uint32_t>(1000u), std::nullopt, now);

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_UINT32(1000u, r.t0);
    TEST_ASSERT_EQUAL_UINT32(now, r.t1);
}

void test_resolve_window_end_only(void)
{
    // end-only -> start = end - 24 h.
    const uint32_t now = 1700000000u;
    api::WindowResult r = api::resolveWindow(
        std::nullopt, std::nullopt, std::optional<uint32_t>(1700000000u), now);

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_UINT32(1700000000u - 86400u, r.t0);
    TEST_ASSERT_EQUAL_UINT32(1700000000u, r.t1);
}

void test_resolve_window_none_defaults(void)
{
    // Neither range nor explicit bounds -> the last 24 h ending at now.
    const uint32_t now = 1700000000u;
    api::WindowResult r =
        api::resolveWindow(std::nullopt, std::nullopt, std::nullopt, now);

    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_UINT32(now - 86400u, r.t0);
    TEST_ASSERT_EQUAL_UINT32(now, r.t1);
}

void test_resolve_window_unknown_range(void)
{
    // An unknown named range is a rejection (ok=false).
    const uint32_t now = 1700000000u;
    api::WindowResult r = api::resolveWindow(
        std::optional<std::string>("bogus"), std::nullopt, std::nullopt, now);

    TEST_ASSERT_FALSE(r.ok);
}

void test_named_range_to_window_underflow_clamp(void)
{
    // A small clock must never wrap uint32: t0 clamps to 0 (not now - range).
    const uint32_t now = 100u;
    uint32_t t0 = 55u;
    uint32_t t1 = 66u;

    TEST_ASSERT_TRUE(api::namedRangeToWindow("30d", now, t0, t1));
    TEST_ASSERT_EQUAL_UINT32(0u, t0);
    TEST_ASSERT_EQUAL_UINT32(now, t1);
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
    RUN_TEST(test_pump_accept_start_max_duration);
    RUN_TEST(test_pump_accept_run_min_duration);
    RUN_TEST(test_pump_reject_run_missing_duration);
    RUN_TEST(test_pump_reject_run_duration_wrong_type);
    RUN_TEST(test_pump_reject_missing_action);
    RUN_TEST(test_pump_reject_non_string_action);
    RUN_TEST(test_config_reject_wrong_type_bool);
    RUN_TEST(test_config_accept_moisture_high_bounds);
    RUN_TEST(test_config_reject_moisture_low_negative);
    RUN_TEST(test_config_accept_interval_floors);
    RUN_TEST(test_resolve_event_count_bounds);
    RUN_TEST(test_resolve_window_range_precedence);
    RUN_TEST(test_resolve_window_explicit_both);
    RUN_TEST(test_resolve_window_start_only);
    RUN_TEST(test_resolve_window_end_only);
    RUN_TEST(test_resolve_window_none_defaults);
    RUN_TEST(test_resolve_window_unknown_range);
    RUN_TEST(test_named_range_to_window_underflow_clamp);
}
