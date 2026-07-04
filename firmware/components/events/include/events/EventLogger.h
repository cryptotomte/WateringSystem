// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file EventLogger.h
 * @brief Typed producers for the PR-06 persistent event log (feature 008 US2).
 *
 * Composes an IDataStorage& (the cross-task LockedDataStorage when shared) and
 * an IWallClock&; each producer picks the event category, builds a
 * deterministic detail string and calls storage.storeEvent(clock.nowEpoch(),
 * category, detail). Normative contract:
 * specs/008-sntp-watchdog-logging/contracts/event-logger.md.
 *
 * PURE by design: no IDF/esp_* includes, no WifiState/esp_reset_reason_t enums
 * in the signatures — producers take primitive args (int reset reason,
 * const char* state/pump/cause strings) so this lives in the `events`
 * component that depends only on `interfaces` and is host-testable against
 * MockDataStorage + FakeWallClock.
 *
 * Never throws, never blocks watering: a storeEvent() returning false is
 * counted (droppedEvents()) and dropped. A pure component cannot ESP_LOGW, so
 * the counter is the surfaced signal — target callers may log it periodically.
 * Credential VALUES are never logged (WiFi events carry the state name only).
 */

#ifndef WATERINGSYSTEM_EVENTS_EVENTLOGGER_H
#define WATERINGSYSTEM_EVENTS_EVENTLOGGER_H

#include <cstdint>
#include <string>

#include "interfaces/IDataStorage.h"
#include "interfaces/IWallClock.h"

/**
 * @brief Map an esp_reset_reason_t integer value to a short name.
 *
 * Pure free function (no IDF include): the caller passes
 * static_cast<int>(esp_reset_reason()). Total over the ESP_RST_* values known
 * at authoring time (0..10); any other value returns "UNKNOWN". Host-tested.
 */
const char* resetReasonName(int espResetReason);

/**
 * @brief Builds + persists typed events over an injected IDataStorage.
 */
class EventLogger {
public:
    /// Holds references (not ownership); both must outlive the logger. Pass the
    /// cross-task LockedDataStorage as `storage` when the store is shared.
    EventLogger(IDataStorage& storage, IWallClock& clock)
        : storage_(storage), clock_(clock) {}

    /// kCategoryReset, detail "reset=<reasonName>" (e.g. "reset=TASK_WDT").
    /// `reason` is the raw esp_reset_reason_t int (kept for the caller's API
    /// symmetry; the detail carries the human-readable name).
    void logReset(int reason, const char* reasonName);

    /// kCategoryConnectivity, detail "wifi=<stateName>" (e.g. "wifi=Connected").
    void logWifi(const char* stateName);

    /// kCategoryPump, detail "pump=<pump> start cause=<cause>".
    void logPumpStart(const char* pump, const char* cause);

    /// kCategoryPump, detail "pump=<pump> stop cause=<cause>".
    void logPumpStop(const char* pump, const char* cause);

    /// kCategoryFailsafe, detail passed verbatim (producer is PR-11).
    void logFailsafe(const char* detail);

    /// kCategoryOta, detail passed verbatim (producer is PR-13).
    void logOta(const char* detail);

    /// Number of events dropped because storeEvent() returned false. Never
    /// resets; a pure component's only failure signal (no ESP_LOGW here).
    uint32_t droppedEvents() const { return droppedEvents_; }

private:
    /// Single write path: stamp with the wall clock, store, count a failure.
    /// Never throws.
    void emit(uint8_t category, const std::string& detail);

    IDataStorage& storage_;
    IWallClock& clock_;
    uint32_t droppedEvents_ = 0;
};

#endif /* WATERINGSYSTEM_EVENTS_EVENTLOGGER_H */
