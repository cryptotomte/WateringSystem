// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ApiServer.cpp
 * @brief /api/v1/ HTTP server implementation (target-only).
 *
 * See ApiServer.h and specs/009-http-server-api-v1/contracts/api-server.md.
 * Excluded from the linux build (esp_http_server has no host port). Models the
 * ProvisioningPortal precedent exactly: the server handle is the only IDF type
 * that touches the class and is kept opaque in the header; the route handlers
 * are file-local functions here that recover the ApiServer from req->user_ctx
 * and call its public response builders. All response BYTES come from the pure,
 * host-tested serializers — this file only reads the cached getters into DTOs
 * and does the httpd_resp_* plumbing.
 *
 * QUIRK 5: handlers use ONLY non-blocking cached getters through the Locked*
 * wrappers — never read()/isAvailable() — so a handler can never stall on the
 * I2C/RS485 bus and delay another reader. The wifi password is never read or
 * serialized (FR-004).
 */

#include "api/ApiServer.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"

#include "api/ApiDtos.h"
#include "api/ApiEnvelope.h"
#include "api/ApiRequests.h"
#include "api/ApiRoutes.h"
#include "api/ApiSerialize.h"
#include "api/ApiStatic.h"
#include "events/EventLogger.h"
#include "network/WifiState.h"
#include "storage/StorageMount.h"
#include "time/TimeService.h"

namespace api {

namespace {

const char* TAG = "api_server";

/// Handler cap: the full /api/v1/ route set (registered below) plus headroom.
constexpr uint16_t kMaxUriHandlers = 16;

/// WifiState -> stable lowercase word for the status DTO (matches the diag
/// console `wifi` vocabulary). Total over the enum.
const char* wifiStateName(WifiState state)
{
    switch (state) {
    case WifiState::Provisioning:
        return "provisioning";
    case WifiState::Connecting:
        return "connecting";
    case WifiState::Connected:
        return "connected";
    case WifiState::Reconnecting:
        return "reconnecting";
    case WifiState::ReconnectPaused:
        return "reconnect_paused";
    }
    return "unknown";
}

/// Cached-getter validity: the last operation succeeded AND the primary value is
/// a real reading (finite). Sensor getters return a self-announcing NaN before
/// the first successful read and hold the last-good value after a failed one, so
/// pairing getLastError()==0 with finiteness (the interface's own
/// presence/gating signals, never value plausibility) yields an honest `valid`
/// with no bus access.
bool cachedValid(int lastError, float primaryValue)
{
    return lastError == 0 && std::isfinite(primaryValue);
}

/// Send a ready JSON body with the HTTP status line mapped from @p status.
esp_err_t sendJson(httpd_req_t* req, ApiStatus status, const std::string& body)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, statusLine(status));
    return httpd_resp_sendstr(req, body.c_str());
}

/// Recover the ApiServer from the request; null-guarded (500 on misconfig).
ApiServer* self(httpd_req_t* req)
{
    return static_cast<ApiServer*>(req->user_ctx);
}

esp_err_t statusHandler(httpd_req_t* req)
{
    ApiServer* server = self(req);
    if (server == nullptr) {
        return sendJson(req, ApiStatus::InternalError,
                        errorBody("server misconfigured"));
    }
    return sendJson(req, ApiStatus::Ok, server->buildStatusBody());
}

esp_err_t sensorsHandler(httpd_req_t* req)
{
    ApiServer* server = self(req);
    if (server == nullptr) {
        return sendJson(req, ApiStatus::InternalError,
                        errorBody("server misconfigured"));
    }
    return sendJson(req, ApiStatus::Ok, server->buildSensorsBody());
}

esp_err_t powerHandler(httpd_req_t* req)
{
    ApiServer* server = self(req);
    if (server == nullptr) {
        return sendJson(req, ApiStatus::InternalError,
                        errorBody("server misconfigured"));
    }
    return sendJson(req, ApiStatus::Ok, server->buildPowerBody());
}

// Serve a gzipped static asset from littlefs. Registered as the GET /* catch-all
// AFTER the exact /api/v1/ routes, so those match first; any other GET falls
// here. Assets are stored pre-gzipped at <base>/<path>.gz (feature 010). File
// I/O only, off the watering buses (same isolation class as history/events).
esp_err_t staticFileHandler(httpd_req_t* req)
{
    const std::optional<std::string> rel = sanitizeAssetPath(req->uri);
    if (!rel.has_value()) {
        return sendJson(req, ApiStatus::NotFound, errorBody("not found"));
    }
    const std::string full =
        std::string(StorageMount::kBasePath) + "/" + *rel + ".gz";
    FILE* f = std::fopen(full.c_str(), "rb");
    if (f == nullptr) {
        return sendJson(req, ApiStatus::NotFound, errorBody("not found"));
    }
    httpd_resp_set_type(req, contentTypeForPath(*rel));
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    char buf[1024];
    size_t n;
    esp_err_t sendErr = ESP_OK;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        sendErr = httpd_resp_send_chunk(req, buf, n);
        if (sendErr != ESP_OK) {
            break;
        }
    }
    std::fclose(f);
    if (sendErr != ESP_OK) {
        return sendErr;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

/// Global 404: any unregistered route answers the JSON error envelope (parity
/// §4 — the API never returns HTML/plain text). Registered via
/// httpd_register_err_handler so it does not interfere with exact route
/// matching. Error handlers receive no user_ctx (the body is pure).
esp_err_t notFoundHandler(httpd_req_t* req, httpd_err_code_t /*error*/)
{
    // Returning ESP_OK keeps the connection usable after the 404.
    return sendJson(req, ApiStatus::NotFound, notFoundBody());
}

/// Global 405: a known path reached with an unsupported method answers the JSON
/// error envelope (parity §4 — never the default HTML page). 405 has no
/// ApiStatus entry (it never originates from a builder), so the status line is
/// set directly here.
esp_err_t methodNotAllowedHandler(httpd_req_t* req, httpd_err_code_t /*error*/)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "405 Method Not Allowed");
    return httpd_resp_sendstr(req, errorBody("method not allowed").c_str());
}

/// POST body cap: pump/config bodies are a handful of small fields; anything
/// larger is a misuse or a flood and is rejected before any allocation. The
/// stack buffer holds the whole body plus a NUL terminator.
constexpr int kMaxBodyLen = 512;

/// StopReason -> stable lowercase word for the pump DTO (same vocabulary as the
/// diag console `pump status`). Total over the enum.
const char* stopReasonName(StopReason reason)
{
    switch (reason) {
    case StopReason::Commanded:
        return "commanded";
    case StopReason::DurationElapsed:
        return "duration_elapsed";
    case StopReason::MaxRuntimeForced:
        return "max_runtime_forced";
    case StopReason::None:
        return "none";
    }
    return "none";
}

/// Snapshot one pump into a PumpDto through its NON-BLOCKING status getters
/// (QUIRK 5). The int64 runtime counters are clamped into the DTO's uint32
/// fields (a pump run never approaches 2^32 ms given the 300 s cap).
PumpDto makePumpDto(IWaterPump& pump)
{
    PumpDto dto;
    dto.name = pump.getName();
    dto.running = pump.isRunning();
    dto.currentRunTimeMs = static_cast<uint32_t>(pump.getCurrentRunTimeMs());
    dto.accumulatedRunTimeMs =
        static_cast<uint32_t>(pump.getAccumulatedRunTimeMs());
    dto.lastStopReason = stopReasonName(pump.getLastStopReason());
    return dto;
}

/// Read the whole request body into @p out with a hard cap (kMaxBodyLen).
/// Returns true on success; on false, @p err carries the reason (an over-cap
/// body or a socket receive error) — both map to a 400 by the caller.
bool readRequestBody(httpd_req_t* req, std::string& out, std::string& err)
{
    if (req->content_len >= kMaxBodyLen) {
        err = "request body too large";
        return false;
    }
    char buf[kMaxBodyLen];
    int total = 0;
    int r;
    while ((r = httpd_req_recv(req, buf + total, sizeof(buf) - 1 - total)) > 0) {
        total += r;
    }
    if (r < 0) {
        err = "request body read error";
        return false;
    }
    buf[total] = '\0';
    out.assign(buf, static_cast<size_t>(total));
    return true;
}

esp_err_t pumpsListHandler(httpd_req_t* req)
{
    ApiServer* server = self(req);
    if (server == nullptr) {
        return sendJson(req, ApiStatus::InternalError,
                        errorBody("server misconfigured"));
    }
    return sendJson(req, ApiStatus::Ok, server->buildPumpsBody());
}

esp_err_t pumpCommandHandler(httpd_req_t* req)
{
    ApiServer* server = self(req);
    if (server == nullptr) {
        return sendJson(req, ApiStatus::InternalError,
                        errorBody("server misconfigured"));
    }

    // Pump name = the trailing segment after the command prefix; drop any query
    // string first. An empty/foreign name falls through to applyPumpCommand's
    // 404 (unknown pump).
    std::string uri(req->uri);
    const std::string::size_type q = uri.find('?');
    if (q != std::string::npos) {
        uri.erase(q);
    }
    std::string name;
    const size_t prefixLen = std::strlen(kPumpCommandPrefix);
    if (uri.size() >= prefixLen &&
        uri.compare(0, prefixLen, kPumpCommandPrefix) == 0) {
        name = uri.substr(prefixLen);
    }

    std::string body;
    std::string readErr;
    if (!readRequestBody(req, body, readErr)) {
        return sendJson(req, ApiStatus::BadRequest, errorBody(readErr));
    }

    const ApiResponse resp = server->applyPumpCommand(name, body);
    return sendJson(req, resp.status, resp.body);
}

esp_err_t configGetHandler(httpd_req_t* req)
{
    ApiServer* server = self(req);
    if (server == nullptr) {
        return sendJson(req, ApiStatus::InternalError,
                        errorBody("server misconfigured"));
    }
    return sendJson(req, ApiStatus::Ok, server->buildConfigBody());
}

esp_err_t configSetHandler(httpd_req_t* req)
{
    ApiServer* server = self(req);
    if (server == nullptr) {
        return sendJson(req, ApiStatus::InternalError,
                        errorBody("server misconfigured"));
    }

    std::string body;
    std::string readErr;
    if (!readRequestBody(req, body, readErr)) {
        return sendJson(req, ApiStatus::BadRequest, errorBody(readErr));
    }

    const ApiResponse resp = server->applyConfigSet(body);
    return sendJson(req, resp.status, resp.body);
}

/// URL-query buffer cap: the v1 query strings are a few short key=value pairs;
/// a longer query is truncated to this bound (no unbounded stack allocation).
constexpr size_t kMaxQueryLen = 256;

/// Per-value cap for a single query parameter (metric names / short ints).
constexpr size_t kMaxQueryValueLen = 64;

/// Read one URL query parameter into @p out. Returns true when @p key is
/// present (its decoded value copied to @p out), false when there is no query
/// string, the key is absent, or the value overflows the value buffer.
bool queryParam(httpd_req_t* req, const char* key, std::string& out)
{
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen <= 1) {
        return false;  // no query string
    }
    if (qlen > kMaxQueryLen) {
        qlen = kMaxQueryLen;
    }
    char q[kMaxQueryLen];
    if (httpd_req_get_url_query_str(req, q, qlen) != ESP_OK) {
        return false;
    }
    char val[kMaxQueryValueLen];
    if (httpd_query_key_value(q, key, val, sizeof val) != ESP_OK) {
        return false;
    }
    out.assign(val);
    return true;
}

/// Parse a base-10 epoch string into @p out; returns true only on a fully
/// consumed, non-negative value (a malformed value leaves @p out untouched).
bool parseEpoch(const std::string& s, int64_t& out)
{
    if (s.empty()) {
        return false;
    }
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || v < 0) {
        return false;
    }
    out = static_cast<int64_t>(v);
    return true;
}

/// Map a stored event category id to its stable lowercase name, or nullptr for
/// an unknown category (the DTO then omits the categoryName echo). Mirrors the
/// IDataStorage category constants (same vocabulary as the event logger).
const char* eventCategoryName(int category)
{
    switch (category) {
    case IDataStorage::kCategoryPump:
        return "pump";
    case IDataStorage::kCategoryFailsafe:
        return "failsafe";
    case IDataStorage::kCategoryConnectivity:
        return "connectivity";
    case IDataStorage::kCategoryOta:
        return "ota";
    case IDataStorage::kCategoryReset:
        return "reset";
    default:
        return nullptr;
    }
}

esp_err_t historyHandler(httpd_req_t* req)
{
    ApiServer* server = self(req);
    if (server == nullptr) {
        return sendJson(req, ApiStatus::InternalError,
                        errorBody("server misconfigured"));
    }

    // Extract the query parameters; validation + window resolution live in the
    // pure-ish builder (buildHistoryResponse) so the handler is thin.
    HistoryQuery query;
    std::string value;
    if (queryParam(req, "metric", value)) {
        query.metric = value;
    }
    if (queryParam(req, "reading", value)) {
        query.reading = value;
    }
    if (queryParam(req, "range", value)) {
        query.range = value;
    }
    // A present-but-unparseable start/end is a client error (400), not a silent
    // default; an absent parameter simply stays unset (default window).
    int64_t epoch = 0;
    if (queryParam(req, "start", value)) {
        if (!parseEpoch(value, epoch)) {
            return sendJson(req, ApiStatus::BadRequest, errorBody("invalid start"));
        }
        query.start = epoch;
    }
    if (queryParam(req, "end", value)) {
        if (!parseEpoch(value, epoch)) {
            return sendJson(req, ApiStatus::BadRequest, errorBody("invalid end"));
        }
        query.end = epoch;
    }

    const ApiResponse resp = server->buildHistoryResponse(query);
    return sendJson(req, resp.status, resp.body);
}

esp_err_t eventsHandler(httpd_req_t* req)
{
    ApiServer* server = self(req);
    if (server == nullptr) {
        return sendJson(req, ApiStatus::InternalError,
                        errorBody("server misconfigured"));
    }

    // A present-but-unparseable count is a client error (400); a parseable but
    // non-positive count defaults (resolveEventCount clamps 1..200, absent->50).
    std::optional<int> requestedCount;
    std::string value;
    if (queryParam(req, "count", value)) {
        char* end = nullptr;
        const long v = std::strtol(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != '\0') {
            return sendJson(req, ApiStatus::BadRequest, errorBody("invalid count"));
        }
        requestedCount = static_cast<int>(v);
    }
    const std::size_t count = resolveEventCount(requestedCount);
    return sendJson(req, ApiStatus::Ok, server->buildEventsBody(count));
}

esp_err_t selfTestHandler(httpd_req_t* req)
{
    ApiServer* server = self(req);
    if (server == nullptr) {
        return sendJson(req, ApiStatus::InternalError,
                        errorBody("server misconfigured"));
    }
    // The one handler that performs a real (bounded) bus read — see
    // buildSelfTestBody: an explicit diagnostic, off the watering critical path.
    return sendJson(req, ApiStatus::Ok, server->buildSelfTestBody());
}

esp_err_t otaHandler(httpd_req_t* req)
{
    // Contract stub: PR-13 implements the OTA execution. Until then the route
    // exists (so it is documented/enumerable) but answers a fixed 501.
    return sendJson(req, ApiStatus::NotImplemented,
                    errorBody("OTA not implemented"));
}

}  // namespace

ApiServer::~ApiServer()
{
    stop();
}

std::string ApiServer::deviceIp() const
{
    esp_netif_t* nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (nif != nullptr && esp_netif_get_ip_info(nif, &ip_info) == ESP_OK) {
        char buf[16];
        esp_ip4addr_ntoa(&ip_info.ip, buf, sizeof buf);
        return std::string(buf);
    }
    return std::string();
}

std::string ApiServer::buildStatusBody()
{
    SystemStatusDto dto;

    // Mode is the automatic-watering flag (parity: manual == automatic OFF).
    dto.mode = config_.getWateringEnabled() ? "automatic" : "manual";

    // Wifi: one consistent snapshot; the SSID is not a secret (the password is
    // never represented). The IP comes from esp_netif, target-only.
    const WifiConnectionSnapshot snap = wifi_.snapshot();
    dto.wifi.state = wifiStateName(snap.state);
    dto.wifi.rssi = snap.rssi;
    dto.wifi.ssid = config_.getWifiSsid();
    dto.wifi.connected = (snap.state == WifiState::Connected);
    dto.wifi.ipAcquired = snap.ipAcquired;
    dto.wifi.ip = deviceIp();

    // Time: a not-set clock reports synced=false with no bogus 1970 epoch/local.
    dto.time.synced = wallClock_.isTimeSet();
    if (dto.time.synced) {
        const uint32_t epoch = wallClock_.nowEpoch();
        dto.time.epoch = static_cast<int64_t>(epoch);
        dto.time.local = TimeService::formatLocal(epoch);
    }
    dto.time.lastSync = static_cast<int64_t>(sntp_.status().lastSyncEpoch);

    dto.uptimeMs = static_cast<uint64_t>(uptime_.nowMs());
    dto.resetReason = resetReasonName(static_cast<int>(esp_reset_reason()));

    const esp_app_desc_t* desc = esp_app_get_description();
    if (desc != nullptr) {
        dto.firmware.version = desc->version;
        dto.firmware.project = desc->project_name;
    }

    const StorageStats stats = storage_.getStorageStats();
    dto.storage.totalBytes = stats.totalBytes;
    dto.storage.usedBytes = stats.usedBytes;
    dto.storage.percentUsed =
        stats.totalBytes != 0
            ? (100.0f * static_cast<float>(stats.usedBytes) /
               static_cast<float>(stats.totalBytes))
            : 0.0f;

#if BOARD_HAS_INA226
    dto.hasPower = true;
    dto.power.busVoltage = power_.getBusVoltage();
    dto.power.current = power_.getCurrent();
    dto.power.power = power_.getPower();
    dto.power.valid = cachedValid(power_.getLastError(), dto.power.busVoltage);
#endif

    return serializeStatus(dto);
}

std::string ApiServer::buildSensorsBody()
{
    SensorReadingsDto dto;

    // Environmental: kept fresh by the 5 s sensor task's read().
    dto.environmental.temperature = env_.getTemperature();
    dto.environmental.humidity = env_.getHumidity();
    dto.environmental.pressure = env_.getPressure();
    dto.environmental.valid =
        cachedValid(env_.getLastError(), dto.environmental.temperature);

    // Soil: no periodic reader lands until PR-11, so the cached values are still
    // NaN placeholders and `valid` is honestly false (contract: soil valid=false
    // until PR-11). NPK channels are present-flagged: emitted only when the
    // sensor reported a non-negative value.
    dto.soil.moisture = soil_.getMoisture();
    dto.soil.temperature = soil_.getTemperature();
    dto.soil.humidity = soil_.getHumidity();
    dto.soil.ph = soil_.getPH();
    dto.soil.ec = soil_.getEC();
    dto.soil.valid = cachedValid(soil_.getLastError(), dto.soil.moisture);
    const float nitrogen = soil_.getNitrogen();
    dto.soil.hasNitrogen = std::isfinite(nitrogen) && nitrogen >= 0.0f;
    dto.soil.nitrogen = nitrogen;
    const float phosphorus = soil_.getPhosphorus();
    dto.soil.hasPhosphorus = std::isfinite(phosphorus) && phosphorus >= 0.0f;
    dto.soil.phosphorus = phosphorus;
    const float potassium = soil_.getPotassium();
    dto.soil.hasPotassium = std::isfinite(potassium) && potassium >= 0.0f;
    dto.soil.potassium = potassium;

    // Level: kept fresh by the 10 Hz main-loop update(); isWaterPresent() is
    // meaningful only while isValid() (a not-yet-valid mark is never wet/dry).
    dto.level.low.valid = levelLow_.isValid();
    dto.level.low.waterPresent = levelLow_.isWaterPresent();
    dto.level.high.valid = levelHigh_.isValid();
    dto.level.high.waterPresent = levelHigh_.isWaterPresent();

#if BOARD_HAS_INA226
    dto.hasPower = true;
    dto.power.busVoltage = power_.getBusVoltage();
    dto.power.current = power_.getCurrent();
    dto.power.power = power_.getPower();
    dto.power.valid = cachedValid(power_.getLastError(), dto.power.busVoltage);
#endif

    // Top-level timestamp: JSON null when the clock is not set (no bogus 1970).
    if (wallClock_.isTimeSet()) {
        dto.hasTimestamp = true;
        dto.timestamp = static_cast<int64_t>(wallClock_.nowEpoch());
    }

    return serializeSensors(dto);
}

std::string ApiServer::buildPowerBody()
{
#if BOARD_HAS_INA226
    PowerDto power;
    power.busVoltage = power_.getBusVoltage();
    power.current = power_.getCurrent();
    power.power = power_.getPower();
    power.valid = cachedValid(power_.getLastError(), power.busVoltage);
    return serializePower(power);
#else
    // rev1 has no INA226 code at all: the board-capability not-available shape.
    return serializePowerUnavailable();
#endif
}

IWaterPump* ApiServer::pumpByName(const std::string& name)
{
    if (name == "plant") {
        return &plantPump_;
    }
#if BOARD_HAS_RESERVOIR_PUMP
    if (name == "reservoir") {
        return &reservoirPump_;
    }
#endif
    return nullptr;
}

std::string ApiServer::buildPumpsBody()
{
    // Capability-enumerated: the reservoir pump exists on rev1 only.
    std::vector<PumpDto> pumps;
    pumps.push_back(makePumpDto(plantPump_));
#if BOARD_HAS_RESERVOIR_PUMP
    pumps.push_back(makePumpDto(reservoirPump_));
#endif
    return serializePumpList(pumps);
}

ApiResponse ApiServer::applyPumpCommand(const std::string& name,
                                        const std::string& body)
{
    IWaterPump* pump = pumpByName(name);
    if (pump == nullptr) {
        return {ApiStatus::NotFound, errorBody("unknown pump")};
    }

    const PumpCommandResult parsed = parsePumpCommand(body);
    if (!parsed.ok) {
        return {ApiStatus::BadRequest, errorBody(parsed.error)};
    }

    switch (parsed.command.action) {
    case PumpAction::Start:
    case PumpAction::Run: {
        // durationS is guaranteed present and in 1..300 by the pure parser; the
        // pump's own runFor() re-enforces the hard cap and the no-restart rule —
        // the server makes no watering decision. A false result here means the
        // pump is already running (its clock is NOT restarted): an explicit 409.
        const int durationS = static_cast<int>(parsed.command.durationS.value());
        if (!pump->runFor(durationS)) {
            return {ApiStatus::Conflict, errorBody("pump already running")};
        }
        break;
    }
    case PumpAction::Stop:
        // Idempotent: stopping an already-stopped pump is a success no-op.
        pump->stop();
        break;
    }

    return {ApiStatus::Ok, serializePump(makePumpDto(*pump))};
}

std::string ApiServer::buildConfigBody()
{
    ConfigDto dto;
    dto.moistureThresholdLow = config_.getMoistureThresholdLow();
    dto.moistureThresholdHigh = config_.getMoistureThresholdHigh();
    dto.wateringDurationS = config_.getWateringDurationS();
    dto.minWateringIntervalS = config_.getMinWateringIntervalS();
    dto.wateringEnabled = config_.getWateringEnabled();
    dto.sensorReadIntervalMs = config_.getSensorReadIntervalMs();
    dto.dataLogIntervalMs = config_.getDataLogIntervalMs();
    // The wifi password is deliberately absent (ConfigDto carries no such field).
    return serializeConfig(dto);
}

ApiResponse ApiServer::applyConfigSet(const std::string& body)
{
    const ConfigSetResult parsed = parseConfigSet(body);
    if (!parsed.ok) {
        return {ApiStatus::BadRequest, errorBody(parsed.error)};
    }

    // All-or-nothing was validated in the pure parser (nothing is marked to
    // apply when it rejects). Each present field is persisted via its setter; a
    // setter returning false here is an unexpected persistence failure on an
    // already-validated value (out-of-range was ruled out) -> 500. The failing
    // setter is logged (identifying the field) before returning.
    const ConfigSetRequest& r = parsed.request;
    bool ok = true;
    if (ok && r.moistureThresholdLow &&
        !config_.setMoistureThresholdLow(*r.moistureThresholdLow)) {
        ESP_LOGE(TAG, "config persist failed for moistureThresholdLow");
        ok = false;
    }
    if (ok && r.moistureThresholdHigh &&
        !config_.setMoistureThresholdHigh(*r.moistureThresholdHigh)) {
        ESP_LOGE(TAG, "config persist failed for moistureThresholdHigh");
        ok = false;
    }
    if (ok && r.wateringDurationS &&
        !config_.setWateringDurationS(*r.wateringDurationS)) {
        ESP_LOGE(TAG, "config persist failed for wateringDurationS");
        ok = false;
    }
    if (ok && r.minWateringIntervalS &&
        !config_.setMinWateringIntervalS(*r.minWateringIntervalS)) {
        ESP_LOGE(TAG, "config persist failed for minWateringIntervalS");
        ok = false;
    }
    if (ok && r.wateringEnabled &&
        !config_.setWateringEnabled(*r.wateringEnabled)) {
        ESP_LOGE(TAG, "config persist failed for wateringEnabled");
        ok = false;
    }
    if (ok && r.sensorReadIntervalMs &&
        !config_.setSensorReadIntervalMs(*r.sensorReadIntervalMs)) {
        ESP_LOGE(TAG, "config persist failed for sensorReadIntervalMs");
        ok = false;
    }
    if (ok && r.dataLogIntervalMs &&
        !config_.setDataLogIntervalMs(*r.dataLogIntervalMs)) {
        ESP_LOGE(TAG, "config persist failed for dataLogIntervalMs");
        ok = false;
    }

    if (!ok) {
        return {ApiStatus::InternalError,
                errorBody("failed to persist configuration")};
    }

    return {ApiStatus::Ok, buildConfigBody()};
}

ApiResponse ApiServer::buildHistoryResponse(const HistoryQuery& query)
{
    // metric is required — the storage layer is keyed by it.
    if (query.metric.empty()) {
        return {ApiStatus::BadRequest, errorBody("metric is required")};
    }

    const uint32_t now = wallClock_.nowEpoch();

    // Window resolution is pure (range precedence over explicit start/end, else
    // the last 24 h; underflow-clamped). An unknown named range is a client
    // error.
    std::optional<uint32_t> start;
    std::optional<uint32_t> end;
    if (query.start.has_value()) {
        start = static_cast<uint32_t>(*query.start);
    }
    if (query.end.has_value()) {
        end = static_cast<uint32_t>(*query.end);
    }
    const WindowResult window = resolveWindow(query.range, start, end, now);
    if (!window.ok) {
        return {ApiStatus::BadRequest, errorBody("unknown range")};
    }
    const uint32_t t0 = window.t0;
    const uint32_t t1 = window.t1;

    HistorySeries series;
    series.metric = query.metric;
    series.reading = query.reading;  // echoed only; storage is keyed by metric
    series.start = static_cast<int64_t>(t0);
    series.end = static_cast<int64_t>(t1);

    // Non-blocking filesystem read (no bus access). An empty result is a 200
    // with empty arrays — a window with no data is a success, not an error.
    const std::vector<SensorReading> readings =
        storage_.getSensorReadings(query.metric, t0, t1);
    series.timestamps.reserve(readings.size());
    series.values.reserve(readings.size());
    for (const SensorReading& r : readings) {
        series.timestamps.push_back(static_cast<int64_t>(r.epoch));
        series.values.push_back(r.value);
    }

    return {ApiStatus::Ok, serializeHistory(series)};
}

std::string ApiServer::buildEventsBody(std::size_t count)
{
    // Non-blocking: the event log lives on the filesystem. getEvents is
    // newest-first; the DTO adds a human category name when the id is known.
    const std::vector<EventRecord> records = storage_.getEvents(count);
    std::vector<EventDto> events;
    events.reserve(records.size());
    for (const EventRecord& r : records) {
        EventDto dto;
        dto.epoch = static_cast<int64_t>(r.epoch);
        dto.category = static_cast<int>(r.category);
        const char* name = eventCategoryName(dto.category);
        if (name != nullptr) {
            dto.categoryName = name;
        }
        dto.detail = r.detail;
        events.push_back(std::move(dto));
    }
    return serializeEvents(events);
}

std::string ApiServer::buildSelfTestBody()
{
    // DOCUMENTED QUIRK-5 EXCEPTION: unlike every other handler, the self-test
    // deliberately issues a real bus read() on each sensor. It is bounded (one
    // attempt per sensor, no retry — the drivers do not loop) and runs on the
    // httpd task, off the 10 Hz watering loop; the injected Locked* wrappers
    // serialize each read() with the other bus users, so a concurrent console
    // or sensor-task read is never corrupted. No watering decision is made.
    SelfTestResultDto result;
    bool overall = true;

    {
        SelfTestCheckDto check;
        check.name = "environmental";
        check.ok = env_.read();
        check.detail =
            check.ok ? std::string("ok")
                     : ("read failed, error " +
                        std::to_string(env_.getLastError()));
        overall = overall && check.ok;
        result.checks.push_back(std::move(check));
    }

    {
        // The soil sensor read() is the RS485/Modbus round-trip self-test.
        SelfTestCheckDto check;
        check.name = "soil";
        check.ok = soil_.read();
        check.detail =
            check.ok ? std::string("ok")
                     : ("read failed, error " +
                        std::to_string(soil_.getLastError()));
        overall = overall && check.ok;
        result.checks.push_back(std::move(check));
    }

    result.overall = overall;
    return serializeSelfTest(result);
}

bool ApiServer::start()
{
    if (server_ != nullptr) {
        return true;  // already started (idempotent)
    }

    httpd_handle_t server = nullptr;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = kMaxUriHandlers;
    config.lru_purge_enable = true;
    // Wildcard matching so POST /api/v1/pumps/{name} can be served by one
    // handler on the pumps command prefix; exact routes still match exactly.
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return false;
    }

    // One httpd handler per route; user_ctx carries this instance. The full
    // /api/v1/ route set is registered here. The per-pump command uses a
    // wildcard pattern (the name is parsed from the URI in the handler); an
    // unknown pump name answers 404 via applyPumpCommand, and a truly unknown
    // route is covered by the 404 err_handler below.
    const httpd_uri_t routes[] = {
        {
            .uri = "/api/v1/status",
            .method = HTTP_GET,
            .handler = &statusHandler,
            .user_ctx = this,
        },
        {
            .uri = "/api/v1/sensors",
            .method = HTTP_GET,
            .handler = &sensorsHandler,
            .user_ctx = this,
        },
        {
            .uri = "/api/v1/power",
            .method = HTTP_GET,
            .handler = &powerHandler,
            .user_ctx = this,
        },
        {
            .uri = "/api/v1/pumps",
            .method = HTTP_GET,
            .handler = &pumpsListHandler,
            .user_ctx = this,
        },
        {
            .uri = "/api/v1/pumps/*",
            .method = HTTP_POST,
            .handler = &pumpCommandHandler,
            .user_ctx = this,
        },
        {
            .uri = "/api/v1/config",
            .method = HTTP_GET,
            .handler = &configGetHandler,
            .user_ctx = this,
        },
        {
            .uri = "/api/v1/config",
            .method = HTTP_POST,
            .handler = &configSetHandler,
            .user_ctx = this,
        },
        {
            .uri = "/api/v1/history",
            .method = HTTP_GET,
            .handler = &historyHandler,
            .user_ctx = this,
        },
        {
            .uri = "/api/v1/events",
            .method = HTTP_GET,
            .handler = &eventsHandler,
            .user_ctx = this,
        },
        {
            .uri = "/api/v1/selftest",
            .method = HTTP_POST,
            .handler = &selfTestHandler,
            .user_ctx = this,
        },
        {
            .uri = "/api/v1/ota",
            .method = HTTP_POST,
            .handler = &otaHandler,
            .user_ctx = this,
        },
        {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = &staticFileHandler,
            .user_ctx = this,
        },
    };
    for (const httpd_uri_t& route : routes) {
        err = httpd_register_uri_handler(server, &route);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register %s failed: %s", route.uri,
                     esp_err_to_name(err));
            httpd_stop(server);
            return false;
        }
    }

    // Unknown routes answer the JSON 404 envelope, not the default HTML page.
    err = httpd_register_err_handler(server, HTTPD_404_NOT_FOUND,
                                     &notFoundHandler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register 404 handler failed: %s", esp_err_to_name(err));
        httpd_stop(server);
        return false;
    }

    // A known path with the wrong method answers the JSON 405 envelope too.
    err = httpd_register_err_handler(server, HTTPD_405_METHOD_NOT_ALLOWED,
                                     &methodNotAllowedHandler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register 405 handler failed: %s", esp_err_to_name(err));
        httpd_stop(server);
        return false;
    }

    server_ = server;
    ESP_LOGI(TAG, "API server started (/api/v1/)");
    return true;
}

void ApiServer::stop()
{
    if (server_ != nullptr) {
        httpd_stop(static_cast<httpd_handle_t>(server_));
        server_ = nullptr;
        ESP_LOGI(TAG, "API server stopped");
    }
}

}  // namespace api
