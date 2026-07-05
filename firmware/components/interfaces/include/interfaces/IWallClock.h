// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file IWallClock.h
 * @brief Injected wall-clock time source (epoch seconds).
 *
 * Separate from ITimeProvider (monotonic milliseconds for timing/safety):
 * this is calendar/wall-clock time used only for stamping events and
 * user-facing time display. It is injected so consumers (e.g. EventLogger)
 * stay deterministic under host tests (FakeWallClock) and never call
 * time()/esp_* directly.
 *
 * Part of the header-only `interfaces` component: no IDF and no <ctime>
 * includes allowed here — only <cstdint>.
 */

#ifndef WATERINGSYSTEM_INTERFACES_IWALLCLOCK_H
#define WATERINGSYSTEM_INTERFACES_IWALLCLOCK_H

#include <cstdint>

/**
 * @brief Wall-clock (calendar) time source in epoch seconds.
 */
class IWallClock {
public:
    virtual ~IWallClock() = default;

    /**
     * @brief Current wall-clock time in seconds since the Unix epoch.
     *
     * Returns a low/boot value (well below a plausible present-day epoch)
     * until the clock has been set (e.g. by SNTP). Callers that need to
     * distinguish "not yet set" MUST consult isTimeSet().
     */
    virtual uint32_t nowEpoch() const = 0;

    /**
     * @brief True iff nowEpoch() is at or beyond the plausibility threshold.
     *
     * False before the wall clock has been set from a trusted source.
     */
    virtual bool isTimeSet() const = 0;
};

#endif /* WATERINGSYSTEM_INTERFACES_IWALLCLOCK_H */
