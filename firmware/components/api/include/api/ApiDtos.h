// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ApiDtos.h
 * @brief Plain in-memory DTOs for the pure serialize/parse layer (host+target).
 *
 * These are POD structs (data-model.md). The thin target handler reads the
 * `Locked*` interfaces + wifi/time/netif/storage state into these structs, then
 * hands them to the pure ApiSerialize functions — so serialization is
 * deterministic and host-tested. Request bodies are parsed by ApiRequests into
 * the *Request/*Command structs here.
 *
 * NO IDF and NO cJSON includes: DTOs are pure data, carrying no serialization
 * logic. `valid`/`has*` flags and `std::optional` fields express presence
 * explicitly (a `valid=false` sub-reading is a distinct state, never a bogus
 * value); NPK channels are present-flagged because the sensor reports them only
 * when non-negative.
 */

#ifndef WATERINGSYSTEM_API_APIDTOS_H
#define WATERINGSYSTEM_API_APIDTOS_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace api {

// ---------------------------------------------------------------------------
// System status (GET /api/v1/status)
// ---------------------------------------------------------------------------

/// WiFi status block; the password is NEVER represented here or serialized.
struct WifiStatusDto {
    std::string state;          ///< WifiState name (e.g. "connected")
    int8_t rssi = 0;            ///< last RSSI dBm; meaningful only when connected
    std::string ssid;           ///< associated SSID (may be empty)
    bool connected = false;     ///< true in the Connected state
    bool ipAcquired = false;    ///< true after GotIp
    std::string ip;             ///< device IP (from esp_netif); empty if none
};

/// Wall-clock/time-sync block. Reflects the not-set state per PR-08.
struct TimeStatusDto {
    bool synced = false;        ///< true once a plausible epoch was obtained
    int64_t epoch = 0;          ///< seconds since epoch (0 while not set)
    std::string local;          ///< Swedish local-time string, empty if not set
    int64_t lastSync = 0;       ///< epoch of last successful SNTP sync (0 = never)
};

/// Firmware identity block (from esp_app_desc on target).
struct FirmwareDto {
    std::string version;        ///< project version string
    std::string project;        ///< project name
};

/// Filesystem usage block (from IDataStorage stats).
struct StorageStatsDto {
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    float percentUsed = 0.0f;
};

/// Pump power telemetry (rev2 INA226). Absent (serialized null) on rev1.
struct PowerDto {
    bool valid = false;         ///< derived from the cached getter's validity
    float busVoltage = 0.0f;    ///< volts
    float current = 0.0f;       ///< amps (signed)
    float power = 0.0f;         ///< watts
};

/// Full system status DTO. `power` present only when `hasPower` (rev2).
struct SystemStatusDto {
    std::string mode;           ///< "manual"|"automatic" (from wateringEnabled)
    WifiStatusDto wifi;
    TimeStatusDto time;
    uint64_t uptimeMs = 0;
    std::string resetReason;    ///< resetReasonName() of the last boot cause
    FirmwareDto firmware;
    StorageStatsDto storage;
    bool hasPower = false;      ///< true on rev2 (power block present)
    PowerDto power;
};

// ---------------------------------------------------------------------------
// Sensor readings (GET /api/v1/sensors)
// ---------------------------------------------------------------------------

/// Environmental sub-reading (BME280, refreshed by the 5 s sensor task).
struct EnvironmentalDto {
    bool valid = false;
    float temperature = 0.0f;   ///< degrees Celsius
    float humidity = 0.0f;      ///< %RH
    float pressure = 0.0f;      ///< hPa
};

/// Soil sub-reading (RS485). NPK channels are included only when present.
/// `valid` stays false until PR-11 adds a periodic reader (last-good/NaN).
struct SoilDto {
    bool valid = false;
    float moisture = 0.0f;      ///< %
    float temperature = 0.0f;   ///< degrees Celsius
    float humidity = 0.0f;      ///< %RH
    float ph = 0.0f;
    float ec = 0.0f;            ///< electrical conductivity
    bool hasNitrogen = false;
    float nitrogen = 0.0f;
    bool hasPhosphorus = false;
    float phosphorus = 0.0f;
    bool hasPotassium = false;
    float potassium = 0.0f;
};

/// One reservoir level mark (low or high); `waterPresent` valid only if `valid`.
struct LevelMarkDto {
    bool valid = false;
    bool waterPresent = false;
};

/// Reservoir level block (two independent marks, refreshed at 10 Hz).
struct LevelDto {
    LevelMarkDto low;
    LevelMarkDto high;
};

/// Full sensor-readings DTO. `power` present only when `hasPower` (rev2).
struct SensorReadingsDto {
    EnvironmentalDto environmental;
    SoilDto soil;
    LevelDto level;
    bool hasPower = false;      ///< true on rev2 (power block present)
    PowerDto power;
    bool hasTimestamp = false;  ///< false = clock not set (no bogus 1970 epoch)
    int64_t timestamp = 0;      ///< epoch seconds when hasTimestamp
};

// ---------------------------------------------------------------------------
// Pumps (GET /api/v1/pumps, POST /api/v1/pumps/{name})
// ---------------------------------------------------------------------------

/// One pump's status. The list is capability-enumerated (BOARD_HAS_RESERVOIR_PUMP).
struct PumpDto {
    std::string name;                   ///< "plant"|"reservoir"
    bool running = false;
    uint32_t currentRunTimeMs = 0;      ///< elapsed in the current run
    uint32_t accumulatedRunTimeMs = 0;  ///< lifetime total
    std::string lastStopReason;         ///< reason string of the last stop
};

/// Pump command action parsed from a request body.
enum class PumpAction {
    Start,  ///< begin a timed run of durationS (rejected if already running)
    Run,    ///< synonym for start with durationS
    Stop    ///< stop; success no-op on an already-stopped pump
};

/// Parsed pump command (data-model §PumpCommand). durationS is 1..300 for
/// start/run; absent for stop.
struct PumpCommand {
    PumpAction action = PumpAction::Stop;
    std::optional<uint32_t> durationS;
};

// ---------------------------------------------------------------------------
// Config (GET/POST /api/v1/config)
// ---------------------------------------------------------------------------

/// Current configuration (GET). NEVER carries the wifi password.
struct ConfigDto {
    float moistureThresholdLow = 0.0f;
    float moistureThresholdHigh = 0.0f;
    uint32_t wateringDurationS = 0;
    uint32_t minWateringIntervalS = 0;
    bool wateringEnabled = false;
    uint32_t sensorReadIntervalMs = 0;
    uint32_t dataLogIntervalMs = 0;
};

/// Parsed config-set body (POST): any subset of settable fields. Validation is
/// all-or-nothing (ApiRequests) against the IConfigStore range constants.
struct ConfigSetRequest {
    std::optional<float> moistureThresholdLow;
    std::optional<float> moistureThresholdHigh;
    std::optional<uint32_t> wateringDurationS;
    std::optional<uint32_t> minWateringIntervalS;
    std::optional<bool> wateringEnabled;
    std::optional<uint32_t> sensorReadIntervalMs;
    std::optional<uint32_t> dataLogIntervalMs;
};

// ---------------------------------------------------------------------------
// History (GET /api/v1/history)
// ---------------------------------------------------------------------------

/// Parsed history query. Either a named `range` OR explicit `start`/`end`;
/// default window is the last 24 h when none is given.
struct HistoryQuery {
    std::string metric;
    std::optional<std::string> reading;
    std::optional<std::string> range;  ///< named: 1h/6h/24h/7d/30d
    std::optional<int64_t> start;      ///< explicit window start (epoch)
    std::optional<int64_t> end;        ///< explicit window end (epoch)
};

/// History result: aligned timestamps[]/values[] plus an echo of the query.
/// Empty arrays for a range with no data (not an error).
struct HistorySeries {
    std::string metric;
    std::optional<std::string> reading;
    int64_t start = 0;                 ///< resolved window start (epoch)
    int64_t end = 0;                   ///< resolved window end (epoch)
    std::vector<int64_t> timestamps;
    std::vector<float> values;         ///< aligned 1:1 with timestamps
};

// ---------------------------------------------------------------------------
// Events (GET /api/v1/events)
// ---------------------------------------------------------------------------

/// One event log entry (newest-first from IDataStorage::getEvents).
struct EventDto {
    int64_t epoch = 0;
    int category = 0;                       ///< raw category id
    std::optional<std::string> categoryName;///< human name, when known
    std::string detail;
};

// ---------------------------------------------------------------------------
// Self-test (POST /api/v1/selftest)
// ---------------------------------------------------------------------------

/// One self-test check outcome.
struct SelfTestCheckDto {
    std::string name;
    bool ok = false;
    std::string detail;
};

/// Structured self-test result: overall pass/fail + per-check outcomes.
struct SelfTestResultDto {
    bool overall = false;
    std::vector<SelfTestCheckDto> checks;
};

}  // namespace api

#endif /* WATERINGSYSTEM_API_APIDTOS_H */
