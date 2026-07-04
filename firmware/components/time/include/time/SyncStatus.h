// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file SyncStatus.h
 * @brief Pure value type tracking SNTP synchronisation state.
 *
 * Shared between TimeService (pure, host-tested) and SntpClient (target).
 * No IDF and no <ctime> includes — only <cstdint>. Consumed by the console
 * `time` line to report whether the wall clock has ever been set and when.
 */

#ifndef WATERINGSYSTEM_TIME_SYNCSTATUS_H
#define WATERINGSYSTEM_TIME_SYNCSTATUS_H

#include <cstdint>

/**
 * @brief Snapshot of the wall-clock synchronisation state.
 *
 * @var SyncStatus::synced        true once a plausible time has been set.
 * @var SyncStatus::lastSyncEpoch epoch seconds of the most recent successful
 *                                sync (0 while never synced).
 */
struct SyncStatus {
    bool synced = false;
    uint32_t lastSyncEpoch = 0;
};

#endif /* WATERINGSYSTEM_TIME_SYNCSTATUS_H */
