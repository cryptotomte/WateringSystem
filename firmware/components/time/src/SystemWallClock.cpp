// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file SystemWallClock.cpp
 * @brief IWallClock over time(nullptr) — target-only (excluded on linux).
 */

#include "time/SystemWallClock.h"

#include <ctime>

#include "time/TimeService.h"

uint32_t SystemWallClock::nowEpoch() const
{
    return static_cast<uint32_t>(time(nullptr));
}

bool SystemWallClock::isTimeSet() const
{
    return TimeService::isPlausibleEpoch(nowEpoch());
}
