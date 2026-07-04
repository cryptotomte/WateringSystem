// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file WifiManager.cpp
 * @brief Pure WifiManager state machine (host + target; see WifiManager.h).
 *
 * No IDF/FreeRTOS includes and no logging — logging belongs to the wifi-task
 * wrapper (T019), keeping this translation unit host-buildable and the
 * FR-014 isolation intact.
 */

#include "network/WifiManager.h"

#include <cstdint>
#include <string>

void WifiManager::begin(WifiBootMode mode)
{
    ipAcquired_ = false;
    disconnectCount_ = 0;
    consecutiveFailures_ = 0;
    rssi_ = 0;

    if (mode == WifiBootMode::Provisioning) {
        // SoftAP + portal are brought up at the wiring site (T018); here we
        // only record the mode so tick() suspends all STA monitoring.
        state_ = WifiState::Provisioning;
        return;
    }

    // Station: issue the first STA attempt (attempt #1) and wait for events.
    startConnect();
}

void WifiManager::tick()
{
    // AP/provisioning mode suspends STA monitoring and reconnect entirely,
    // regardless of elapsed time (contract §6).
    if (state_ == WifiState::Provisioning) {
        return;
    }

    // Drain the entire event queue this tick. The loop terminates as soon as
    // the driver reports None, so a "hung" driver (never any events) simply
    // does no work — tick() never blocks (FR-014, contract §7).
    for (WifiEvent event = driver_.pollEvent(); event != WifiEvent::None;
         event = driver_.pollEvent()) {
        handleEvent(event);
    }

    const int64_t now = time_.nowMs();

    switch (state_) {
        case WifiState::Reconnecting:
            // Fixed retry cadence: re-attempt only once the interval elapsed.
            // consecutiveFailures is NOT reset here — it keeps climbing toward
            // the pause threshold across the round.
            if (now >= nextAttemptMs_) {
                startConnect();
            }
            break;

        case WifiState::ReconnectPaused:
            // After the long pause, begin a fresh round with failures reset.
            if (now >= nextAttemptMs_) {
                consecutiveFailures_ = 0;
                startConnect();
            }
            break;

        case WifiState::Connected:
            // Health monitor: refresh rssi on the fixed cadence. Link loss
            // itself arrives as a Disconnected event, so no active poll is
            // needed to detect it here.
            if (now - lastMonitorMs_ >=
                static_cast<int64_t>(policy_.monitorIntervalMs)) {
                rssi_ = driver_.rssi();
                lastMonitorMs_ = now;
            }
            break;

        case WifiState::Connecting:
        case WifiState::Provisioning:
            // Connecting waits for an event; Provisioning handled above.
            break;
    }
}

WifiConnectionSnapshot WifiManager::snapshot() const
{
    return WifiConnectionSnapshot{state_, rssi_, consecutiveFailures_,
                                  disconnectCount_, ipAcquired_};
}

void WifiManager::startConnect()
{
    const std::string ssid = config_.getWifiSsid();
    const std::string password = config_.getWifiPassword();
    // Non-blocking: success/failure of the attempt arrives later as an event.
    // A synchronous config error (return false) leaves us in Connecting; the
    // machine never reboots, it just waits — the operator can re-provision.
    driver_.staConnect(ssid, password);
    state_ = WifiState::Connecting;
}

void WifiManager::handleEvent(WifiEvent event)
{
    const int64_t now = time_.nowMs();

    switch (event) {
        case WifiEvent::GotIp:
            // DHCP lease acquired: fully connected. Reset the failure count
            // and prime the health monitor with a fresh rssi reading.
            state_ = WifiState::Connected;
            consecutiveFailures_ = 0;
            ipAcquired_ = true;
            rssi_ = driver_.rssi();
            lastMonitorMs_ = now;
            break;

        case WifiEvent::Connected:
            // L2 association only — stay Connecting until GotIp confirms a
            // usable link.
            break;

        case WifiEvent::Disconnected:
        case WifiEvent::ConnectFailed:
            handleFailure(now);
            break;

        case WifiEvent::None:
            // Unreachable: the drain loop stops on None.
            break;
    }
}

void WifiManager::handleFailure(int64_t now)
{
    // A drop from an established connection is a diagnostic disconnect;
    // failures that occur while merely (re)connecting are not counted here.
    if (state_ == WifiState::Connected || ipAcquired_) {
        ++disconnectCount_;
        ipAcquired_ = false;
    }

    if (consecutiveFailures_ < UINT8_MAX) {
        ++consecutiveFailures_;
    }

    if (consecutiveFailures_ >= policy_.failuresBeforePause) {
        // Threshold reached: pause a full round before retrying, but never
        // reboot (FR-013 — the machine only cycles Reconnecting↔Paused).
        state_ = WifiState::ReconnectPaused;
        nextAttemptMs_ = now + static_cast<int64_t>(policy_.pauseMs);
    } else {
        state_ = WifiState::Reconnecting;
        nextAttemptMs_ = now + static_cast<int64_t>(policy_.retryIntervalMs);
    }
}
