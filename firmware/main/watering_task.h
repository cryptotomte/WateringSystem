// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file watering_task.h
 * @brief Decision-layer watering task (app wiring, feature 011).
 *
 * App-level FreeRTOS task, not a component: it runs the pure WateringController
 * (and, on boards with a reservoir pump, the ReservoirController) at the sensor-
 * read cadence. The controller acts as the periodic soil reader — its tick()
 * performs the blocking Modbus read that refreshes the LockedSoilSensor cache
 * (the same cache the /sensors endpoint serves), so no separate soil-reader task
 * is needed. The 10 Hz main loop still owns precise pump-timing enforcement.
 * Task/behaviour contract: specs/011-watering-controller-host-tests/.
 */

#ifndef WATERINGSYSTEM_MAIN_WATERING_TASK_H
#define WATERINGSYSTEM_MAIN_WATERING_TASK_H

#include "board/board.h"
#include "control/WateringController.h"
#include "interfaces/IConfigStore.h"
#if BOARD_HAS_RESERVOIR_PUMP
#include "control/ReservoirController.h"
#endif

/**
 * @brief Start the decision-layer watering task.
 *
 * Pass the Locked*-backed controllers built in app_main; they must outlive the
 * task (i.e. forever — function-local statics). The task subscribes to the task
 * WDT and ticks every IConfigStore::getSensorReadIntervalMs() (re-read each
 * loop so config changes take effect), floored at 1000 ms. A task-creation
 * failure is logged and swallowed — like the sensor task, the decision layer is
 * started best-effort and the 10 Hz safety loop is unaffected.
 *
 * @param controller Automatic + manual plant watering logic (soil reader).
 * @param reservoir  Reservoir auto-fill state machine (rev1 only).
 * @param config     Runtime-tunable cadence and mode flag, re-read each tick.
 */
#if BOARD_HAS_RESERVOIR_PUMP
void watering_task_start(WateringController& controller, ReservoirController& reservoir,
                         IConfigStore& config);
#else
void watering_task_start(WateringController& controller, IConfigStore& config);
#endif

#endif /* WATERINGSYSTEM_MAIN_WATERING_TASK_H */
