// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file sensor_task.h
 * @brief Periodic environmental sensor poller (app wiring, feature 005).
 *
 * App-level FreeRTOS task, not a component: PR-11's watering controller is
 * expected to absorb this cadence (research.md R7). Task/behavior contract:
 * specs/005-bme280-i2c/contracts/interfaces.md ("Sensor task").
 */

#ifndef WATERINGSYSTEM_MAIN_SENSOR_TASK_H
#define WATERINGSYSTEM_MAIN_SENSOR_TASK_H

#include "interfaces/IEnvironmentalSensor.h"

/**
 * @brief Start the 5 s environmental sensor poll task.
 *
 * Pass the LockedEnvironmentalSensor decorator, never the raw sensor — the
 * task reads concurrently with the console REPL's `env` command (and
 * PR-09/PR-11 consumers later). The task starts even when the sensor
 * failed initialization (lazy re-init recovers later — parity), never
 * exits and never reboots on failures; publishing IS the locked sensor
 * itself (last-good values + getLastError()).
 *
 * Call once, after diag console registration in app_main. A task-creation
 * failure is logged and swallowed — the poller is not a safety function.
 *
 * @param sensor Polled every 5 s; must outlive the task (i.e. forever —
 *               pass a function-local static from app_main).
 */
void sensor_task_start(IEnvironmentalSensor& sensor);

#endif /* WATERINGSYSTEM_MAIN_SENSOR_TASK_H */
