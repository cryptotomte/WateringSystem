// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_api_serialize.cpp
 * @brief Host suite for the pure API JSON serializers (feature 009, US1/T007).
 *
 * Builds DTOs with known values, runs the serializers, re-parses the output
 * with cJSON and asserts the envelope shape and field contents. Covers the
 * US1 read serializers (status / sensors / power) plus the safety invariants:
 * the wifi password never appears, rev1 power serializes as JSON null, a
 * `valid=false` soil section is still emitted (non-finite values as null), NPK
 * channels appear only when their has-flag is set, and a not-set clock
 * serializes without a bogus epoch.
 */

#include <cmath>
#include <string>
#include <vector>

#include "unity.h"

#include "cJSON.h"

#include "api/ApiDtos.h"
#include "api/ApiRequests.h"
#include "api/ApiSerialize.h"

namespace {

// --- helpers -------------------------------------------------------------

/// A representative, fully-populated status DTO (rev2, power present).
api::SystemStatusDto makeStatus()
{
    api::SystemStatusDto s;
    s.mode = "automatic";
    s.wifi.state = "connected";
    s.wifi.rssi = -57;
    s.wifi.ssid = "greenhouse";
    s.wifi.connected = true;
    s.wifi.ipAcquired = true;
    s.wifi.ip = "192.168.1.42";
    s.time.synced = true;
    s.time.epoch = 1751000000;
    s.time.local = "2026-06-27 08:13:20 +0200";
    s.time.lastSync = 1750999000;
    s.uptimeMs = 123456;
    s.resetReason = "POWERON";
    s.firmware.version = "3.0.0-dev";
    s.firmware.project = "WateringSystem";
    s.storage.totalBytes = 960000;
    s.storage.usedBytes = 48000;
    s.storage.percentUsed = 5.0f;
    s.hasPower = true;
    s.power.valid = true;
    s.power.busVoltage = 12.1f;
    s.power.current = 0.35f;
    s.power.power = 4.235f;
    return s;
}

// --- status --------------------------------------------------------------

void test_status_envelope_and_fields(void)
{
    std::string body = api::serializeStatus(makeStatus());
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "success")));
    TEST_ASSERT_EQUAL_STRING(
        "automatic", cJSON_GetObjectItem(root, "mode")->valuestring);

    cJSON* wifi = cJSON_GetObjectItem(root, "wifi");
    TEST_ASSERT_NOT_NULL(wifi);
    TEST_ASSERT_EQUAL_STRING(
        "connected", cJSON_GetObjectItem(wifi, "state")->valuestring);
    TEST_ASSERT_EQUAL_INT(-57, cJSON_GetObjectItem(wifi, "rssi")->valueint);
    TEST_ASSERT_EQUAL_STRING(
        "greenhouse", cJSON_GetObjectItem(wifi, "ssid")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(wifi, "connected")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(wifi, "ipAcquired")));
    TEST_ASSERT_EQUAL_STRING(
        "192.168.1.42", cJSON_GetObjectItem(wifi, "ip")->valuestring);

    cJSON* time = cJSON_GetObjectItem(root, "time");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(time, "synced")));
    TEST_ASSERT_EQUAL_DOUBLE(
        1751000000.0, cJSON_GetObjectItem(time, "epoch")->valuedouble);

    cJSON* fw = cJSON_GetObjectItem(root, "firmware");
    TEST_ASSERT_EQUAL_STRING(
        "3.0.0-dev", cJSON_GetObjectItem(fw, "version")->valuestring);
    TEST_ASSERT_EQUAL_STRING(
        "WateringSystem", cJSON_GetObjectItem(fw, "project")->valuestring);

    cJSON* st = cJSON_GetObjectItem(root, "storage");
    TEST_ASSERT_EQUAL_DOUBLE(
        960000.0, cJSON_GetObjectItem(st, "totalBytes")->valuedouble);

    cJSON_Delete(root);
}

void test_status_never_serializes_wifi_password(void)
{
    // There is no password field anywhere in the DTO; assert the key is absent
    // at the top level and inside the wifi block (defence in depth).
    std::string body = api::serializeStatus(makeStatus());
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_NULL(cJSON_GetObjectItem(root, "password"));
    cJSON* wifi = cJSON_GetObjectItem(root, "wifi");
    TEST_ASSERT_NULL(cJSON_GetObjectItem(wifi, "password"));
    TEST_ASSERT_NULL(cJSON_GetObjectItem(wifi, "pass"));

    // Belt-and-braces: the literal substring never appears in the raw body.
    TEST_ASSERT_TRUE(body.find("password") == std::string::npos);

    cJSON_Delete(root);
}

void test_status_rev1_power_is_json_null(void)
{
    api::SystemStatusDto s = makeStatus();
    s.hasPower = false;  // rev1: no INA226

    std::string body = api::serializeStatus(s);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);

    cJSON* power = cJSON_GetObjectItem(root, "power");
    TEST_ASSERT_NOT_NULL(power);          // key present
    TEST_ASSERT_TRUE(cJSON_IsNull(power));  // value is JSON null

    cJSON_Delete(root);
}

void test_status_not_set_clock_still_serializes(void)
{
    // Un-synced boot state: epoch below the plausibility threshold (0).
    api::SystemStatusDto s = makeStatus();
    s.time.synced = false;
    s.time.epoch = 0;
    s.time.local = "";
    s.time.lastSync = 0;

    std::string body = api::serializeStatus(s);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);

    cJSON* time = cJSON_GetObjectItem(root, "time");
    TEST_ASSERT_NOT_NULL(time);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(time, "synced")));
    // A not-set clock emits a JSON null epoch (no bogus 1970), mirroring the
    // not-set top-level sensor timestamp.
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(time, "epoch")));
    TEST_ASSERT_EQUAL_STRING(
        "", cJSON_GetObjectItem(time, "local")->valuestring);

    cJSON_Delete(root);
}

// --- sensors -------------------------------------------------------------

void test_sensors_valid_readings(void)
{
    api::SensorReadingsDto d;
    d.environmental.valid = true;
    d.environmental.temperature = 21.5f;
    d.environmental.humidity = 48.0f;
    d.environmental.pressure = 1013.2f;
    d.level.low.valid = true;
    d.level.low.waterPresent = true;
    d.level.high.valid = true;
    d.level.high.waterPresent = false;
    d.hasTimestamp = true;
    d.timestamp = 1751000000;

    std::string body = api::serializeSensors(d);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "success")));

    cJSON* env = cJSON_GetObjectItem(root, "environmental");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(env, "valid")));
    TEST_ASSERT_EQUAL_DOUBLE(
        21.5, cJSON_GetObjectItem(env, "temperature")->valuedouble);

    cJSON* level = cJSON_GetObjectItem(root, "level");
    cJSON* low = cJSON_GetObjectItem(level, "low");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(low, "valid")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(low, "waterPresent")));
    cJSON* high = cJSON_GetObjectItem(level, "high");
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(high, "waterPresent")));

    cJSON* ts = cJSON_GetObjectItem(root, "timestamp");
    TEST_ASSERT_EQUAL_DOUBLE(1751000000.0, ts->valuedouble);

    cJSON_Delete(root);
}

void test_sensors_invalid_soil_section_still_emitted_with_null(void)
{
    // Soil valid=false with NaN placeholders (the PR-11-not-yet state). The
    // section is still serialized; non-finite values render as JSON null (our
    // documented choice — a 0 would read as a real measurement).
    api::SensorReadingsDto d;
    d.soil.valid = false;
    d.soil.moisture = std::nan("");
    d.soil.temperature = std::nan("");
    d.soil.humidity = std::nan("");
    d.soil.ph = std::nan("");
    d.soil.ec = std::nan("");

    std::string body = api::serializeSensors(d);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);

    cJSON* soil = cJSON_GetObjectItem(root, "soil");
    TEST_ASSERT_NOT_NULL(soil);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(soil, "valid")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(soil, "moisture")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(soil, "ph")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(soil, "ec")));

    cJSON_Delete(root);
}

void test_sensors_npk_present_only_when_flagged(void)
{
    api::SensorReadingsDto d;
    d.soil.valid = true;
    d.soil.moisture = 33.0f;
    d.soil.temperature = 18.0f;
    d.soil.humidity = 40.0f;
    d.soil.ph = 6.5f;
    d.soil.ec = 1.2f;
    // Only nitrogen reported by the sensor this read.
    d.soil.hasNitrogen = true;
    d.soil.nitrogen = 120.0f;
    d.soil.hasPhosphorus = false;
    d.soil.hasPotassium = false;

    std::string body = api::serializeSensors(d);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);

    cJSON* soil = cJSON_GetObjectItem(root, "soil");
    cJSON* nitrogen = cJSON_GetObjectItem(soil, "nitrogen");
    TEST_ASSERT_NOT_NULL(nitrogen);
    TEST_ASSERT_EQUAL_DOUBLE(120.0, nitrogen->valuedouble);
    // Un-flagged channels are absent (not null, not 0).
    TEST_ASSERT_NULL(cJSON_GetObjectItem(soil, "phosphorus"));
    TEST_ASSERT_NULL(cJSON_GetObjectItem(soil, "potassium"));

    cJSON_Delete(root);
}

void test_sensors_rev1_power_null_and_not_set_timestamp(void)
{
    // rev1 (no power) and clock not yet set: power key is JSON null and the
    // top-level timestamp is JSON null (no bogus 1970 epoch).
    api::SensorReadingsDto d;
    d.hasPower = false;
    d.hasTimestamp = false;

    std::string body = api::serializeSensors(d);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);

    cJSON* power = cJSON_GetObjectItem(root, "power");
    TEST_ASSERT_NOT_NULL(power);
    TEST_ASSERT_TRUE(cJSON_IsNull(power));

    cJSON* ts = cJSON_GetObjectItem(root, "timestamp");
    TEST_ASSERT_NOT_NULL(ts);
    TEST_ASSERT_TRUE(cJSON_IsNull(ts));

    cJSON_Delete(root);
}

// --- power ---------------------------------------------------------------

void test_power_rev2_fields_spread(void)
{
    api::PowerDto p;
    p.valid = true;
    p.busVoltage = 12.0f;
    p.current = 0.5f;
    p.power = 6.0f;

    std::string body = api::serializePower(p);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "success")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "valid")));
    TEST_ASSERT_EQUAL_DOUBLE(
        12.0, cJSON_GetObjectItem(root, "busVoltage")->valuedouble);
    TEST_ASSERT_EQUAL_DOUBLE(
        0.5, cJSON_GetObjectItem(root, "current")->valuedouble);
    TEST_ASSERT_EQUAL_DOUBLE(
        6.0, cJSON_GetObjectItem(root, "power")->valuedouble);

    cJSON_Delete(root);
}

void test_power_non_finite_last_good_is_null(void)
{
    // Before the first read the last-good getters return NaN placeholders.
    api::PowerDto p;
    p.valid = false;
    p.busVoltage = std::nan("");
    p.current = std::nan("");
    p.power = std::nan("");

    std::string body = api::serializePower(p);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(root, "valid")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(root, "busVoltage")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(root, "current")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(root, "power")));

    cJSON_Delete(root);
}

void test_power_unavailable_shape(void)
{
    std::string body = api::serializePowerUnavailable();
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "success")));
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(root, "available")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(root, "power")));

    cJSON_Delete(root);
}

// --- pumps ---------------------------------------------------------------

void test_pump_serialize_fields(void)
{
    api::PumpDto p;
    p.name = "plant";
    p.running = true;
    p.currentRunTimeMs = 5000;
    p.accumulatedRunTimeMs = 123456;
    p.lastStopReason = "duration_elapsed";

    std::string body = api::serializePump(p);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "success")));
    TEST_ASSERT_EQUAL_STRING(
        "plant", cJSON_GetObjectItem(root, "name")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "running")));
    TEST_ASSERT_EQUAL_DOUBLE(
        5000.0, cJSON_GetObjectItem(root, "currentRunTimeMs")->valuedouble);
    TEST_ASSERT_EQUAL_DOUBLE(
        123456.0,
        cJSON_GetObjectItem(root, "accumulatedRunTimeMs")->valuedouble);
    TEST_ASSERT_EQUAL_STRING(
        "duration_elapsed",
        cJSON_GetObjectItem(root, "lastStopReason")->valuestring);

    cJSON_Delete(root);
}

void test_pump_list_serialize_array(void)
{
    std::vector<api::PumpDto> pumps;
    api::PumpDto plant;
    plant.name = "plant";
    plant.running = false;
    api::PumpDto reservoir;
    reservoir.name = "reservoir";
    reservoir.running = true;
    pumps.push_back(plant);
    pumps.push_back(reservoir);

    std::string body = api::serializePumpList(pumps);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "success")));

    cJSON* arr = cJSON_GetObjectItem(root, "pumps");
    TEST_ASSERT_TRUE(cJSON_IsArray(arr));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(arr));

    cJSON* first = cJSON_GetArrayItem(arr, 0);
    TEST_ASSERT_EQUAL_STRING(
        "plant", cJSON_GetObjectItem(first, "name")->valuestring);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(first, "running")));

    cJSON* second = cJSON_GetArrayItem(arr, 1);
    TEST_ASSERT_EQUAL_STRING(
        "reservoir", cJSON_GetObjectItem(second, "name")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(second, "running")));

    cJSON_Delete(root);
}

// --- config --------------------------------------------------------------

void test_config_serialize_all_fields_no_password(void)
{
    api::ConfigDto c;
    c.moistureThresholdLow = 30.0f;
    c.moistureThresholdHigh = 55.0f;
    c.wateringDurationS = 20;
    c.minWateringIntervalS = 300;
    c.wateringEnabled = true;
    c.sensorReadIntervalMs = 5000;
    c.dataLogIntervalMs = 300000;

    std::string body = api::serializeConfig(c);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "success")));

    // Every settable config field is present.
    TEST_ASSERT_EQUAL_DOUBLE(
        30.0, cJSON_GetObjectItem(root, "moistureThresholdLow")->valuedouble);
    TEST_ASSERT_EQUAL_DOUBLE(
        55.0, cJSON_GetObjectItem(root, "moistureThresholdHigh")->valuedouble);
    TEST_ASSERT_EQUAL_DOUBLE(
        20.0, cJSON_GetObjectItem(root, "wateringDurationS")->valuedouble);
    TEST_ASSERT_EQUAL_DOUBLE(
        300.0, cJSON_GetObjectItem(root, "minWateringIntervalS")->valuedouble);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "wateringEnabled")));
    TEST_ASSERT_EQUAL_DOUBLE(
        5000.0, cJSON_GetObjectItem(root, "sensorReadIntervalMs")->valuedouble);
    TEST_ASSERT_EQUAL_DOUBLE(
        300000.0, cJSON_GetObjectItem(root, "dataLogIntervalMs")->valuedouble);

    // The wifi password is never serialized in the config body.
    TEST_ASSERT_NULL(cJSON_GetObjectItem(root, "password"));
    TEST_ASSERT_NULL(cJSON_GetObjectItem(root, "wifiPassword"));
    TEST_ASSERT_TRUE(body.find("password") == std::string::npos);

    cJSON_Delete(root);
}

// --- history -------------------------------------------------------------

void test_history_aligned_series_and_echo(void)
{
    api::HistorySeries series;
    series.metric = "env_temperature";
    series.reading = "temperature";
    series.start = 1751000000;
    series.end = 1751003600;
    series.timestamps = {1751000000, 1751001800, 1751003600};
    series.values = {21.5f, 22.0f, 22.5f};

    std::string body = api::serializeHistory(series);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "success")));

    cJSON* ts = cJSON_GetObjectItem(root, "timestamps");
    cJSON* vals = cJSON_GetObjectItem(root, "values");
    TEST_ASSERT_TRUE(cJSON_IsArray(ts));
    TEST_ASSERT_TRUE(cJSON_IsArray(vals));
    // timestamps[] and values[] are aligned 1:1.
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetArraySize(ts));
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetArraySize(vals));
    TEST_ASSERT_EQUAL_DOUBLE(
        1751001800.0, cJSON_GetArrayItem(ts, 1)->valuedouble);
    TEST_ASSERT_EQUAL_DOUBLE(22.5, cJSON_GetArrayItem(vals, 2)->valuedouble);

    // Echo fields.
    TEST_ASSERT_EQUAL_STRING(
        "env_temperature", cJSON_GetObjectItem(root, "metric")->valuestring);
    TEST_ASSERT_EQUAL_STRING(
        "temperature", cJSON_GetObjectItem(root, "reading")->valuestring);
    TEST_ASSERT_EQUAL_DOUBLE(
        1751000000.0, cJSON_GetObjectItem(root, "start")->valuedouble);
    TEST_ASSERT_EQUAL_DOUBLE(
        1751003600.0, cJSON_GetObjectItem(root, "end")->valuedouble);
    TEST_ASSERT_EQUAL_DOUBLE(
        3.0, cJSON_GetObjectItem(root, "count")->valuedouble);

    cJSON_Delete(root);
}

void test_history_empty_series_empty_arrays(void)
{
    // A range with no data is a SUCCESS with empty arrays and count 0, never an
    // error envelope.
    api::HistorySeries series;
    series.metric = "soil_moisture";
    // reading left unset -> echoed as JSON null.
    series.start = 1751000000;
    series.end = 1751086400;

    std::string body = api::serializeHistory(series);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "success")));

    cJSON* ts = cJSON_GetObjectItem(root, "timestamps");
    cJSON* vals = cJSON_GetObjectItem(root, "values");
    TEST_ASSERT_TRUE(cJSON_IsArray(ts));
    TEST_ASSERT_TRUE(cJSON_IsArray(vals));
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(ts));
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(vals));
    TEST_ASSERT_EQUAL_DOUBLE(
        0.0, cJSON_GetObjectItem(root, "count")->valuedouble);

    // The reading echo key is present but JSON null when absent from the query.
    cJSON* reading = cJSON_GetObjectItem(root, "reading");
    TEST_ASSERT_NOT_NULL(reading);
    TEST_ASSERT_TRUE(cJSON_IsNull(reading));

    cJSON_Delete(root);
}

// --- events --------------------------------------------------------------

void test_events_array_fields_and_order(void)
{
    // Caller supplies newest-first; order must be preserved verbatim.
    std::vector<api::EventDto> events;
    api::EventDto newest;
    newest.epoch = 1751003600;
    newest.category = 1;  // pump
    newest.detail = "plant start 20s";
    api::EventDto older;
    older.epoch = 1751000000;
    older.category = 3;  // connectivity
    older.detail = "wifi connected";
    events.push_back(newest);
    events.push_back(older);

    std::string body = api::serializeEvents(events);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "success")));

    cJSON* arr = cJSON_GetObjectItem(root, "events");
    TEST_ASSERT_TRUE(cJSON_IsArray(arr));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(arr));

    cJSON* first = cJSON_GetArrayItem(arr, 0);
    TEST_ASSERT_EQUAL_DOUBLE(
        1751003600.0, cJSON_GetObjectItem(first, "epoch")->valuedouble);
    TEST_ASSERT_EQUAL_DOUBLE(
        1.0, cJSON_GetObjectItem(first, "category")->valuedouble);
    TEST_ASSERT_EQUAL_STRING(
        "plant start 20s", cJSON_GetObjectItem(first, "detail")->valuestring);

    cJSON* second = cJSON_GetArrayItem(arr, 1);
    TEST_ASSERT_EQUAL_DOUBLE(
        1751000000.0, cJSON_GetObjectItem(second, "epoch")->valuedouble);
    TEST_ASSERT_EQUAL_STRING(
        "wifi connected", cJSON_GetObjectItem(second, "detail")->valuestring);

    cJSON_Delete(root);
}

// --- self-test -----------------------------------------------------------

void test_selftest_overall_and_checks(void)
{
    api::SelfTestResultDto result;
    result.overall = false;  // one failing check drags overall down
    api::SelfTestCheckDto env;
    env.name = "environmental";
    env.ok = true;
    env.detail = "BME280 responding";
    api::SelfTestCheckDto soil;
    soil.name = "soil_rs485";
    soil.ok = false;
    soil.detail = "no response";
    result.checks.push_back(env);
    result.checks.push_back(soil);

    std::string body = api::serializeSelfTest(result);
    cJSON* root = cJSON_Parse(body.c_str());
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "success")));
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(root, "overall")));

    cJSON* checks = cJSON_GetObjectItem(root, "checks");
    TEST_ASSERT_TRUE(cJSON_IsArray(checks));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(checks));

    cJSON* first = cJSON_GetArrayItem(checks, 0);
    TEST_ASSERT_EQUAL_STRING(
        "environmental", cJSON_GetObjectItem(first, "name")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(first, "ok")));

    cJSON* second = cJSON_GetArrayItem(checks, 1);
    TEST_ASSERT_EQUAL_STRING(
        "soil_rs485", cJSON_GetObjectItem(second, "name")->valuestring);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(second, "ok")));
    TEST_ASSERT_EQUAL_STRING(
        "no response", cJSON_GetObjectItem(second, "detail")->valuestring);

    cJSON_Delete(root);
}

// --- named range helper --------------------------------------------------

void test_named_range_to_window(void)
{
    const uint32_t now = 1751000000u;
    uint32_t t0 = 0;
    uint32_t t1 = 0;

    struct { const char* name; uint32_t seconds; } cases[] = {
        {"1h", 3600u},
        {"6h", 21600u},
        {"24h", 86400u},
        {"7d", 604800u},
        {"30d", 2592000u},
    };
    for (const auto& c : cases) {
        t0 = 0;
        t1 = 0;
        TEST_ASSERT_TRUE(api::namedRangeToWindow(c.name, now, t0, t1));
        TEST_ASSERT_EQUAL_UINT32(now, t1);
        TEST_ASSERT_EQUAL_UINT32(now - c.seconds, t0);
    }

    // Unknown range name -> false; out params left untouched.
    t0 = 42u;
    t1 = 99u;
    TEST_ASSERT_FALSE(api::namedRangeToWindow("bogus", now, t0, t1));
    TEST_ASSERT_EQUAL_UINT32(42u, t0);
    TEST_ASSERT_EQUAL_UINT32(99u, t1);
}

}  // namespace

void run_api_serialize_tests(void)
{
    RUN_TEST(test_status_envelope_and_fields);
    RUN_TEST(test_status_never_serializes_wifi_password);
    RUN_TEST(test_status_rev1_power_is_json_null);
    RUN_TEST(test_status_not_set_clock_still_serializes);
    RUN_TEST(test_sensors_valid_readings);
    RUN_TEST(test_sensors_invalid_soil_section_still_emitted_with_null);
    RUN_TEST(test_sensors_npk_present_only_when_flagged);
    RUN_TEST(test_sensors_rev1_power_null_and_not_set_timestamp);
    RUN_TEST(test_power_rev2_fields_spread);
    RUN_TEST(test_power_non_finite_last_good_is_null);
    RUN_TEST(test_power_unavailable_shape);
    RUN_TEST(test_pump_serialize_fields);
    RUN_TEST(test_pump_list_serialize_array);
    RUN_TEST(test_config_serialize_all_fields_no_password);
    RUN_TEST(test_history_aligned_series_and_echo);
    RUN_TEST(test_history_empty_series_empty_arrays);
    RUN_TEST(test_events_array_fields_and_order);
    RUN_TEST(test_selftest_overall_and_checks);
    RUN_TEST(test_named_range_to_window);
}
