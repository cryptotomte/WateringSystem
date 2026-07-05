// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file task_watchdog.h
 * @brief Thin wrappers over the IDF task watchdog (feature 008 US3) —
 *        target only.
 *
 * The task WDT configuration lives in Kconfig/sdkconfig: CONFIG_ESP_TASK_WDT_INIT
 * initialises the watchdog at boot and CONFIG_ESP_TASK_WDT_PANIC makes a timeout
 * panic (reboot). watchdog_init() only RECONFIGURES the runtime timeout to
 * CONFIG_WS_TASK_WDT_TIMEOUT_S; the watering-critical tasks then subscribe
 * themselves and feed the watchdog once per loop iteration.
 *
 * Subscription policy (contracts/task-watchdog.md): ONLY the watering-critical
 * tasks are subscribed — the 10 Hz main loop and the 5 s sensor task. The WiFi
 * task is deliberately NOT subscribed (a network stall must never reboot the
 * device, FR-014) and neither is the esp_console REPL (it blocks on UART by
 * design). On a non-serviced subscribed task the WDT panics → reboot; at the
 * next boot pumps_force_off() runs first (unchanged) and the reset reason is
 * logged as TASK_WDT.
 */

#ifndef WATERINGSYSTEM_MAIN_TASK_WATCHDOG_H
#define WATERINGSYSTEM_MAIN_TASK_WATCHDOG_H

#include "esp_err.h"

/**
 * @brief Ensure the task WDT runs with CONFIG_WS_TASK_WDT_TIMEOUT_S and
 *        panic-on-timeout enabled. Call once early in app_main.
 *
 * Idempotent with respect to the IDF boot-time init: because
 * CONFIG_ESP_TASK_WDT_INIT already initialised the watchdog, this reconfigures
 * the timeout via esp_task_wdt_reconfigure(). If the watchdog was NOT inited
 * (ESP_ERR_INVALID_STATE) it falls back to esp_task_wdt_init().
 *
 * @return ESP_OK on success, the failing esp_err_t otherwise (logged).
 */
esp_err_t watchdog_init();

/**
 * @brief Subscribe the CALLING task to the task WDT (esp_task_wdt_add(NULL)).
 *
 * Call once at the start of a watering-critical task, before its loop. A
 * failure is logged and non-fatal — the task simply is not watched.
 */
void watchdog_subscribe_current_task();

/**
 * @brief Feed the task WDT for the CALLING task (esp_task_wdt_reset()).
 *
 * Call once per loop iteration of a subscribed task.
 */
void watchdog_feed();

#endif /* WATERINGSYSTEM_MAIN_TASK_WATCHDOG_H */
