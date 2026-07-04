// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file MockWifiDriver.h
 * @brief Scriptable IWifiDriver test double (header-only).
 *
 * Backs the WifiManager host tests (feature 007): a scriptable event queue
 * consumed by pollEvent() (queueEvent + scriptConnectSuccess /
 * scriptConnectFailure helpers), call counters for the STA/AP methods, the
 * last ssid/password passed to staConnect/apStart, and a settable rssi().
 * No real networking; deterministic under FakeTimeProvider. Never compiled
 * into target builds (only included from test code). No IDF includes.
 * Mirrors MockConfigStore / MockModbusClient.
 */

#ifndef WATERINGSYSTEM_NETWORK_TESTING_MOCKWIFIDRIVER_H
#define WATERINGSYSTEM_NETWORK_TESTING_MOCKWIFIDRIVER_H

#include <cstdint>
#include <deque>
#include <string>

#include "interfaces/IWifiDriver.h"

/**
 * @brief IWifiDriver over a scripted event queue, instrumented for tests.
 *
 * pollEvent() pops the front of `events` (FIFO), or returns
 * WifiEvent::None when the queue is empty — modelling the driver contract
 * that a "hung" attempt simply never delivers an event.
 */
class MockWifiDriver : public IWifiDriver {
public:
    // -- Instrumentation (public, MockConfigStore style) ------------------
    int staConnectCalls = 0;
    int staStopCalls = 0;
    int apStartCalls = 0;
    int apStopCalls = 0;

    std::string lastStaSsid;      ///< ssid of the most recent staConnect
    std::string lastStaPassword;  ///< password of the most recent staConnect
    std::string lastApSsid;       ///< ssid of the most recent apStart
    std::string lastApPassword;   ///< password of the most recent apStart

    bool staConnectResult = true;  ///< false: synchronous staConnect failure
    bool apStartResult = true;     ///< false: synchronous apStart failure

    // -- Scripting --------------------------------------------------------

    /**
     * @brief Queue one event to be returned by a later pollEvent() (FIFO).
     */
    void queueEvent(WifiEvent event) { events.push_back(event); }

    /**
     * @brief Queue a successful connect: Connected followed by GotIp.
     */
    void scriptConnectSuccess()
    {
        events.push_back(WifiEvent::Connected);
        events.push_back(WifiEvent::GotIp);
    }

    /**
     * @brief Queue a failed connect: a single ConnectFailed.
     */
    void scriptConnectFailure() { events.push_back(WifiEvent::ConnectFailed); }

    /**
     * @brief Set the value returned by rssi().
     */
    void setRssi(int8_t value) { rssi_ = value; }

    /// Scripted event queue (public for direct assertions/manipulation).
    std::deque<WifiEvent> events;

    // -- IWifiDriver ------------------------------------------------------

    bool staConnect(const std::string& ssid,
                    const std::string& password) override
    {
        ++staConnectCalls;
        lastStaSsid = ssid;
        lastStaPassword = password;
        return staConnectResult;
    }

    void staStop() override { ++staStopCalls; }

    bool apStart(const std::string& ssid,
                 const std::string& password) override
    {
        ++apStartCalls;
        lastApSsid = ssid;
        lastApPassword = password;
        return apStartResult;
    }

    void apStop() override { ++apStopCalls; }

    WifiEvent pollEvent() override
    {
        if (events.empty()) {
            return WifiEvent::None;
        }
        const WifiEvent event = events.front();
        events.pop_front();
        return event;
    }

    int8_t rssi() const override { return rssi_; }

private:
    int8_t rssi_ = 0;
};

#endif /* WATERINGSYSTEM_NETWORK_TESTING_MOCKWIFIDRIVER_H */
