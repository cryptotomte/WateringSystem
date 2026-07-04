// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_wifi.cpp
 * @brief Unity host suite for WiFi provisioning & station management
 * (feature 007).
 *
 * Phase 1/2 scaffolding (task T003): the suite entry point exists and links
 * so the runner and CMake wiring are in place. The real tests — credential
 * validation, boot-mode truth table, reconnect schedule, isolation and the
 * no-boot-loop property — arrive with the US1/US2/US3 phases (T008+),
 * exercising the pure WifiManager over MockWifiDriver + FakeTimeProvider.
 */

void run_wifi_tests(void)
{
    // Intentionally empty until the US1+ phases add real cases (T008+).
}
