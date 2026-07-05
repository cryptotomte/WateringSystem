// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ApiRequests.cpp
 * @brief Implementation of the pure API request parsers/validators.
 */

#include "api/ApiRequests.h"

#include <cstddef>
#include <cstdint>
#include <optional>

#include "cJSON.h"

#include "interfaces/IConfigStore.h"

namespace api {

namespace {

/// Outcome of inspecting one optional request field.
enum class FieldCheck {
    Absent,   ///< key not present in the body (leave the optional unset)
    Ok,       ///< key present, correct type and in range (out written)
    Invalid   ///< key present but wrong type or out of range (err written)
};

/// Validate an optional numeric field against [minV, maxV] and store it as a
/// float optional. Absent keys are Ok-to-skip; a non-number or out-of-range
/// value is Invalid with a field-naming error.
FieldCheck checkFloatField(const cJSON* root, const char* key, float minV,
                           float maxV, std::optional<float>& out,
                           std::string& err)
{
    const cJSON* f = cJSON_GetObjectItemCaseSensitive(root, key);
    if (f == nullptr) {
        return FieldCheck::Absent;
    }
    if (!cJSON_IsNumber(f)) {
        err = std::string(key) + " must be a number";
        return FieldCheck::Invalid;
    }
    double v = f->valuedouble;
    if (v < static_cast<double>(minV) || v > static_cast<double>(maxV)) {
        err = std::string(key) + " out of range";
        return FieldCheck::Invalid;
    }
    out = static_cast<float>(v);
    return FieldCheck::Ok;
}

/// Validate an optional numeric field against [minV, maxV] and store it as a
/// uint32 optional. Same conventions as checkFloatField.
FieldCheck checkUintField(const cJSON* root, const char* key, uint32_t minV,
                          uint32_t maxV, std::optional<uint32_t>& out,
                          std::string& err)
{
    const cJSON* f = cJSON_GetObjectItemCaseSensitive(root, key);
    if (f == nullptr) {
        return FieldCheck::Absent;
    }
    if (!cJSON_IsNumber(f)) {
        err = std::string(key) + " must be a number";
        return FieldCheck::Invalid;
    }
    double v = f->valuedouble;
    if (v < static_cast<double>(minV) || v > static_cast<double>(maxV)) {
        err = std::string(key) + " out of range";
        return FieldCheck::Invalid;
    }
    out = static_cast<uint32_t>(v);
    return FieldCheck::Ok;
}

/// Validate an optional boolean field. Absent is Ok-to-skip; a non-bool is
/// Invalid with a field-naming error.
FieldCheck checkBoolField(const cJSON* root, const char* key,
                          std::optional<bool>& out, std::string& err)
{
    const cJSON* f = cJSON_GetObjectItemCaseSensitive(root, key);
    if (f == nullptr) {
        return FieldCheck::Absent;
    }
    if (!cJSON_IsBool(f)) {
        err = std::string(key) + " must be a boolean";
        return FieldCheck::Invalid;
    }
    out = cJSON_IsTrue(f);
    return FieldCheck::Ok;
}

}  // namespace

ConfigSetResult parseConfigSet(const std::string& body)
{
    ConfigSetResult result;

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        result.error = "malformed JSON";
        return result;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        result.error = "body must be a JSON object";
        return result;
    }

    // Parse into a local request; only commit it to the result once EVERY
    // present field validates (all-or-nothing — a single bad field applies
    // nothing).
    ConfigSetRequest req;
    std::string err;
    bool bad = false;

    // Each check returns Invalid on a present-but-wrong field; we stop on the
    // first failure so the error names that field.
    if (!bad && checkFloatField(root, "moistureThresholdLow",
                                IConfigStore::kMoistureThresholdMin,
                                IConfigStore::kMoistureThresholdMax,
                                req.moistureThresholdLow, err) ==
                    FieldCheck::Invalid) {
        bad = true;
    }
    if (!bad && checkFloatField(root, "moistureThresholdHigh",
                                IConfigStore::kMoistureThresholdMin,
                                IConfigStore::kMoistureThresholdMax,
                                req.moistureThresholdHigh, err) ==
                    FieldCheck::Invalid) {
        bad = true;
    }
    if (!bad && checkUintField(root, "wateringDurationS",
                               IConfigStore::kWateringDurationMinS,
                               IConfigStore::kWateringDurationMaxS,
                               req.wateringDurationS, err) ==
                    FieldCheck::Invalid) {
        bad = true;
    }
    if (!bad && checkUintField(root, "minWateringIntervalS",
                               IConfigStore::kMinWateringIntervalFloorS,
                               UINT32_MAX, req.minWateringIntervalS, err) ==
                    FieldCheck::Invalid) {
        bad = true;
    }
    if (!bad && checkBoolField(root, "wateringEnabled", req.wateringEnabled,
                               err) == FieldCheck::Invalid) {
        bad = true;
    }
    if (!bad && checkUintField(root, "sensorReadIntervalMs",
                               IConfigStore::kSensorReadIntervalFloorMs,
                               UINT32_MAX, req.sensorReadIntervalMs, err) ==
                    FieldCheck::Invalid) {
        bad = true;
    }
    if (!bad && checkUintField(root, "dataLogIntervalMs",
                               IConfigStore::kDataLogIntervalFloorMs,
                               UINT32_MAX, req.dataLogIntervalMs, err) ==
                    FieldCheck::Invalid) {
        bad = true;
    }

    cJSON_Delete(root);

    if (bad) {
        // Discard the partially-populated request: nothing is applied.
        result.error = err;
        return result;
    }

    result.request = req;
    result.ok = true;
    return result;
}

PumpCommandResult parsePumpCommand(const std::string& body)
{
    PumpCommandResult result;

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        result.error = "malformed JSON";
        return result;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        result.error = "body must be a JSON object";
        return result;
    }

    const cJSON* action = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (!cJSON_IsString(action) || action->valuestring == nullptr) {
        cJSON_Delete(root);
        result.error = "action must be a string";
        return result;
    }

    std::string verb = action->valuestring;
    PumpCommand cmd;

    if (verb == "stop") {
        cmd.action = PumpAction::Stop;
        // Stop needs no duration; any durationS present is ignored.
        cJSON_Delete(root);
        result.command = cmd;
        result.ok = true;
        return result;
    }

    if (verb == "start" || verb == "run") {
        cmd.action = (verb == "start") ? PumpAction::Start : PumpAction::Run;

        const cJSON* dur = cJSON_GetObjectItemCaseSensitive(root, "durationS");
        if (!cJSON_IsNumber(dur)) {
            cJSON_Delete(root);
            result.error = "durationS must be a number";
            return result;
        }
        double v = dur->valuedouble;
        if (v < static_cast<double>(IConfigStore::kWateringDurationMinS) ||
            v > static_cast<double>(IConfigStore::kWateringDurationMaxS)) {
            cJSON_Delete(root);
            result.error = "durationS out of range";
            return result;
        }
        cmd.durationS = static_cast<uint32_t>(v);

        cJSON_Delete(root);
        result.command = cmd;
        result.ok = true;
        return result;
    }

    cJSON_Delete(root);
    result.error = "unknown action";
    return result;
}

bool namedRangeToWindow(const std::string& range, uint32_t now, uint32_t& t0,
                        uint32_t& t1)
{
    uint32_t span = 0;
    if (range == "1h") {
        span = 3600u;
    } else if (range == "6h") {
        span = 21600u;
    } else if (range == "24h") {
        span = 86400u;
    } else if (range == "7d") {
        span = 604800u;
    } else if (range == "30d") {
        span = 2592000u;
    } else {
        return false;  // unknown range name: leave t0/t1 untouched.
    }

    t1 = now;
    // Clamp the underflow: an unset/small clock must not wrap uint32.
    t0 = (now >= span) ? now - span : 0u;
    return true;
}

std::size_t resolveEventCount(std::optional<int> requested)
{
    constexpr std::size_t kDefault = 50;
    constexpr std::size_t kMax = 200;
    if (!requested.has_value() || *requested <= 0) {
        return kDefault;
    }
    const std::size_t v = static_cast<std::size_t>(*requested);
    return v > kMax ? kMax : v;
}

WindowResult resolveWindow(std::optional<std::string> range,
                           std::optional<uint32_t> start,
                           std::optional<uint32_t> end, uint32_t now)
{
    constexpr uint32_t kDefaultWindowS = 86400;  // last 24 h
    WindowResult out;

    if (range.has_value()) {
        // A named range resolves to an absolute window ending at now; an
        // unknown range name is a client error (ok stays false).
        out.ok = namedRangeToWindow(*range, now, out.t0, out.t1);
        return out;
    }

    if (start.has_value() || end.has_value()) {
        // Explicit window: a missing end defaults to now; a missing start
        // defaults to 24 h before the end (clamped so it can't wrap).
        out.t1 = end.has_value() ? *end : now;
        if (start.has_value()) {
            out.t0 = *start;
        } else {
            out.t0 = out.t1 >= kDefaultWindowS ? out.t1 - kDefaultWindowS : 0u;
        }
        out.ok = true;
        return out;
    }

    // No window given: default to the last 24 h (clamped).
    out.t1 = now;
    out.t0 = now >= kDefaultWindowS ? now - kDefaultWindowS : 0u;
    out.ok = true;
    return out;
}

}  // namespace api
