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
#include "interfaces/IWaterPump.h"

/**
 * @brief Register the pump instances the console commands operate on.
 *
 * Must be called before diag_console_start(). Plain pointer registration —
 * no static constructors involved (boot fail-safe rule).
 */
void diag_console_register_pumps(IWaterPump& plant, IWaterPump& reservoir);

/**
 * @brief Start the UART REPL (prompt "ws>") and register the pump command.
 *
 * @return ESP_OK on success, the failing esp_err_t otherwise.
 */
esp_err_t diag_console_start(void);

#endif /* WATERINGSYSTEM_MAIN_DIAG_CONSOLE_H */
