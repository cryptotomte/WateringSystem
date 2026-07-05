// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LockedWifiManager.h
 * @brief Mutex-serializing WifiManager snapshot decorator (header-only).
 *
 * WHY THIS EXISTS: feature 009 adds a second cross-task reader of the wifi
 * state — the HTTP `/api/v1/status` handler on the esp_http_server task — next
 * to the existing diag console reader. WifiManager is unsynchronized by design
 * (single writer: the wifi task's tick()), so two concurrent readers could each
 * observe a momentarily inconsistent tuple. This thin decorator wraps a
 * WifiManager and takes a mutex around snapshot(), giving every reader a
 * self-consistent copy (resolves the WifiState.h TODO(PR-09)). Same composition
 * pattern as LockedConfigStore / LockedSoilSensor.
 *
 * SCOPE: this synchronizes READERS with each other. The single writer (tick())
 * runs lock-free on the wifi task and is intentionally NOT routed through this
 * wrapper — a reader may still observe state one tick old, which is correct for
 * status display. Only snapshot() is exposed; drive begin()/tick() directly on
 * the wrapped WifiManager from its owning task.
 *
 * Pure C++ (<mutex> is available via pthread on ESP-IDF and on the linux
 * preview target), so the decorator is host-includable.
 */

#ifndef WATERINGSYSTEM_NETWORK_LOCKEDWIFIMANAGER_H
#define WATERINGSYSTEM_NETWORK_LOCKEDWIFIMANAGER_H

#include <mutex>

#include "network/WifiManager.h"
#include "network/WifiState.h"

/**
 * @brief WifiManager decorator exposing a mutex-synchronized snapshot().
 *
 * Composition, not inheritance: the wrapped WifiManager stays pure (no locking)
 * and its host tests are unchanged. The wrapped manager must outlive this
 * object; once wrapped, all cross-task status reads go through this wrapper.
 */
class LockedWifiManager {
public:
    /// Wrap @p manager; the wrapped manager must outlive this object.
    explicit LockedWifiManager(WifiManager& manager) : manager_(manager) {}

    LockedWifiManager(const LockedWifiManager&) = delete;
    LockedWifiManager& operator=(const LockedWifiManager&) = delete;

    /// Consistent copy of the wifi state + counters (serialized against other
    /// readers).
    WifiConnectionSnapshot snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return manager_.snapshot();
    }

private:
    WifiManager& manager_;
    mutable std::mutex mutex_;
};

#endif /* WATERINGSYSTEM_NETWORK_LOCKEDWIFIMANAGER_H */
