// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file FakeTimeProvider.h
 * @brief Deterministic fake clock for host tests (header-only).
 *
 * Time only moves when the test calls advance() — no sleeps, fully
 * deterministic enforcement testing. Never compiled into target builds
 * (only included from test code).
 */

#ifndef WATERINGSYSTEM_ACTUATORS_TESTING_FAKETIMEPROVIDER_H
#define WATERINGSYSTEM_ACTUATORS_TESTING_FAKETIMEPROVIDER_H

#include <cstdint>

#include "interfaces/ITimeProvider.h"

/**
 * @brief Manually advanced monotonic clock.
 */
class FakeTimeProvider : public ITimeProvider {
public:
    /// Start at an arbitrary (non-zero) epoch to catch 0-assumptions.
    explicit FakeTimeProvider(int64_t startMs = 1'000'000) : nowMs_(startMs) {}

    int64_t nowMs() override { return nowMs_; }

    /// Move the clock forward by ms (monotonic: ms must be >= 0).
    void advance(int64_t ms) { nowMs_ += ms; }

private:
    int64_t nowMs_;
};

#endif /* WATERINGSYSTEM_ACTUATORS_TESTING_FAKETIMEPROVIDER_H */
