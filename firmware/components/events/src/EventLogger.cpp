// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file EventLogger.cpp
 * @brief Typed event producers + reset-reason name mapping (pure).
 *
 * No IDF/esp_* includes: detail strings are built with std::string only and
 * category constants come from IDataStorage. See EventLogger.h for the
 * behavioural contract.
 */

#include "events/EventLogger.h"

const char* resetReasonName(int espResetReason)
{
    // Mirrors esp_reset_reason_t (esp_system.h) by integer value so this stays
    // pure (no IDF include). Total: any unlisted value maps to "UNKNOWN".
    switch (espResetReason) {
        case 0:  return "UNKNOWN";    // ESP_RST_UNKNOWN
        case 1:  return "POWERON";    // ESP_RST_POWERON
        case 2:  return "EXT";        // ESP_RST_EXT
        case 3:  return "SW";         // ESP_RST_SW
        case 4:  return "PANIC";      // ESP_RST_PANIC
        case 5:  return "INT_WDT";    // ESP_RST_INT_WDT
        case 6:  return "TASK_WDT";   // ESP_RST_TASK_WDT
        case 7:  return "WDT";        // ESP_RST_WDT
        case 8:  return "DEEPSLEEP";  // ESP_RST_DEEPSLEEP
        case 9:  return "BROWNOUT";   // ESP_RST_BROWNOUT
        case 10: return "SDIO";       // ESP_RST_SDIO
        case 11: return "USB";        // ESP_RST_USB
        case 12: return "JTAG";       // ESP_RST_JTAG
        case 13: return "EFUSE";      // ESP_RST_EFUSE
        case 14: return "PWR_GLITCH"; // ESP_RST_PWR_GLITCH
        case 15: return "CPU_LOCKUP"; // ESP_RST_CPU_LOCKUP
        default: return "UNKNOWN";
    }
}

void EventLogger::emit(uint8_t category, const std::string& detail)
{
    // Never throws / never blocks watering: a failed append is counted only
    // (pure component — no ESP_LOGW). The store truncates over-long detail at
    // kEventDetailMaxLen, so producers need not pre-truncate.
    if (!storage_.storeEvent(clock_.nowEpoch(), category, detail)) {
        ++droppedEvents_;
    }
}

void EventLogger::logReset(int reason)
{
    // The detail carries the mapped human-readable name only (contract example:
    // "reset=TASK_WDT"); mapping internally makes a name mismatch unrepresentable.
    emit(IDataStorage::kCategoryReset,
         std::string("reset=") + resetReasonName(reason));
}

void EventLogger::logWifi(const char* stateName)
{
    // The STATE name only — never the SSID/password (FR-004).
    emit(IDataStorage::kCategoryConnectivity,
         std::string("wifi=") + (stateName ? stateName : "unknown"));
}

void EventLogger::logPumpStart(const char* pump, const char* cause)
{
    emit(IDataStorage::kCategoryPump,
         std::string("pump=") + (pump ? pump : "?") + " start cause=" +
             (cause ? cause : "unknown"));
}

void EventLogger::logPumpStop(const char* pump, const char* cause)
{
    emit(IDataStorage::kCategoryPump,
         std::string("pump=") + (pump ? pump : "?") + " stop cause=" +
             (cause ? cause : "unknown"));
}

void EventLogger::logFailsafe(const char* detail)
{
    emit(IDataStorage::kCategoryFailsafe, detail ? detail : "");
}

void EventLogger::logOta(const char* detail)
{
    emit(IDataStorage::kCategoryOta, detail ? detail : "");
}
