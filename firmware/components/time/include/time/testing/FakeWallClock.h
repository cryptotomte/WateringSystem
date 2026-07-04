// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file FakeWallClock.h
 * @brief Deterministic fake wall clock for host tests (header-only).
 *
 * Implements IWallClock with a settable epoch. "is set" is derived from a
 * settable plausibility threshold (default: the 2020-01-01Z epoch, matching
 * TimeService::kMinPlausibleEpoch) so consumer tests (EventLogger, the time
 * console line) can exercise both the not-yet-set and synced states without
 * any IDF or real clock. Never compiled into target builds — included only
 * from host test code. No IDF includes.
 */

#ifndef WATERINGSYSTEM_TIME_TESTING_FAKEWALLCLOCK_H
#define WATERINGSYSTEM_TIME_TESTING_FAKEWALLCLOCK_H

#include <cstdint>

#include "interfaces/IWallClock.h"

/**
 * @brief Manually settable wall clock for deterministic host tests.
 */
class FakeWallClock : public IWallClock {
public:
    /// 2020-01-01T00:00:00Z — mirrors TimeService::kMinPlausibleEpoch.
    static constexpr uint32_t kDefaultThreshold = 1577836800u;

    /// Start unset (epoch 0) unless a starting epoch is supplied.
    explicit FakeWallClock(uint32_t startEpoch = 0,
                           uint32_t setThreshold = kDefaultThreshold)
        : epoch_(startEpoch), threshold_(setThreshold) {}

    uint32_t nowEpoch() const override { return epoch_; }

    bool isTimeSet() const override { return epoch_ >= threshold_; }

    /// Set the current epoch (e.g. simulate an SNTP sync).
    void setEpoch(uint32_t epoch) { epoch_ = epoch; }

    /// Adjust the "is set" plausibility threshold.
    void setThreshold(uint32_t threshold) { threshold_ = threshold; }

private:
    uint32_t epoch_;
    uint32_t threshold_;
};

#endif /* WATERINGSYSTEM_TIME_TESTING_FAKEWALLCLOCK_H */
