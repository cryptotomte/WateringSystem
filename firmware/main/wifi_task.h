// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file wifi_task.h
 * @brief WiFi station tick task (app wiring, feature 007 US2).
 *
 * App-level FreeRTOS task, not a component — mirrors main/sensor_task.h. It is
 * a SEPARATE task from the 10 Hz pump/level loop and shares no mutex with the
 * watering path (FR-014): all it does is call WifiManager::tick() at a fixed
 * cadence and reflect the connection state on the status LED. Task/behavior
 * contract: specs/007-wifi-provisioning/contracts/wifi-manager-states.md
 * (tick cadence) and docs/parity-checklist.md §7/§9 (LED).
 */

#ifndef WATERINGSYSTEM_MAIN_WIFI_TASK_H
#define WATERINGSYSTEM_MAIN_WIFI_TASK_H

#include "network/WifiManager.h"

/**
 * @brief Start the WiFi station tick task (station mode only).
 *
 * The task calls manager.tick() every ~250 ms (non-blocking; the manager
 * advances purely from time deltas + drained driver events) and drives
 * BOARD_PIN_STATUS_LED: ~500 ms toggle while Connecting/Reconnecting, steady
 * on when Connected, off otherwise. Started only when the device boots in
 * station mode — provisioning brings up the AP + portal instead and does not
 * run this task.
 *
 * Call once, from app_main, after WifiManager::begin(Station). A task-creation
 * failure is logged and swallowed — WiFi is not a safety function and the
 * watering path is unaffected.
 *
 * @param manager Ticked forever; must outlive the task (pass a function-local
 *                static from app_main).
 */
void wifi_task_start(WifiManager& manager);

#endif /* WATERINGSYSTEM_MAIN_WIFI_TASK_H */
