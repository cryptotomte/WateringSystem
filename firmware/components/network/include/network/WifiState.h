// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file WifiState.h
 * @brief Shared WiFi state types (pure — host + target).
 *
 * The state enum, the immutable status snapshot exposed to consumers, and
 * the reconnect-policy construction parameters for the pure WifiManager
 * (feature 007). No IDF includes; part of the header-only surface of the
 * `network` component. Normative contract:
 * specs/007-wifi-provisioning/contracts/wifi-manager-states.md and
 * data-model.md.
 */

#ifndef WATERINGSYSTEM_NETWORK_WIFISTATE_H
#define WATERINGSYSTEM_NETWORK_WIFISTATE_H

#include <cstdint>

/**
 * @brief WifiManager state (data-model.md state machine).
 *
 * `Provisioning` = SoftAP up + portal serving, STA monitoring suspended.
 * `Connecting` = first STA attempt in flight. `Connected` = GotIp acquired.
 * `Reconnecting` = attempt failed, waiting retryIntervalMs before the next.
 * `ReconnectPaused` = failuresBeforePause consecutive failures reached,
 * waiting pauseMs before a fresh round (never reboots — FR-013).
 */
enum class WifiState {
    Provisioning,
    Connecting,
    Connected,
    Reconnecting,
    ReconnectPaused
};

/**
 * @brief Point-in-time, by-value status copy for status/LED consumers.
 *
 * Produced by WifiManager::snapshot() as a plain by-value, point-in-time copy
 * of the state + counters. WifiManager is unsynchronized with a single writer
 * (the wifi task's tick()); snapshot() is NOT itself synchronized. A cross-task
 * reader (the diag console, and since feature 009 the HTTP `/api/v1/status`
 * handler) may therefore observe state up to one tick old, or a momentarily
 * torn tuple — acceptable for status display only (the same trade-off as the
 * PR-08 SyncStatus reader). Consistent with WifiManager.h ("Unsynchronized by
 * design").
 */
struct WifiConnectionSnapshot {
    WifiState state;               ///< current state machine state
    int8_t rssi;                   ///< last RSSI dBm; valid only in Connected
    uint8_t consecutiveFailures;   ///< 0..failuresBeforePause; drives the pause
    uint32_t disconnectCount;      ///< monotonic drop count, for diagnostics
    bool ipAcquired;               ///< true after GotIp
};

/**
 * @brief Reconnect timing parameters (defaults = parity constants).
 *
 * Kept as a plain struct so the wiring site can override from Kconfig
 * (CONFIG_WS_WIFI_*). Defaults match docs/parity-checklist.md §7:
 * 10 s retry, +60 s pause after 5 consecutive failures, 5 s health monitor.
 */
struct ReconnectPolicy {
    uint32_t retryIntervalMs = 10000;   ///< delay between STA attempts
    uint8_t failuresBeforePause = 5;    ///< failures that trigger the pause
    uint32_t pauseMs = 60000;           ///< extra wait before the next round
    uint32_t monitorIntervalMs = 5000;  ///< health-check cadence (Connected)
};

#endif /* WATERINGSYSTEM_NETWORK_WIFISTATE_H */
