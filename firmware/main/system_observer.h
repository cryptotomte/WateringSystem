// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file system_observer.h
 * @brief Edge-detecting bridge from live system state to the EventLogger
 *        (feature 008 US2).
 *
 * Target-side glue (NOT a pure component): it observes the WifiManager
 * snapshot and each pump's running state once per main-loop iteration and
 * emits a typed EventLogger event on every transition — WiFi state change,
 * pump off→on (logPumpStart) and pump on→off (logPumpStop). It holds only
 * borrowed references/pointers (no ownership) and NEVER blocks or crashes
 * watering: the underlying EventLogger drops (counts) a failed store rather
 * than throwing, and poll() never touches pump control.
 *
 * Every dependency is null-guarded: `wifi` is nullptr in provisioning mode
 * (and whenever WiFi init failed), and a pump pointer is nullptr when that
 * pump does not exist on the board (rev2 single-pump node has no reservoir).
 *
 * Lives in main/ because it depends on the target WifiManager + pump handles;
 * the pure formatting/category logic stays in the host-tested EventLogger.
 */

#ifndef WATERINGSYSTEM_MAIN_SYSTEM_OBSERVER_H
#define WATERINGSYSTEM_MAIN_SYSTEM_OBSERVER_H

#include "events/EventLogger.h"
#include "interfaces/IWaterPump.h"
#include "network/WifiManager.h"
#include "network/WifiState.h"
#include "time/SntpClient.h"

/**
 * @brief Detects WiFi/pump state transitions and forwards them to EventLogger.
 *
 * Construct once at the wiring site after the logger, WifiManager and pumps
 * exist, then call poll() once per 10 Hz main-loop iteration. Unsynchronized
 * by design: poll() runs from the single main loop; WifiManager exposes an
 * immutable snapshot() and the pump handles are the LockedWaterPump wrappers.
 */
class SystemObserver {
public:
    /**
     * @brief Inject the logger, the (nullable) WifiManager, the SNTP client and
     *        the pump handles.
     *
     * @param logger    Typed event sink (borrowed; must outlive this observer).
     * @param wifi      WiFi state source, or nullptr in provisioning/headless
     *                  mode (WiFi transitions are then simply not observed).
     * @param sntp      SNTP client to start on the first Connected transition,
     *                  or nullptr to never start SNTP (borrowed). SNTP is thus
     *                  never started in provisioning/headless mode (wifi is
     *                  nullptr) nor when no client is injected — the wall clock
     *                  simply stays "time not set" (feature 008 US3).
     * @param plant     Plant pump handle, or nullptr if absent.
     * @param reservoir Reservoir pump handle, or nullptr on single-pump boards.
     */
    SystemObserver(EventLogger& logger, WifiManager* wifi, SntpClient* sntp,
                   IWaterPump* plant, IWaterPump* reservoir = nullptr)
        : logger_(logger), wifi_(wifi), sntp_(sntp), plant_(plant),
          reservoir_(reservoir)
    {
    }

    /**
     * @brief Sample WiFi + pump state once and emit an event per transition.
     *
     * Non-blocking, null-safe. The first observed WiFi state logs once; each
     * subsequent state change logs once. A pump off→on logs a start, on→off
     * logs a stop (cause best-effort). Never affects watering.
     */
    void poll();

private:
    void pollWifi();
    void pollPump(IWaterPump* pump, const char* name, bool& lastRunning);

    EventLogger& logger_;
    WifiManager* wifi_;
    SntpClient* sntp_;
    IWaterPump* plant_;
    IWaterPump* reservoir_;

    // Last-seen WiFi state; haveWifiState_ stays false until the first poll so
    // the initial state is logged exactly once.
    WifiState lastWifiState_ = WifiState::Provisioning;
    bool haveWifiState_ = false;

    // SNTP is started exactly once, on the first Connected transition where
    // start() reports success. SntpClient::start() is itself idempotent; this
    // guard avoids calling it on every later Connected transition, but a failed
    // init leaves it false so the next Connected transition retries.
    bool sntpStarted_ = false;

    // Last observed EventLogger::droppedEvents() value. The pure logger only
    // counts a failed store; poll() gives that counter a target-side voice by
    // ESP_LOGW-ing the delta whenever it increases (mirrors EspWifiDriver's
    // dropped-counter pattern).
    uint32_t lastDropped_ = 0;

    // Pumps are forced OFF at boot (app_main invariant), so "not running" is
    // the correct initial edge baseline — no spurious start on the first poll.
    bool plantLastRunning_ = false;
    bool reservoirLastRunning_ = false;
};

#endif /* WATERINGSYSTEM_MAIN_SYSTEM_OBSERVER_H */
