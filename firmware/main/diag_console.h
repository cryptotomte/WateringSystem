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

#include "board/board.h"
#include "esp_err.h"
#include "interfaces/IConfigStore.h"
#include "interfaces/IDataStorage.h"
#include "interfaces/IEnvironmentalSensor.h"
#include "interfaces/ILevelSensor.h"
#include "interfaces/IModbusClient.h"
#include "interfaces/IPowerSensor.h"
#include "interfaces/ISoilSensor.h"
#include "interfaces/IWallClock.h"
#include "interfaces/IWaterPump.h"
#include "network/WifiManager.h"
#include "time/SyncStatus.h"

/**
 * @brief Register the pump instances the console commands operate on.
 *
 * Capability-aware signature (feature 006, FR-007): on single-pump boards
 * (BOARD_HAS_RESERVOIR_PUMP == 0) only the plant pump is registered and
 * `pump reservoir ...` does not exist — compile-time absence, so PR-14's
 * "exactly one pump" check sees a usage error, never a runtime
 * "unavailable".
 *
 * Must be called before diag_console_start(). Plain pointer registration —
 * no static constructors involved (boot fail-safe rule).
 */
#if BOARD_HAS_RESERVOIR_PUMP
void diag_console_register_pumps(IWaterPump& plant, IWaterPump& reservoir);
#else
void diag_console_register_pumps(IWaterPump& plant);
#endif

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

#if BOARD_HAS_INA226
/**
 * @brief Register the power sensor the `power` command operates on
 *        (bring-up path for PR-14; feature 006).
 *
 * Only exists on INA226-equipped boards (rev2) — the `power` command is
 * compiled out elsewhere. Pass the LockedPowerSensor decorator, never the
 * raw sensor — the console handler runs on the REPL task, and PR-09/PR-11
 * add further readers. Must be called before diag_console_start(); plain
 * pointer registration.
 */
void diag_console_register_power(IPowerSensor& sensor);
#endif

/**
 * @brief Register the WiFi manager the `wifi` status command reads (feature
 *        007 US2).
 *
 * Pass the station-mode WifiManager, or nullptr when the device booted in
 * provisioning mode (no station manager exists) — the `wifi` command then
 * reports "not available". The command only reads the manager's immutable
 * snapshot() (single acquisition), never the raw state, and never echoes
 * credentials (FR-004). Must be called before diag_console_start(); plain
 * pointer registration.
 *
 * @param manager Station manager, or nullptr in provisioning/unconfigured mode.
 */
void diag_console_register_wifi(WifiManager* manager);

/**
 * @brief Register the wall clock + SNTP sync status the `time` command reads
 *        (feature 008 US1).
 *
 * Both pointers are nullptr-tolerant: pass nullptr before the clock/SNTP
 * client exist (the `time` command then reports "time not set"). The
 * construction/wiring into app_main arrives with US3; this PR only provides
 * the command and its register hook. The command reads the injected clock and
 * status directly (no credentials involved). Must be called before
 * diag_console_start(); plain pointer registration.
 *
 * @param clock  Wall-clock source (SystemWallClock on target), or nullptr.
 * @param sync   SNTP sync status (owned by SntpClient), or nullptr.
 */
void diag_console_register_time(IWallClock* clock, const SyncStatus* sync);

/**
 * @brief Start the UART REPL (prompt "ws>") and register the commands.
 *
 * @return ESP_OK on success, the failing esp_err_t otherwise.
 */
esp_err_t diag_console_start(void);

#endif /* WATERINGSYSTEM_MAIN_DIAG_CONSOLE_H */
