// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_event_logger.cpp
 * @brief Host suite for the EventLogger producers (feature 008).
 *
 * Registered by test_main.cpp via run_event_logger_tests(). Empty for now —
 * the per-producer formatting/category, write-failure counting and
 * reset-reason mapping tests (against MockDataStorage + FakeWallClock) are
 * added in T012 alongside the EventLogger implementation (T013).
 */

#include "unity.h"

// TODO(T012): register EventLogger formatting/category/failure tests here.
void run_event_logger_tests(void)
{
}
