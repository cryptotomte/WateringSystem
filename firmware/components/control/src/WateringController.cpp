// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file WateringController.cpp
 * @brief Automatic watering logic implementation (pure C++, host-tested).
 *
 * Pure: no esp_* / esp_timer includes, no hardware or network calls. All time
 * is injected (ITimeProvider), all tunables are read from IConfigStore each
 * tick. See WateringController.h for the invariants and the data-model for the
 * decision tables.
 */

#include "control/WateringController.h"

void WateringController::tick()
{
    const int64_t now = clock_.nowMs();

    // Actuator layer first: enforce timed self-stop and the hard 300 s cap.
    plant_.update();

    // A burst that ended on its own (duration/cap self-stop) is the soak-gate
    // origin too — record the burst end so a self-stopped burst is not exempt
    // from the pause (FR-003). A stop we command at the high threshold sets the
    // same field inline below.
    if (burstActive_ && !plant_.isRunning()) {
        lastBurstEndMs_ = now;
        burstActive_ = false;
    }

    // Automatic path only. When automatic watering is disabled the mode is
    // manual/suspended (operator override, US3): take no automatic action.
    if (!config_.getWateringEnabled()) {
        return;
    }

    // ---- Read the sensor (US1 drives ISoilSensor directly; US3 wires a
    // LockedSoilSensor snapshot so read()+availability come from one locked
    // acquisition). We react only to the read RESULT and the availability
    // signal, never to specific numeric error codes. ------------------------
    const bool readOk = soil_.read();
    const bool available = soil_.isAvailable();
    const float moisture = soil_.getMoisture();
    const bool inRange =
        moisture >= kMoistureMinPct && moisture <= kMoistureMaxPct;
    const bool invalid = readOk && !inRange;

    // A fresh, in-range read is by definition not stale; record it before the
    // staleness test so the FIRST valid read is acted on (a fresh valid read is
    // never "stale", even when lastValidSoilMs_ was still 0).
    if (readOk && inRange) {
        lastValidSoilMs_ = now;
    }
    const bool stale =
        (lastValidSoilMs_ == 0) || (now - lastValidSoilMs_ > kStalenessMs);

    // ---- FAIL-SAFE (unconditional, checked BEFORE the soak gate) ----------
    // Never delayed or suppressed by the soak-pause/scheduling logic (FR-006).
    const char* failsafeReason = nullptr;
    if (!available) {
        failsafeReason = "soil-unavailable";
    } else if (invalid) {
        failsafeReason = "moisture-invalid";
    } else if (stale) {
        failsafeReason = "soil-stale";
    }
    if (failsafeReason != nullptr) {
        if (plant_.isRunning()) {
            plant_.stop();
            events_.logFailsafe(failsafeReason);
        }
        // Abandon any in-flight automatic burst; take no watering decision.
        burstActive_ = false;
        return;
    }

    // Gate on the read result (FR-004): a transient read failure while the last
    // valid reading is still within the staleness window is not a fail-safe
    // condition — wait for fresh data rather than act on placeholder values.
    if (!readOk) {
        return;
    }

    // ---- WATERING DECISION (soak gate is the LAST thing checked) -----------
    const float lowThreshold = config_.getMoistureThresholdLow();
    const float highThreshold = config_.getMoistureThresholdHigh();

    if (plant_.isRunning()) {
        if (moisture >= highThreshold) {
            // Target reached: stop and arm the soak pause from this burst end.
            plant_.stop();
            lastBurstEndMs_ = now;
            burstActive_ = false;
        }
        // else: keep running within the burst.
        return;
    }

    if (moisture <= lowThreshold) {
        const int64_t soakMs =
            static_cast<int64_t>(config_.getMinWateringIntervalS()) * 1000;
        const bool soakElapsed =
            (lastBurstEndMs_ == 0) || (now - lastBurstEndMs_ >= soakMs);
        if (soakElapsed) {
            if (plant_.runFor(static_cast<int>(config_.getWateringDurationS()))) {
                burstActive_ = true;
            }
        }
        // else: soak pause active — do NOT start another burst, even though the
        // soil still reads dry (FR-003).
    }
}
