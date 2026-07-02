// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file MockEnvironmentalSensor.h
 * @brief Scriptable IEnvironmentalSensor test double (header-only).
 *
 * Serves the host tests of environmental-sensor CONSUMERS (watering
 * controller PR-11, web server PR-09): script a sequence of read outcomes
 * with the consistency helpers and assert on the call counters. The
 * driver's own probe/calibration/compensation logic is tested against the
 * REAL Bme280Sensor via MockI2cBus — never through this mock. Never
 * compiled into target builds (only included from test code). No IDF
 * includes.
 *
 * Consistency helpers from the start (PR-04 lesson — MockSoilSensor's
 * separate result/error/value fields let tests script incoherent states):
 * scriptSuccessfulRead()/scriptFailedRead() keep outcome, error code and
 * getter values coherent per step, enforcing the interface's last-good
 * contract (a failed read never touches the values).
 */

#ifndef WATERINGSYSTEM_SENSORS_TESTING_MOCKENVIRONMENTALSENSOR_H
#define WATERINGSYSTEM_SENSORS_TESTING_MOCKENVIRONMENTALSENSOR_H

#include <cstddef>
#include <vector>

#include "interfaces/IEnvironmentalSensor.h"

/**
 * @brief IEnvironmentalSensor replaying a scripted read sequence,
 * instrumented for tests.
 *
 * Each read() consumes the next scripted step; when the script runs out,
 * the LAST step repeats forever (a steady sensor keeps delivering, a dead
 * one keeps failing — matches the real driver's persistence). An entirely
 * unscripted read() succeeds with error 0 and whatever values are current
 * (the 0.0 placeholders before any successful step — same
 * meaningless-before-first-read contract as the real driver).
 * initialize()/isAvailable() results are plain scripted fields
 * (MockSoilSensor style); per the interface contract neither touches the
 * error code.
 */
class MockEnvironmentalSensor : public IEnvironmentalSensor {
public:
    // -- Scripted results (non-read calls) -----------------------------------

    bool initializeResult = true;   ///< returned by initialize()
    bool isAvailableResult = true;  ///< returned by isAvailable()

    // -- Instrumentation ------------------------------------------------------

    int initializeCalls = 0;
    int readCalls = 0;
    int isAvailableCalls = 0;

    // -- Consistency helpers (script one read() outcome each, FIFO) ----------

    /// Script the next read() to succeed: returns true, error 0, and the
    /// getters serve exactly these values afterwards.
    void scriptSuccessfulRead(float temperature, float humidity,
                              float pressure)
    {
        script_.push_back({true, 0, temperature, humidity, pressure});
    }

    /// Script the next read() to fail with @p error (1 = not found,
    /// 2 = read failed): returns false and leaves the getter values
    /// untouched (last-good contract).
    void scriptFailedRead(int error)
    {
        script_.push_back({false, error, 0.0f, 0.0f, 0.0f});
    }

    // -- IEnvironmentalSensor -------------------------------------------------

    bool initialize() override
    {
        ++initializeCalls;
        return initializeResult;
    }

    bool read() override
    {
        ++readCalls;
        if (script_.empty()) {
            // Unscripted: succeed with the current values (placeholders
            // before the first scripted success).
            lastError_ = 0;
            return true;
        }
        const Step& step = script_[next_];
        if (next_ + 1 < script_.size()) {
            ++next_;  // exhausted scripts repeat the last step forever
        }
        if (!step.ok) {
            lastError_ = step.error;
            return false;  // values untouched — last-good contract
        }
        temperature_ = step.temperature;
        humidity_ = step.humidity;
        pressure_ = step.pressure;
        lastError_ = 0;
        return true;
    }

    bool isAvailable() override
    {
        ++isAvailableCalls;
        return isAvailableResult;
    }

    int getLastError() override { return lastError_; }

    float getTemperature() override { return temperature_; }
    float getHumidity() override { return humidity_; }
    float getPressure() override { return pressure_; }

private:
    /// One scripted read() outcome; values are only meaningful when ok.
    struct Step {
        bool ok;
        int error;
        float temperature;
        float humidity;
        float pressure;
    };

    std::vector<Step> script_;
    size_t next_ = 0;
    int lastError_ = 0;

    // Last-good values (placeholders 0.0 before the first scripted success).
    float temperature_ = 0.0f;
    float humidity_ = 0.0f;
    float pressure_ = 0.0f;
};

#endif /* WATERINGSYSTEM_SENSORS_TESTING_MOCKENVIRONMENTALSENSOR_H */
