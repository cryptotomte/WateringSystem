// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ITimeProvider.h
 * @brief Injected monotonic time source (milliseconds).
 *
 * Time is injected so that all timing/safety logic is deterministic under
 * host tests (FakeTimeProvider) and never calls esp_timer directly —
 * esp_timer is not simulated on the IDF linux preview target.
 *
 * Part of the header-only `interfaces` component: no IDF includes allowed.
 */

#ifndef WATERINGSYSTEM_INTERFACES_ITIMEPROVIDER_H
#define WATERINGSYSTEM_INTERFACES_ITIMEPROVIDER_H

#include <cstdint>

/**
 * @brief Monotonic millisecond clock.
 */
class ITimeProvider {
public:
    virtual ~ITimeProvider() = default;

    /**
     * @brief Current monotonic time in milliseconds.
     *
     * Never decreases. int64_t milliseconds cannot wrap within the device
     * lifetime.
     */
    virtual int64_t nowMs() = 0;
};

#endif /* WATERINGSYSTEM_INTERFACES_ITIMEPROVIDER_H */
