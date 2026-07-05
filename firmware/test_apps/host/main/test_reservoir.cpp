// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_reservoir.cpp
 * @brief Host suite for the pure ReservoirController (feature 011, US2).
 *
 * The level truth table, stop-on-high, max-fill abort, post-abort cooldown
 * and feature-gate branches (US2) are added here in a later task (T009).
 * This file registers the suite entry point now so the host test app links
 * and runs empty until those tests land.
 */

#include "unity.h"

// TODO(T009): register the ReservoirController tests here.
void run_reservoir_tests(void)
{
}
