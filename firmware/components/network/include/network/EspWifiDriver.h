// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file EspWifiDriver.h
 * @brief IWifiDriver implementation over esp_wifi/esp_netif/esp_event
 *        (target-only, feature 007 US2).
 *
 * The single hardware touchpoint of the WiFi subsystem: it owns the STA and
 * AP netifs, drives esp_wifi (mode/config/start/connect) and translates the
 * esp_event callbacks into a thread-safe WifiEvent queue drained by
 * pollEvent(). It carries NO timing and NO business logic — all cadence and
 * state-machine decisions live in the pure WifiManager above the IWifiDriver
 * seam (contracts/IWifiDriver.md). Excluded from the linux build (esp_wifi
 * has no host port; host tests use MockWifiDriver).
 *
 * PRIV rule (same as EspI2cBus / StorageMount / ProvisioningPortal): the
 * heavy IDF headers (esp_wifi.h, esp_netif.h, esp_event.h, freertos/queue.h)
 * appear only in the .cpp. The netif handles and the FreeRTOS queue handle are
 * held here as opaque void* so a consumer that includes this header (only
 * app_main, on target) pulls in no WiFi/netif dependency. Credential values
 * are never logged (FR-004 / PR-06 convention): the SSID may be logged, the
 * password never.
 */

#ifndef WATERINGSYSTEM_NETWORK_ESPWIFIDRIVER_H
#define WATERINGSYSTEM_NETWORK_ESPWIFIDRIVER_H

#include <string>

#include "esp_err.h"

#include "interfaces/IWifiDriver.h"

/**
 * @brief esp_wifi-backed WiFi driver (STA/AP control + non-blocking events).
 *
 * Lifetime: construct once at the wiring site (app_main function-local static,
 * so no non-trivial constructor runs before the boot pump fail-safe — the
 * constructor is trivial and all IDF work happens in init()). init() must be
 * called exactly once, after esp_netif_init()/esp_event_loop_create_default()
 * and NVS init already done in app_main, before the first staConnect/apStart.
 */
class EspWifiDriver : public IWifiDriver {
public:
    EspWifiDriver() = default;
    ~EspWifiDriver() override;

    EspWifiDriver(const EspWifiDriver&) = delete;
    EspWifiDriver& operator=(const EspWifiDriver&) = delete;

    /**
     * @brief One-time init: create the default STA + AP netifs, esp_wifi_init,
     *        register the WIFI_EVENT / IP_EVENT handlers and create the event
     *        queue. Idempotent (a second call is a successful no-op).
     *
     * @return ESP_OK on success; the first failing esp_err_t otherwise. On a
     *         failure the driver stays uninitialized and every control method
     *         becomes a logged no-op / false — WiFi is simply unavailable and
     *         the watering path is unaffected (FR-014).
     */
    esp_err_t init();

    bool staConnect(const std::string& ssid,
                    const std::string& password) override;
    void staStop() override;
    bool apStart(const std::string& ssid,
                 const std::string& password) override;
    void apStop() override;
    WifiEvent pollEvent() override;
    int8_t rssi() const override;

private:
    // Held opaque to keep esp_wifi/esp_netif/freertos headers in the .cpp.
    void* eventQueue_ = nullptr;  ///< QueueHandle_t of WifiEvent (thread-safe)
    void* staNetif_ = nullptr;    ///< esp_netif_t* for the STA interface
    void* apNetif_ = nullptr;     ///< esp_netif_t* for the AP interface
    bool initialized_ = false;    ///< true once init() succeeded
    bool started_ = false;        ///< true between esp_wifi_start and stop
};

#endif /* WATERINGSYSTEM_NETWORK_ESPWIFIDRIVER_H */
