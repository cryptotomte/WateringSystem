// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ApiRequests.h
 * @brief Pure request-body parsers/validators for the /api/v1/ API (host+target).
 *
 * These functions parse a raw JSON request body into the *Request / *Command
 * DTOs from ApiDtos.h, validating each field against the IConfigStore range
 * constants (the single source of truth for the settable ranges). They are
 * PURE C++ over cJSON (the espressif/cjson managed component, which links on the
 * linux preview target) — NO esp_http_server / esp_* dependency — so parsing and
 * validation are deterministic and host-tested (T012). The thin target ApiServer
 * only forwards the request body here, then applies the result via the Locked*
 * interfaces.
 *
 * Validation conventions (data-model.md):
 *   - Config-set is ALL-OR-NOTHING: any present field that is out of range or the
 *     wrong type fails the whole request (ok=false, an error naming the field)
 *     and NO field is marked to apply. Absent fields stay absent (std::optional).
 *   - Malformed JSON (cJSON_Parse returns NULL) is a rejection, never a crash.
 *   - No function throws: every outcome is reported through the result struct.
 */

#ifndef WATERINGSYSTEM_API_APIREQUESTS_H
#define WATERINGSYSTEM_API_APIREQUESTS_H

#include <string>

#include "api/ApiDtos.h"

namespace api {

/// Result of parsing a config-set body: the parsed subset plus an ok/error
/// verdict. When ok is false, request carries NO fields to apply (all-or-nothing)
/// and error names the offending field or reason.
struct ConfigSetResult {
    ConfigSetRequest request;  ///< populated subset; empty when ok is false
    bool ok = false;
    std::string error;         ///< human-readable reason when ok is false
};

/// Result of parsing a pump-command body: the parsed command plus an ok/error
/// verdict. When ok is false, command is unspecified and error names the reason.
struct PumpCommandResult {
    PumpCommand command;       ///< valid only when ok is true
    bool ok = false;
    std::string error;         ///< human-readable reason when ok is false
};

/**
 * @brief Parse a POST config body into a validated ConfigSetRequest.
 *
 * Accepts any subset of the settable config fields
 * (moistureThresholdLow/High, wateringDurationS, minWateringIntervalS,
 * wateringEnabled, sensorReadIntervalMs, dataLogIntervalMs). Each PRESENT field
 * is range-/type-checked against the IConfigStore constants. All-or-nothing: if
 * any present field is out of range or the wrong type, ok is false, error names
 * the field, and the returned request marks nothing to apply. Malformed JSON is
 * a rejection.
 */
ConfigSetResult parseConfigSet(const std::string& body);

/**
 * @brief Parse a POST pump-command body into a validated PumpCommand.
 *
 * `action` must be one of "start", "run", "stop". For start/run, `durationS`
 * is required and must be in 1..300 s (IConfigStore::kWateringDuration*S). Stop
 * needs no duration. A bad action, an out-of-range/missing duration, a wrong
 * type or malformed JSON is a rejection with an explanatory error.
 */
PumpCommandResult parsePumpCommand(const std::string& body);

}  // namespace api

#endif /* WATERINGSYSTEM_API_APIREQUESTS_H */
