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

namespace {

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
}

void SystemObserver::pollWifi()
{
    if (wifi_ == nullptr) {
        return;  // provisioning / headless: nothing to observe
    }
    const WifiState state = wifi_->snapshot().state;
    if (!haveWifiState_ || state != lastWifiState_) {
        logger_.logWifi(wifiStateName(state));
        lastWifiState_ = state;
        haveWifiState_ = true;
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
