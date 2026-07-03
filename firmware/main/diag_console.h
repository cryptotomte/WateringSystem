// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file diag_console.h
 * @brief Serial diagnostic REPL (esp_console) for rig testing.
 *
 * Temporary scope per specs/002-pump-gpio-board/contracts/
 * serial-diagnostic.md — full FR12 diagnostics arrive in later phases.
 */

#ifndef WATERINGSYSTEM_MAIN_DIAG_CONSOLE_H
#define WATERINGSYSTEM_MAIN_DIAG_CONSOLE_H

#include "esp_err.h"
#include "interfaces/IConfigStore.h"
#include "interfaces/IDataStorage.h"
#include "interfaces/IEnvironmentalSensor.h"
#include "interfaces/ILevelSensor.h"
#include "interfaces/IModbusClient.h"
#include "interfaces/ISoilSensor.h"
#include "interfaces/IWaterPump.h"

/**
 * @brief Register the pump instances the console commands operate on.
 *
 * Must be called before diag_console_start(). Plain pointer registration —
 * no static constructors involved (boot fail-safe rule).
 */
void diag_console_register_pumps(IWaterPump& plant, IWaterPump& reservoir);

/**
 * @brief Register the storage instances the `config`/`storage` commands
 *        operate on (HIL verification path for feature 003).
 *
 * Pass the Locked* decorators, never the raw stores — the console handlers
 * run on the REPL task, concurrently with the main task (FR-013). Must be
 * called before diag_console_start(); plain pointer registration.
 */
void diag_console_register_storage(IConfigStore& config,
                                   IDataStorage& storage);

/**
 * @brief Register the soil sensor + Modbus client the `soil`/`rs485test`
 *        commands operate on (HIL verification path for feature 004).
 *
 * Pass the LockedSoilSensor decorator, never the raw sensor — the console
 * handlers run on the REPL task, concurrently with the main-loop reader
 * arriving in PR-11. The client may be passed raw: in this PR it is only
 * reached from the REPL task (directly and via the locked sensor). Must be
 * called before diag_console_start(); plain pointer registration.
 */
void diag_console_register_soil(ISoilSensor& sensor, IModbusClient& client);

/**
 * @brief Register the environmental sensor the `env` command operates on
 *        (HIL verification path for feature 005).
 *
 * Pass the LockedEnvironmentalSensor decorator, never the raw sensor — the
 * console handler runs on the REPL task, concurrently with the 5 s sensor
 * task. Must be called before diag_console_start(); plain pointer
 * registration.
 */
void diag_console_register_env(IEnvironmentalSensor& sensor);

/**
 * @brief Register the two level sensors the `level` command operates on
 *        (HIL verification path for feature 006).
 *
 * Pass the LockedLevelSensor decorators, never the raw sensors — the
 * console handler runs on the REPL task, concurrently with the 10 Hz
 * main-loop update(). Must be called before diag_console_start(); plain
 * pointer registration.
 *
 * @param low  Low-mark sensor (BOARD_PIN_LEVEL_LOW).
 * @param high High-mark sensor (BOARD_PIN_LEVEL_HIGH).
 */
void diag_console_register_level(ILevelSensor& low, ILevelSensor& high);

/**
 * @brief Start the UART REPL (prompt "ws>") and register the commands.
 *
 * @return ESP_OK on success, the failing esp_err_t otherwise.
 */
esp_err_t diag_console_start(void);

#endif /* WATERINGSYSTEM_MAIN_DIAG_CONSOLE_H */
