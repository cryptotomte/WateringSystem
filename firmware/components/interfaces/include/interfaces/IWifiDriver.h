// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file IWifiDriver.h
 * @brief WiFi hardware seam (STA/AP control + non-blocking event poll).
 *
 * The host-test seam for WiFi (feature 007): the pure WifiManager holds all
 * timing and state-machine logic above this interface and is host-tested
 * against MockWifiDriver; EspWifiDriver is the only hardware-touching
 * implementation (esp_wifi/esp_netif/esp_event) and is excluded from the
 * linux build. Mirrors IModbusClient / II2cBus. Normative contract:
 * specs/007-wifi-provisioning/contracts/IWifiDriver.md.
 *
 * Part of the header-only `interfaces` component: no IDF includes allowed.
 */

#ifndef WATERINGSYSTEM_INTERFACES_IWIFIDRIVER_H
#define WATERINGSYSTEM_INTERFACES_IWIFIDRIVER_H

#include <cstdint>
#include <string>

/**
 * @brief WiFi lifecycle events, drained one per tick via pollEvent().
 *
 * `Connected` = association succeeded (L2). `GotIp` = DHCP lease acquired —
 * the "usable" signal the manager treats as fully connected.
 * `Disconnected` / `ConnectFailed` = drop / association failure (the manager
 * treats both as "attempt failed"). `None` = the event queue is empty.
 *
 * Event ordering (contract): a successful connect delivers `Connected` then
 * `GotIp`; a failure delivers `ConnectFailed` or `Disconnected`.
 */
enum class WifiEvent { None, Connected, GotIp, Disconnected, ConnectFailed };

/**
 * @brief STA/AP control plus a non-blocking event queue.
 *
 * Behavioral contract: no method blocks on network I/O — the outcome of a
 * connect/AP-start attempt arrives later as an event via pollEvent(). This
 * keeps the pure WifiManager deterministic and lets the wifi task never
 * stall (FR-014). The driver owns no timers (all cadence lives in
 * WifiManager via ITimeProvider) and touches only WiFi/netif/event-loop
 * resources — never pump/sensor state.
 */
class IWifiDriver {
public:
    virtual ~IWifiDriver() = default;

    /**
     * @brief Configure STA and begin association (non-blocking).
     *
     * @param ssid Network SSID to join.
     * @param password Network password (empty for an open network).
     * @return false only on a synchronous config error; success/failure of
     *         the attempt itself arrives later as a WifiEvent.
     */
    virtual bool staConnect(const std::string& ssid,
                            const std::string& password) = 0;

    /**
     * @brief Stop STA / disconnect. Idempotent.
     */
    virtual void staStop() = 0;

    /**
     * @brief Start the SoftAP (WPA2) at 192.168.4.1 (non-blocking).
     *
     * @param ssid AP SSID to advertise.
     * @param password AP password (WPA2).
     * @return false only on a synchronous config error.
     */
    virtual bool apStart(const std::string& ssid,
                         const std::string& password) = 0;

    /**
     * @brief Stop the SoftAP. Idempotent.
     */
    virtual void apStop() = 0;

    /**
     * @brief Drain the next queued event, or None if the queue is empty.
     *
     * Called once per manager tick; thread-safe (the driver enqueues from
     * the esp_event callback).
     */
    virtual WifiEvent pollEvent() = 0;

    /**
     * @brief Last known RSSI in dBm; unspecified when not connected.
     */
    virtual int8_t rssi() const = 0;
};

#endif /* WATERINGSYSTEM_INTERFACES_IWIFIDRIVER_H */
