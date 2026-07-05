// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file TimeService.cpp
 * @brief Pure implementation of the TimeService local-time formatter.
 *
 * No IDF / esp_* dependencies: compiled into BOTH the linux host build and the
 * ESP32 target build. localtime_r honours the process timezone set elsewhere
 * (SntpClient::applyTimezone() on target; setenv+tzset in host tests).
 */

#include "time/TimeService.h"

#include <ctime>

std::string TimeService::formatLocal(uint32_t epoch)
{
    time_t t = static_cast<time_t>(epoch);
    struct tm lt;
    if (localtime_r(&t, &lt) == nullptr) {
        // Conversion failed (e.g. out-of-range time_t) — never format an
        // uninitialised tm; return a sentinel instead.
        return std::string("(time unavailable)");
    }
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %z", &lt);
    return std::string(buf);
}
