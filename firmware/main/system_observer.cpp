// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file system_observer.cpp
 * @brief Transition detection + EventLogger forwarding (feature 008 US2).
 *
 * The WifiState → name mapping lives here (not in the pure EventLogger, which
 * takes a const char* state name only). Pump start/stop causes are best-effort
 * "unknown" for now: the IWaterPump seam exposes isRunning() (edge source) and
 * getLastStopReason(), but the running/stop-reason tuple visible from an
 * external poll cannot reliably attribute the cause (console vs controller),
 * so a deterministic placeholder is used until the controller (PR-11) owns and
 * records the cause at the command site.
 */

#include "system_observer.h"

#include "esp_log.h"

namespace {

const char* TAG = "sys_observer";


/// Short, stable name for each WifiState (matches the enum labels so the
/// event detail reads e.g. "wifi=Connected"). Total over the enum.
const char* wifiStateName(WifiState state)
{
    switch (state) {
        case WifiState::Provisioning:    return "Provisioning";
        case WifiState::Connecting:      return "Connecting";
        case WifiState::Connected:       return "Connected";
        case WifiState::Reconnecting:    return "Reconnecting";
        case WifiState::ReconnectPaused: return "ReconnectPaused";
    }
    return "unknown";
}

}  // namespace

void SystemObserver::poll()
{
    pollWifi();
    pollPump(plant_, "plant", plantLastRunning_);
    pollPump(reservoir_, "reservoir", reservoirLastRunning_);

    // Give the pure logger's dropped-event counter a target-side voice: it can
    // only count a failed store, so surface any increase here (mirrors the
    // EspWifiDriver dropped-counter pattern). Never blocks or touches watering.
    const uint32_t dropped = logger_.droppedEvents();
    if (dropped > lastDropped_) {
        ESP_LOGW(TAG, "event log dropped %u events (total %u)",
                 static_cast<unsigned>(dropped - lastDropped_),
                 static_cast<unsigned>(dropped));
        lastDropped_ = dropped;
    }
}

void SystemObserver::pollWifi()
{
    if (wifi_ == nullptr) {
        return;  // provisioning / headless: nothing to observe
    }
    const WifiState state = wifi_->snapshot().state;

    // Was the station Connected on the PREVIOUS poll? Capture before updating
    // lastWifiState_ so the SNTP/API starts below fire only on the transition
    // INTO Connected, not on every poll while already Connected.
    const bool wasConnected =
        haveWifiState_ && lastWifiState_ == WifiState::Connected;

    if (!haveWifiState_ || state != lastWifiState_) {
        logger_.logWifi(wifiStateName(state));
        lastWifiState_ = state;
        haveWifiState_ = true;
    }

    // pollWifi() runs at 10 Hz. Attempt the once-only SNTP + API-server starts
    // ONLY on the edge INTO Connected (an IP is required — before the link is up
    // they are pointless). Gating on the edge keeps a FAILED start to a single
    // attempt per Connected transition — it retries on the NEXT Connected edge
    // (after a reconnect), never 10x/s. Each latch (sntpStarted_/
    // apiServerStarted_) makes a success permanent. Both start() calls are
    // idempotent and non-fatal (a failure never affects boot/watering,
    // FR-014/FR-015); the pointers are nullptr in provisioning/headless mode, so
    // neither is ever started there.
    const bool connectedEdge = state == WifiState::Connected && !wasConnected;
    if (!connectedEdge) {
        return;
    }
    if (sntp_ != nullptr && !sntpStarted_ && sntp_->start()) {
        sntpStarted_ = true;
    }
    if (apiServer_ != nullptr && !apiServerStarted_ && apiServer_->start()) {
        apiServerStarted_ = true;
    }
}

void SystemObserver::pollPump(IWaterPump* pump, const char* name,
                              bool& lastRunning)
{
    if (pump == nullptr) {
        return;  // pump not present on this board
    }
    const bool running = pump->isRunning();
    if (running == lastRunning) {
        return;
    }
    if (running) {
        // Cause attribution belongs to the controller (PR-11); best-effort.
        logger_.logPumpStart(name, "unknown");
    } else {
        logger_.logPumpStop(name, "unknown");
    }
    lastRunning = running;
}
