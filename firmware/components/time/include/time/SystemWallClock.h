// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file SystemWallClock.h
 * @brief IWallClock over the system clock (time(nullptr)) — target only.
 *
 * The production wall-clock source: reads the C library system clock, which
 * SNTP steps once it syncs (SntpClient, step-set mode). Excluded from the
 * linux host build (host consumers use FakeWallClock); the declaration is
 * trivial so it lives target-side with its .cpp. isTimeSet() reuses the pure
 * TimeService plausibility threshold so it matches FakeWallClock exactly.
 */

#ifndef WATERINGSYSTEM_TIME_SYSTEMWALLCLOCK_H
#define WATERINGSYSTEM_TIME_SYSTEMWALLCLOCK_H

#include <cstdint>

#include "interfaces/IWallClock.h"

/**
 * @brief IWallClock backed by the system clock (SNTP-stepped on target).
 */
class SystemWallClock : public IWallClock {
public:
    uint32_t nowEpoch() const override;
    bool isTimeSet() const override;
};

#endif /* WATERINGSYSTEM_TIME_SYSTEMWALLCLOCK_H */
