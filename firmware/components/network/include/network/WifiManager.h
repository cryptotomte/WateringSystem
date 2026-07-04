// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file WifiManager.h
 * @brief Pure WiFi station state machine + reconnect scheduler (host + target).
 *
 * All timing and state-machine logic for feature 007 lives here, above the
 * IWifiDriver seam: it never touches esp_wifi/esp_netif/esp_event and holds
 * NO reference to any watering/pump/sensor object (FR-014, Constitution I) —
 * the constructor injects only IWifiDriver, IConfigStore, ITimeProvider and a
 * ReconnectPolicy. That isolation is enforced structurally by this signature
 * and asserted in the host tests. Compiles on the linux preview target and is
 * unit-tested against MockWifiDriver + FakeTimeProvider + MockConfigStore.
 *
 * tick() is strictly non-blocking: it advances purely from
 * ITimeProvider::nowMs() deltas and drained IWifiDriver::pollEvent()s, never
 * sleeps or waits, and never triggers a restart/reboot (FR-013, no boot loop).
 *
 * Normative contract:
 * specs/007-wifi-provisioning/contracts/wifi-manager-states.md and
 * data-model.md. Part of the header-only public surface of the `network`
 * component: no IDF includes.
 */

#ifndef WATERINGSYSTEM_NETWORK_WIFIMANAGER_H
#define WATERINGSYSTEM_NETWORK_WIFIMANAGER_H

#include <cstdint>

#include "interfaces/IConfigStore.h"
#include "interfaces/ITimeProvider.h"
#include "interfaces/IWifiDriver.h"
#include "network/WifiBootMode.h"
#include "network/WifiState.h"

/**
 * @brief Station-mode connection lifecycle + reconnect scheduler.
 *
 * Drive it by calling begin() once at boot and tick() at a fixed cadence from
 * the wifi task (T019). All external effects go through the injected
 * IWifiDriver; time comes from ITimeProvider; credentials are read from
 * IConfigStore. Unsynchronized by design — cross-task readers consume the
 * immutable snapshot() (single acquisition at the wiring site).
 */
class WifiManager {
public:
    /**
     * @brief Inject the hardware seam, config store, clock and reconnect policy.
     *
     * @param driver WiFi control + event queue seam.
     * @param config Credential source (SSID/password) for STA attempts.
     * @param time Monotonic clock; all cadences are nowMs() deltas.
     * @param policy Retry/pause/monitor timing (defaults = parity constants).
     */
    WifiManager(IWifiDriver& driver, IConfigStore& config, ITimeProvider& time,
                ReconnectPolicy policy = {})
        : driver_(driver), config_(config), time_(time), policy_(policy)
    {
    }

    /**
     * @brief Enter the boot mode decided by decideBootMode().
     *
     * Station: read the stored credentials, issue the first staConnect
     * (attempt #1) and enter Connecting. Provisioning: enter Provisioning and
     * make NO driver calls — the AP radio and portal are brought up at the
     * wiring site (T018), and tick() is a no-op in this mode.
     */
    void begin(WifiBootMode mode);

    /**
     * @brief Advance the state machine once (non-blocking).
     *
     * Drains every queued IWifiDriver event, then applies time-based
     * transitions (retry cadence, pause release, connected-health monitor)
     * from nowMs() deltas. No-op while Provisioning. Never sleeps or reboots.
     */
    void tick();

    /**
     * @brief Consistent copy of state + counters + rssi for status/LED
     * consumers (single acquisition at the wiring site).
     */
    WifiConnectionSnapshot snapshot() const;

private:
    /// Read credentials and issue a staConnect, entering Connecting.
    void startConnect();

    /// Apply one drained lifecycle event to the state machine.
    void handleEvent(WifiEvent event);

    /// Handle a ConnectFailed/Disconnected event (schedule retry or pause).
    void handleFailure(int64_t now);

    IWifiDriver& driver_;
    IConfigStore& config_;
    ITimeProvider& time_;
    ReconnectPolicy policy_;

    WifiState state_ = WifiState::Connecting;
    int8_t rssi_ = 0;
    uint8_t consecutiveFailures_ = 0;
    uint32_t disconnectCount_ = 0;
    bool ipAcquired_ = false;

    /// Deadline for the next STA attempt (Reconnecting) or pause release
    /// (ReconnectPaused), an absolute nowMs() value.
    int64_t nextAttemptMs_ = 0;
    /// Last time the connected-health monitor refreshed rssi (Connected).
    int64_t lastMonitorMs_ = 0;
};

#endif /* WATERINGSYSTEM_NETWORK_WIFIMANAGER_H */
