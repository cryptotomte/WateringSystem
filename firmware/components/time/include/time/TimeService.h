// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file TimeService.h
 * @brief Pure wall-clock helpers: epoch plausibility + Swedish local-time
 *        formatting.
 *
 * Pure C++ (no IDF, no esp_*): builds on both the linux preview host and the
 * ESP32 target. formatLocal() renders via <ctime>/localtime_r, which both
 * platforms provide, and assumes the process timezone has already been set to
 * `CET-1CEST,M3.5.0,M10.5.0/3` (done on target by SntpClient::applyTimezone(),
 * and in the host test by setenv("TZ", ...)+tzset()).
 */

#ifndef WATERINGSYSTEM_TIME_TIMESERVICE_H
#define WATERINGSYSTEM_TIME_TIMESERVICE_H

#include <cstdint>
#include <string>

/**
 * @brief Stateless wall-clock utilities (plausibility + local-time render).
 */
class TimeService {
public:
    /// 2020-01-01T00:00:00Z — the lower bound for a "set" wall clock. Anything
    /// below this is treated as an un-synced boot value (matches
    /// FakeWallClock::kDefaultThreshold and IWallClock::isTimeSet()).
    static constexpr uint32_t kMinPlausibleEpoch = 1577836800u;

    /**
     * @brief True iff @p e is at or beyond the plausibility threshold.
     * @param e Candidate epoch seconds.
     */
    static bool isPlausibleEpoch(uint32_t e) { return e >= kMinPlausibleEpoch; }

    /**
     * @brief Render @p epoch as Swedish local time "YYYY-MM-DD HH:MM:SS +ZZZZ".
     *
     * Uses the process timezone (assumed already set to
     * `CET-1CEST,M3.5.0,M10.5.0/3`): winter -> CET (+0100), summer -> CEST
     * (+0200), with the DST boundaries at the last Sunday of March/October.
     *
     * @param epoch Epoch seconds to convert.
     * @return Formatted local timestamp string.
     */
    static std::string formatLocal(uint32_t epoch);
};

#endif /* WATERINGSYSTEM_TIME_TIMESERVICE_H */
