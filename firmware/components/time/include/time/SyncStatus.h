// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file SyncStatus.h
 * @brief Pure value type tracking SNTP synchronisation state.
 *
 * Shared between TimeService (pure, host-tested) and SntpClient (target).
 * No IDF and no <ctime> includes — only <cstdint> and the pure
 * time/TimeService.h. Consumed by the console `time` line to report whether the
 * wall clock has ever been set and when.
 */

#ifndef WATERINGSYSTEM_TIME_SYNCSTATUS_H
#define WATERINGSYSTEM_TIME_SYNCSTATUS_H

#include <cstdint>

#include "time/TimeService.h"

/**
 * @brief Snapshot of the wall-clock synchronisation state.
 *
 * @var SyncStatus::lastSyncEpoch epoch seconds of the most recent successful
 *                                sync (0 while never synced).
 *
 * synced() is derived from lastSyncEpoch — one source of truth, so the illegal
 * "synced but epoch 0" pair cannot be represented.
 */
struct SyncStatus {
    uint32_t lastSyncEpoch = 0;

    /// True once a plausible time has been synced.
    bool synced() const
    {
        return lastSyncEpoch >= TimeService::kMinPlausibleEpoch;
    }
};

#endif /* WATERINGSYSTEM_TIME_SYNCSTATUS_H */
