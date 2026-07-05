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

    // ---- Read the sensor ONCE per tick (controller-as-reader: read() drives
    // the bus and refreshes the cache), then take ONE coherent, NON-BLOCKING
    // snapshot() for the availability + values we decide on — NO second bus
    // probe. We react only to the read RESULT and the availability signal,
    // never to specific numeric error codes. `available` now means "at least
    // one successful read on record": a sensor that has NEVER read OK is
    // !available -> "soil-unavailable"; a sensor that worked then stopped
    // responding keeps available true, the read fails, and it fails safe as
    // "soil-stale". Both stop the pump — only the reason string differs. ------
    soil_.read();                                // drives the bus, refreshes cache
    const SoilSnapshot soil = soil_.snapshot();  // one coherent, non-blocking tuple
    const bool readOk = soil.readOk;
    const bool available = soil.available;       // ever-read-ok; no second probe
    const float moisture = soil.moisture;
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

    // ---- PERIODIC DATA-LOG (runs in EVERY mode, before any early return) ----
    // Telemetry must be recorded even when automatic watering is disabled, a
    // manual override is active, or a fail-safe is about to fire (FR-014). Soil
    // is logged only when this tick's read was successful and in range.
    maybeLogData(now, /*soilValid=*/(readOk && inRange), soil);

    // ---- MANUAL OVERRIDE (bypasses fail-safe + soak/decision logic) --------
    // A manual run is an explicit operator override (FR-007/008): it is exempt
    // from the automatic fail-safe and the soak gate. A pump self-stop at the
    // 300 s cap clears the override on the next tick.
    if (manualRunActive_) {
        if (!plant_.isRunning()) {
            manualRunActive_ = false;
        }
        return;
    }

    // Automatic path only. When automatic watering is disabled the mode is
    // manual/suspended: take no automatic action.
    if (!config_.getWateringEnabled()) {
        return;
    }

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

bool WateringController::startManual(int durationS)
{
    // Clamp to the pump's accepted range [1, 300] s (the pump rejects 0 and
    // anything over its 300 s hard cap; we clamp rather than reject so an
    // operator command always maps to a bounded run).
    int clamped = durationS;
    if (clamped < 1) {
        clamped = 1;
    } else if (clamped > 300) {
        clamped = 300;
    }
    const bool started = plant_.runFor(clamped);
    if (started) {
        manualRunActive_ = true;
    }
    return started;
}

void WateringController::stop()
{
    plant_.stop();
    manualRunActive_ = false;
}

void WateringController::maybeLogData(int64_t now, bool soilValid,
                                     const SoilSnapshot& soil)
{
    // No plausible wall-clock timestamp yet — never log a bogus 1970 epoch.
    if (!wallClock_.isTimeSet()) {
        return;
    }

    // Interval gate: the first eligible log after time is set fires immediately
    // (lastDataLogMs_ == 0 counts as due); otherwise wait out the interval.
    const int64_t interval = static_cast<int64_t>(config_.getDataLogIntervalMs());
    const bool due =
        (lastDataLogMs_ == 0) || (now - lastDataLogMs_ >= interval);
    if (!due) {
        return;
    }
    lastDataLogMs_ = now;

    const uint32_t epoch = wallClock_.nowEpoch();

    // Environmental telemetry (only on a successful, available read).
    if (env_.read() && env_.isAvailable()) {
        storage_.storeSensorReading("env_temperature", epoch,
                                    env_.getTemperature());
        storage_.storeSensorReading("env_humidity", epoch, env_.getHumidity());
        storage_.storeSensorReading("env_pressure", epoch, env_.getPressure());
    }

    // Soil telemetry (uses the values from this tick's single snapshot(), so
    // the logged values match the values tick() decided on — one read/tick).
    if (soilValid) {
        storage_.storeSensorReading("soil_moisture", epoch, soil.moisture);
        storage_.storeSensorReading("soil_temperature", epoch,
                                    soil.temperature);
        // NOTE: soil humidity is deliberately NOT logged. ISoilSensor's
        // getHumidity() is documented as identical to getMoisture() (a single
        // moisture/humidity quantity in register 0x0000; the legacy driver
        // exposed it under both names), so logging it would be a pure duplicate
        // of soil_moisture. Dropping it keeps the max distinct metric set at
        // exactly kMaxMetrics (10): 3 env + 4 soil-base + 3 NPK.
        storage_.storeSensorReading("soil_ph", epoch, soil.ph);
        storage_.storeSensorReading("soil_ec", epoch, soil.ec);

        // NPK is only meaningful when >= 0 (the sensor reports -1 when a
        // channel is unsupported/unavailable); skip a negative channel.
        const float n = soil.nitrogen;
        const float p = soil.phosphorus;
        const float k = soil.potassium;
        if (n >= 0) {
            storage_.storeSensorReading("soil_nitrogen", epoch, n);
        }
        if (p >= 0) {
            storage_.storeSensorReading("soil_phosphorus", epoch, p);
        }
        if (k >= 0) {
            storage_.storeSensorReading("soil_potassium", epoch, k);
        }
    }
}
