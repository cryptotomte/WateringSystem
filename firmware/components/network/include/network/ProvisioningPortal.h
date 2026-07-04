// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ProvisioningPortal.h
 * @brief First-boot SoftAP setup portal (target-only HTTP server).
 *
 * A minimal standalone esp_http_server that runs while the device is in
 * Provisioning (AP) mode (feature 007, US1). It serves a small self-contained
 * setup page and accepts a POST that validates + persists WiFi credentials,
 * then schedules a restart so the operator's new settings take effect. The
 * full JSON status API and /api/wifi/scan are out of scope (PR-09).
 *
 * This is the only HTTP touchpoint of the `network` component and is excluded
 * from the linux build (esp_http_server has no host port); all reusable
 * policy (credential validation) lives in the pure WifiCredentialValidation
 * header so it stays host-tested. Normative contract:
 * specs/007-wifi-provisioning/contracts/provisioning-portal.md.
 *
 * PRIV rule (same as EspI2cBus / StorageMount): esp_http_server.h appears
 * only in the .cpp; the server handle is held as an opaque pointer and the
 * HTTP route handlers are file-local in the .cpp — so consumers (app_main)
 * that include this header pull in no HTTP-server dependency. Credential
 * values are never logged or echoed back (FR-004 / PR-06 convention).
 */

#ifndef WATERINGSYSTEM_NETWORK_PROVISIONINGPORTAL_H
#define WATERINGSYSTEM_NETWORK_PROVISIONINGPORTAL_H

#include <cstdint>
#include <functional>
#include <string>

#include "esp_err.h"

#include "interfaces/IConfigStore.h"

/**
 * @brief Standalone provisioning HTTP server over IConfigStore.
 *
 * Lifetime: construct once at the wiring site (app_main), call start() when
 * entering provisioning mode and keep the instance alive for as long as the
 * portal serves. The config store is held by reference and outlives the
 * portal (it is an app_main function-local static).
 */
class ProvisioningPortal {
public:
    /// Delay between the credential-save HTTP response and the restart, so
    /// the response reaches the browser before the device reboots (FR-007).
    static constexpr uint32_t kRestartDelayMs = 3000;

    /// Hook invoked to trigger the (deferred) restart after a successful
    /// credential save. Kept injectable so esp_restart stays out of the
    /// directly-called handler path; the default schedules it kRestartDelayMs
    /// later on a short-lived task.
    using RestartHook = std::function<void()>;

    /**
     * @brief Construct the portal over a config store and a restart hook.
     *
     * @param configStore Credential sink; must outlive the portal.
     * @param restartHook Called once on a successful save. When null (the
     *        default), a built-in scheduled restart is used.
     */
    explicit ProvisioningPortal(IConfigStore& configStore,
                                RestartHook restartHook = nullptr);

    ~ProvisioningPortal();

    ProvisioningPortal(const ProvisioningPortal&) = delete;
    ProvisioningPortal& operator=(const ProvisioningPortal&) = delete;

    /**
     * @brief Start the HTTP server and register the routes.
     *
     * @return ESP_OK on success; an esp_err_t on server start / route
     *         registration failure (already-started is a successful no-op).
     */
    esp_err_t start();

    /**
     * @brief Stop the HTTP server. Idempotent.
     */
    void stop();

    // -- Submission API used by the POST route handler (.cpp) --------------
    // Validation happens in the handler via the pure validateWifiCredentials;
    // these two steps stay on the instance so the handler can interleave them
    // with the HTTP response (persist, respond 200, then restart).

    /**
     * @brief Persist already-validated credentials.
     * @return false on a persistence failure (handler responds 5xx and the
     *         device stays provisionable).
     */
    bool persistCredentials(const std::string& ssid,
                            const std::string& password);

    /**
     * @brief Invoke the restart hook (default: schedule a deferred reboot).
     * Called after the success response has been sent.
     */
    void scheduleRestart();

private:
    IConfigStore& configStore_;
    RestartHook restartHook_;
    void* server_ = nullptr;  ///< opaque httpd_handle_t (see .cpp)
};

#endif /* WATERINGSYSTEM_NETWORK_PROVISIONINGPORTAL_H */
