// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ApiSerialize.h
 * @brief Pure DTO -> JSON serializers for the /api/v1/ API (host+target).
 *
 * These functions take the plain DTOs from ApiDtos.h and render the success
 * envelope body (`{ "success": true, ...payload }`) for the read endpoints.
 * They are PURE C++ over cJSON (the espressif/cjson managed component, which
 * links on the linux preview target) — NO esp_http_server / esp_* dependency —
 * so the exact byte output is deterministic and host-tested (T007). The thin
 * target ApiServer only reads the Locked* interfaces into these DTOs and sends
 * the returned string.
 *
 * Presence and validity conventions (data-model.md):
 *   - A sub-reading with `valid=false` is still serialized (its `valid` flag is
 *     the signal); a not-yet-valid soil reading is a distinct state, never a
 *     bogus value.
 *   - Non-finite floats (NaN/Inf — e.g. a last-good placeholder before the
 *     first read) are emitted as JSON `null`, never a misleading 0.
 *   - NPK channels are emitted only when their `has*` flag is set (the sensor
 *     reports them only when non-negative).
 *   - The wifi password is never represented in any DTO or serialized field.
 */

#ifndef WATERINGSYSTEM_API_APISERIALIZE_H
#define WATERINGSYSTEM_API_APISERIALIZE_H

#include <string>
#include <vector>

#include "api/ApiDtos.h"

namespace api {

/**
 * @brief Serialize a SystemStatusDto to the GET /api/v1/status success body.
 *
 * Emits `mode`, `wifi`, `time`, `uptimeMs`, `resetReason`, `firmware`,
 * `storage`, and `power` (the power object on rev2 when `hasPower`, otherwise
 * JSON null). Key order is deterministic.
 */
std::string serializeStatus(const SystemStatusDto& status);

/**
 * @brief Serialize a SensorReadingsDto to the GET /api/v1/sensors success body.
 *
 * Emits `environmental`, `soil` (NPK channels only when their has-flag is set),
 * `level` (two independent marks), `power` (rev2 object when `hasPower`, else
 * JSON null), and a top-level `timestamp` (JSON null when the clock is not set,
 * so no bogus 1970 epoch). Key order is deterministic.
 */
std::string serializeSensors(const SensorReadingsDto& sensors);

/**
 * @brief Serialize a PowerDto to the GET /api/v1/power success body (rev2).
 *
 * Emits `valid`, `busVoltage`, `current`, `power` at the top level (non-finite
 * last-good placeholders become JSON null).
 */
std::string serializePower(const PowerDto& power);

/**
 * @brief The GET /api/v1/power not-available body for boards without an INA226.
 *
 * A success envelope carrying `available: false` and `power: null` — the
 * board-capability shape for rev1 (which contains no INA226 code).
 */
std::string serializePowerUnavailable();

/**
 * @brief Serialize one PumpDto to the POST pumps command success body.
 *
 * Spreads `name`, `running`, `currentRunTimeMs`, `accumulatedRunTimeMs` and
 * `lastStopReason` (a string) to the top level via the success envelope. Used
 * for the resulting pump state after a command.
 */
std::string serializePump(const PumpDto& pump);

/**
 * @brief Serialize a list of PumpDto to the GET pumps success body.
 *
 * Emits `{ success, pumps: [ { name, running, currentRunTimeMs,
 * accumulatedRunTimeMs, lastStopReason }, ... ] }`. The list is
 * capability-enumerated by the caller (rev1 plant+reservoir, rev2 plant).
 */
std::string serializePumpList(const std::vector<PumpDto>& pumps);

/**
 * @brief Serialize a ConfigDto to the GET config success body.
 *
 * Spreads every settable config field (moistureThresholdLow/High,
 * wateringDurationS, minWateringIntervalS, wateringEnabled,
 * sensorReadIntervalMs, dataLogIntervalMs) to the top level. The wifi password
 * is NEVER present (the DTO carries no such field).
 */
std::string serializeConfig(const ConfigDto& config);

}  // namespace api

#endif /* WATERINGSYSTEM_API_APISERIALIZE_H */
