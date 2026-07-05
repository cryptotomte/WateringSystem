// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ApiSerialize.cpp
 * @brief Implementation of the pure DTO -> JSON serializers.
 */

#include "api/ApiSerialize.h"

#include <cmath>

#include "cJSON.h"

#include "api/ApiEnvelope.h"

namespace api {

namespace {

/// Add a number to @p obj, or JSON null when @p value is not finite. A
/// last-good placeholder (NaN/Inf, e.g. an unread sensor) becomes `null` — a
/// misleading 0 would read as a real measurement.
void addFiniteNumber(cJSON* obj, const char* key, double value)
{
    if (std::isfinite(value)) {
        cJSON_AddNumberToObject(obj, key, value);
    } else {
        cJSON_AddNullToObject(obj, key);
    }
}

/// Build the power telemetry object `{ valid, busVoltage, current, power }`.
/// Ownership transfers to the caller (who attaches or spreads it).
cJSON* buildPowerObject(const PowerDto& power)
{
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(obj, "valid", power.valid);
    addFiniteNumber(obj, "busVoltage", power.busVoltage);
    addFiniteNumber(obj, "current", power.current);
    addFiniteNumber(obj, "power", power.power);
    return obj;
}

/// Attach the power block to @p root under "power": the telemetry object on
/// rev2 (`hasPower`), otherwise JSON null (rev1 has no INA226).
void attachPower(cJSON* root, bool hasPower, const PowerDto& power)
{
    if (hasPower) {
        cJSON_AddItemToObject(root, "power", buildPowerObject(power));
    } else {
        cJSON_AddNullToObject(root, "power");
    }
}

}  // namespace

std::string serializeStatus(const SystemStatusDto& status)
{
    cJSON* root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "mode", status.mode.c_str());

    // The wifi block never carries the password (there is no such field).
    cJSON* wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi, "state", status.wifi.state.c_str());
    cJSON_AddNumberToObject(wifi, "rssi", status.wifi.rssi);
    cJSON_AddStringToObject(wifi, "ssid", status.wifi.ssid.c_str());
    cJSON_AddBoolToObject(wifi, "connected", status.wifi.connected);
    cJSON_AddBoolToObject(wifi, "ipAcquired", status.wifi.ipAcquired);
    cJSON_AddStringToObject(wifi, "ip", status.wifi.ip.c_str());
    cJSON_AddItemToObject(root, "wifi", wifi);

    cJSON* time = cJSON_CreateObject();
    cJSON_AddBoolToObject(time, "synced", status.time.synced);
    cJSON_AddNumberToObject(time, "epoch",
                            static_cast<double>(status.time.epoch));
    cJSON_AddStringToObject(time, "local", status.time.local.c_str());
    cJSON_AddNumberToObject(time, "lastSync",
                            static_cast<double>(status.time.lastSync));
    cJSON_AddItemToObject(root, "time", time);

    cJSON_AddNumberToObject(root, "uptimeMs",
                            static_cast<double>(status.uptimeMs));
    cJSON_AddStringToObject(root, "resetReason", status.resetReason.c_str());

    cJSON* firmware = cJSON_CreateObject();
    cJSON_AddStringToObject(firmware, "version", status.firmware.version.c_str());
    cJSON_AddStringToObject(firmware, "project", status.firmware.project.c_str());
    cJSON_AddItemToObject(root, "firmware", firmware);

    cJSON* storage = cJSON_CreateObject();
    cJSON_AddNumberToObject(storage, "totalBytes",
                            static_cast<double>(status.storage.totalBytes));
    cJSON_AddNumberToObject(storage, "usedBytes",
                            static_cast<double>(status.storage.usedBytes));
    addFiniteNumber(storage, "percentUsed", status.storage.percentUsed);
    cJSON_AddItemToObject(root, "storage", storage);

    attachPower(root, status.hasPower, status.power);

    return successBody(root);
}

std::string serializeSensors(const SensorReadingsDto& sensors)
{
    cJSON* root = cJSON_CreateObject();

    cJSON* env = cJSON_CreateObject();
    cJSON_AddBoolToObject(env, "valid", sensors.environmental.valid);
    addFiniteNumber(env, "temperature", sensors.environmental.temperature);
    addFiniteNumber(env, "humidity", sensors.environmental.humidity);
    addFiniteNumber(env, "pressure", sensors.environmental.pressure);
    cJSON_AddItemToObject(root, "environmental", env);

    cJSON* soil = cJSON_CreateObject();
    cJSON_AddBoolToObject(soil, "valid", sensors.soil.valid);
    addFiniteNumber(soil, "moisture", sensors.soil.moisture);
    addFiniteNumber(soil, "temperature", sensors.soil.temperature);
    addFiniteNumber(soil, "humidity", sensors.soil.humidity);
    addFiniteNumber(soil, "ph", sensors.soil.ph);
    addFiniteNumber(soil, "ec", sensors.soil.ec);
    // NPK channels are present only when the sensor reported them (has-flag).
    if (sensors.soil.hasNitrogen) {
        addFiniteNumber(soil, "nitrogen", sensors.soil.nitrogen);
    }
    if (sensors.soil.hasPhosphorus) {
        addFiniteNumber(soil, "phosphorus", sensors.soil.phosphorus);
    }
    if (sensors.soil.hasPotassium) {
        addFiniteNumber(soil, "potassium", sensors.soil.potassium);
    }
    cJSON_AddItemToObject(root, "soil", soil);

    cJSON* level = cJSON_CreateObject();
    cJSON* low = cJSON_CreateObject();
    cJSON_AddBoolToObject(low, "valid", sensors.level.low.valid);
    cJSON_AddBoolToObject(low, "waterPresent", sensors.level.low.waterPresent);
    cJSON_AddItemToObject(level, "low", low);
    cJSON* high = cJSON_CreateObject();
    cJSON_AddBoolToObject(high, "valid", sensors.level.high.valid);
    cJSON_AddBoolToObject(high, "waterPresent", sensors.level.high.waterPresent);
    cJSON_AddItemToObject(level, "high", high);
    cJSON_AddItemToObject(root, "level", level);

    attachPower(root, sensors.hasPower, sensors.power);

    // Top-level timestamp: JSON null when the clock is not set (no bogus 1970).
    if (sensors.hasTimestamp) {
        cJSON_AddNumberToObject(root, "timestamp",
                                static_cast<double>(sensors.timestamp));
    } else {
        cJSON_AddNullToObject(root, "timestamp");
    }

    return successBody(root);
}

std::string serializePower(const PowerDto& power)
{
    // The dedicated GET /power body spreads the telemetry fields to the top
    // level (`{ success, valid, busVoltage, current, power }`).
    return successBody(buildPowerObject(power));
}

std::string serializePowerUnavailable()
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "available", false);
    cJSON_AddNullToObject(root, "power");
    return successBody(root);
}

}  // namespace api
