// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ApiServer.h
 * @brief Versioned /api/v1/ REST/JSON HTTP server (target-only plumbing).
 *
 * The single esp_http_server touchpoint of the `api` component (feature 009,
 * PR-09). It owns no business/watering logic: each handler reads the injected
 * Locked* decorators through their NON-BLOCKING cached getters into a plain DTO,
 * hands that DTO to the pure host-tested serializers (api/ApiSerialize.h), and
 * sends the returned JSON string. All routing/envelope/serialization decisions
 * live in the pure layer; this class is the thin HTTP wiring only.
 *
 * This file covers the US1 read endpoints only:
 *   GET /api/v1/status   — system status (mode/wifi/time/uptime/reset/fw/storage
 *                          [+power on rev2])
 *   GET /api/v1/sensors  — cached environmental/soil/level [+power] readings
 *   GET /api/v1/power     — INA226 telemetry (rev2); not-available shape on rev1
 * plus the US2 pump/config endpoints:
 *   GET  /api/v1/pumps        — every pump's status (capability-enumerated)
 *   POST /api/v1/pumps/{name} — start/run/stop a pump (cap+rules in the pump)
 *   GET  /api/v1/config       — current config (never the wifi password)
 *   POST /api/v1/config       — apply a validated config subset (persisted)
 * Unknown routes answer the JSON 404 envelope. The remaining US3 routes
 * (history/events/selftest/ota) are added in later tasks.
 *
 * PRIV rule (same as ProvisioningPortal / EspI2cBus): esp_http_server.h appears
 * ONLY in the .cpp; the server handle is held here as an opaque void* and the
 * HTTP route handlers are file-local in the .cpp — so consumers (app_main)
 * pull in no HTTP-server dependency. Excluded from the linux build (the pure
 * layer carries all host-tested logic). The wifi password is never represented
 * in any DTO or serialized field (FR-004).
 *
 * Isolation (FR-015): the server runs on the esp_http_server task, is NOT
 * subscribed to the task watchdog, and shares no mutex with the watering loop
 * beyond the Locked* wrappers — a slow or flooding client cannot delay the
 * 10 Hz pump/level loop or the sensor task.
 */

#ifndef WATERINGSYSTEM_API_APISERVER_H
#define WATERINGSYSTEM_API_APISERVER_H

#include <string>

#include "board/board.h"
#include "interfaces/IConfigStore.h"
#include "interfaces/IDataStorage.h"
#include "interfaces/IEnvironmentalSensor.h"
#include "interfaces/ILevelSensor.h"
#include "interfaces/ISoilSensor.h"
#include "interfaces/ITimeProvider.h"
#include "interfaces/IWallClock.h"
#include "interfaces/IWaterPump.h"
#if BOARD_HAS_INA226
#include "interfaces/IPowerSensor.h"
#endif
#include "network/LockedWifiManager.h"
#include "time/SntpClient.h"

namespace api {

/**
 * @brief A ready-to-send response: an HTTP status line plus a JSON body.
 *
 * Returned by the mutating US2 command builders (pump command / config set) so
 * the file-local httpd handler can vary the status line per outcome (200 on
 * success, 4xx on a rejected command/validation error, 5xx on an unexpected
 * persistence failure) rather than being fixed to 200 like the read builders.
 * `status` is a static string literal (never freed); `body` comes from the pure
 * envelope/serializer layer.
 */
struct ApiResponse {
    const char* status;  ///< HTTP status line, e.g. "200 OK"
    std::string body;    ///< JSON envelope body
};

/**
 * @brief Standalone /api/v1/ HTTP server over the Locked* decorators.
 *
 * Lifetime: construct once at the wiring site (app_main STATION branch region,
 * after every sensor + the config/storage/clock it reports), call start() when
 * the station reaches Connected (an IP is required for the socket to bind on the
 * STA interface) and keep the instance alive for the program lifetime. Every
 * injected reference must outlive the server (they are app_main function-local
 * statics).
 */
class ApiServer {
public:
    /**
     * @brief Inject the Locked* read decorators + status sources.
     *
     * The references are the mutex-serializing Locked* wrappers (passed as their
     * interfaces), the wifi snapshot decorator, the wall clock + SNTP status,
     * and the monotonic uptime clock — exactly the objects the diag console is
     * wired with. On rev2 the INA226 power sensor is injected too (compile-time
     * absent on rev1, BOARD_HAS_INA226).
     *
     * @param config      Configuration store (mode + SSID); cached getters only.
     * @param storage     Data storage (filesystem stats).
     * @param env         Environmental sensor (cached T/RH/P, refreshed by the
     *                    5 s sensor task).
     * @param soil        Soil sensor (cached values; no periodic reader yet —
     *                    reads report valid=false until PR-11).
     * @param levelLow    Reservoir low-mark level sensor (cached, 10 Hz loop).
     * @param levelHigh   Reservoir high-mark level sensor (cached, 10 Hz loop).
     * @param wifi        Wifi snapshot decorator (state/rssi/ip-acquired).
     * @param wallClock   Wall clock (epoch + is-set) for time + timestamps.
     * @param sntp        SNTP client (last-sync epoch); status only.
     * @param uptime      Monotonic clock for uptimeMs.
     * @param plantPump   Plant pump, as its LockedWaterPump wrapper (the only
     *                    allowed access path — every command is serialized with
     *                    the 10 Hz update() loop). Its runFor()/stop() enforce
     *                    the hard 300 s cap and the no-restart rule; the server
     *                    makes no watering decision of its own.
     * @param reservoirPump Reservoir pump wrapper (rev1 only; compile-time
     *                    absent on the single-pump rev2 node,
     *                    BOARD_HAS_RESERVOIR_PUMP).
     * @param power       INA226 power sensor (rev2 only).
     */
    ApiServer(IConfigStore& config, IDataStorage& storage,
              IEnvironmentalSensor& env, ISoilSensor& soil,
              ILevelSensor& levelLow, ILevelSensor& levelHigh,
              LockedWifiManager& wifi, IWallClock& wallClock, SntpClient& sntp,
              ITimeProvider& uptime, IWaterPump& plantPump
#if BOARD_HAS_RESERVOIR_PUMP
              ,
              IWaterPump& reservoirPump
#endif
#if BOARD_HAS_INA226
              ,
              IPowerSensor& power
#endif
              )
        : config_(config), storage_(storage), env_(env), soil_(soil),
          levelLow_(levelLow), levelHigh_(levelHigh), wifi_(wifi),
          wallClock_(wallClock), sntp_(sntp), uptime_(uptime),
          plantPump_(plantPump)
#if BOARD_HAS_RESERVOIR_PUMP
          ,
          reservoirPump_(reservoirPump)
#endif
#if BOARD_HAS_INA226
          ,
          power_(power)
#endif
    {
    }

    ~ApiServer();

    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;

    /**
     * @brief Start the HTTP server and register the US1 routes.
     *
     * Idempotent (an already-started server is a successful no-op) and non-fatal
     * on failure: the caller logs and keeps running — the watering path never
     * depends on the API (FR-015).
     *
     * @return true when the server is running (started or already up); false on
     *         a server-start / route-registration failure.
     */
    bool start();

    /// Stop the HTTP server. Idempotent.
    void stop();

    // -- Response builders invoked by the file-local HTTP handlers (.cpp) -----
    // Public so the anonymous-namespace httpd handlers (which recover the
    // instance from req->user_ctx) can call them without exposing httpd types
    // in this header. They return the ready-to-send JSON body; the handlers do
    // the httpd_resp_* plumbing. Each reads ONLY non-blocking cached getters —
    // never read()/isAvailable() (QUIRK 5) — and makes no watering decision.

    /// Build the GET /api/v1/status success body.
    std::string buildStatusBody();

    /// Build the GET /api/v1/sensors success body.
    std::string buildSensorsBody();

    /// Build the GET /api/v1/power body (telemetry on rev2, not-available on rev1).
    std::string buildPowerBody();

    /**
     * @brief Build the GET /api/v1/pumps success body (list of every pump).
     *
     * Capability-enumerated (rev1 plant+reservoir, rev2 plant only,
     * BOARD_HAS_RESERVOIR_PUMP) from the pumps' non-blocking status getters.
     */
    std::string buildPumpsBody();

    /**
     * @brief Apply a POST /api/v1/pumps/{name} command and report the outcome.
     *
     * @param name  the trailing URI segment ("plant"/"reservoir"); an unknown
     *              name yields a 404 error envelope.
     * @param body  the raw JSON request body (parsed by the pure parsePumpCommand).
     * @return the resulting PumpDto on success (200); a 4xx error envelope on an
     *         unknown name, malformed/invalid body, or a rejected command (e.g.
     *         start on an already-running pump — the clock is not restarted).
     *
     * The server makes NO watering decision: the duration cap and the
     * no-restart rule live entirely in the pump's own runFor()/stop().
     */
    ApiResponse applyPumpCommand(const std::string& name,
                                 const std::string& body);

    /// Build the GET /api/v1/config success body (never the wifi password).
    std::string buildConfigBody();

    /**
     * @brief Apply a POST /api/v1/config set request and report the outcome.
     *
     * Validation is all-or-nothing in the pure parseConfigSet; on success each
     * present field is applied through its IConfigStore setter (which persists).
     *
     * @param body  the raw JSON request body.
     * @return the new ConfigDto (200) on success; a 400 error envelope on a
     *         malformed/out-of-range body; a 500 error envelope if a setter
     *         unexpectedly fails to persist an already-validated value.
     */
    ApiResponse applyConfigSet(const std::string& body);

private:
    /// Current device IPv4 address on the STA interface ("" when none).
    std::string deviceIp() const;

    /// Resolve a pump name ("plant"/"reservoir") to its wrapper, or nullptr for
    /// an unknown name (capability-aware: "reservoir" exists on rev1 only).
    IWaterPump* pumpByName(const std::string& name);

    IConfigStore& config_;
    IDataStorage& storage_;
    IEnvironmentalSensor& env_;
    ISoilSensor& soil_;
    ILevelSensor& levelLow_;
    ILevelSensor& levelHigh_;
    LockedWifiManager& wifi_;
    IWallClock& wallClock_;
    SntpClient& sntp_;
    ITimeProvider& uptime_;
    IWaterPump& plantPump_;
#if BOARD_HAS_RESERVOIR_PUMP
    IWaterPump& reservoirPump_;
#endif
#if BOARD_HAS_INA226
    IPowerSensor& power_;
#endif

    void* server_ = nullptr;  ///< opaque httpd_handle_t (see .cpp)
};

}  // namespace api

#endif /* WATERINGSYSTEM_API_APISERVER_H */
