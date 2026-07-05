// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file MockSoilSensor.h
 * @brief Scriptable ISoilSensor test double (header-only).
 *
 * Serves the host tests of soil-sensor CONSUMERS (watering controller /
 * sensor manager, PR-11): set the eight quantity values and the
 * initialize()/read()/isAvailable() results, script the error code, and
 * assert on the call counters. The sensor's own decode/validation logic is
 * tested against the REAL ModbusSoilSensor via MockModbusClient — never
 * through this mock. Never compiled into target builds (only included from
 * test code). No IDF includes.
 *
 * Consistency helpers (PR-11, mirroring MockEnvironmentalSensor):
 * scriptSuccessfulRead()/scriptFailedRead() keep outcome, error code and
 * getter values coherent per read() step (a failed read never touches the
 * values — the interface's last-good contract), instead of hand-setting the
 * public fields into an incoherent combination. The plain public fields stay
 * for back-compat: with NO script, read() behaves exactly as before
 * (returns readResult, getters serve the manually-set values).
 */

#ifndef WATERINGSYSTEM_SENSORS_TESTING_MOCKSOILSENSOR_H
#define WATERINGSYSTEM_SENSORS_TESTING_MOCKSOILSENSOR_H

#include <cstddef>
#include <vector>

#include "interfaces/ISoilSensor.h"

/**
 * @brief ISoilSensor returning scripted values, instrumented for tests.
 *
 * All state is public (MockModbusClient/MockConfigStore style): assign the
 * result fields and quantity values before driving the consumer, then
 * assert on the counters/recorded arguments. The getters serve the scripted
 * values unconditionally — like the real sensor after a failed read() they
 * keep returning the last values, so consumers must gate on the read()
 * result / lastError, never on value plausibility (FR-005).
 */
class MockSoilSensor : public ISoilSensor {
public:
    // -- Scripted results ---------------------------------------------------

    bool initializeResult = true;  ///< returned by initialize()
    bool readResult = true;        ///< returned by read()
    bool isAvailableResult = true; ///< returned by isAvailable()
    bool calibrateResult = true;   ///< returned by all three calibrate*()
    int lastError = 0;             ///< returned by getLastError()

    // -- Scripted quantity values (served by the getters) --------------------

    float moisture = 0.0f;
    float temperature = 0.0f;
    float humidity = 0.0f;  ///< the real sensor keeps this ≡ moisture (parity)
    float ph = 0.0f;
    float ec = 0.0f;
    float nitrogen = 0.0f;
    float phosphorus = 0.0f;
    float potassium = 0.0f;

    // -- Instrumentation -----------------------------------------------------

    int initializeCalls = 0;
    int readCalls = 0;
    int isAvailableCalls = 0;
    /// Every calibrate*() reference-value argument, in call order (the
    /// vector size doubles as the per-quantity call counter).
    std::vector<float> calibrateMoistureCalls;
    std::vector<float> calibratePhCalls;
    std::vector<float> calibrateEcCalls;

    // -- Consistency helpers (script one read() outcome each, FIFO) ----------

    /// Script the next read() to succeed: read() returns true, error 0, and
    /// the getters serve exactly these values afterwards (readResult and the
    /// public value fields are updated coherently when the step is consumed).
    void scriptSuccessfulRead(float moistureValue, float temperatureValue,
                              float humidityValue, float phValue,
                              float ecValue, float nitrogenValue,
                              float phosphorusValue, float potassiumValue)
    {
        script_.push_back({true, 0, moistureValue, temperatureValue,
                           humidityValue, phValue, ecValue, nitrogenValue,
                           phosphorusValue, potassiumValue});
    }

    /// Script the next read() to fail with @p error: read() returns false and
    /// leaves the getter values untouched (last-good contract).
    void scriptFailedRead(int error)
    {
        script_.push_back(
            {false, error, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
    }

    // -- ISoilSensor ---------------------------------------------------------

    bool initialize() override
    {
        ++initializeCalls;
        return initializeResult;
    }

    bool read() override
    {
        ++readCalls;
        if (script_.empty()) {
            // Unscripted: legacy behaviour — return the plain field, getters
            // serve the manually-set values.
            return readResult;
        }
        const Step& step = script_[next_];
        if (next_ + 1 < script_.size()) {
            ++next_;  // exhausted scripts repeat the last step forever
        }
        if (!step.ok) {
            readResult = false;
            lastError = step.error;
            return false;  // values untouched — last-good contract
        }
        // Publish the coherent values into the public fields so the getters
        // (which serve those fields) stay in sync with the read outcome.
        moisture = step.moisture;
        temperature = step.temperature;
        humidity = step.humidity;
        ph = step.ph;
        ec = step.ec;
        nitrogen = step.nitrogen;
        phosphorus = step.phosphorus;
        potassium = step.potassium;
        readResult = true;
        lastError = 0;
        return true;
    }

    bool isAvailable() override
    {
        ++isAvailableCalls;
        return isAvailableResult;
    }

    int getLastError() override { return lastError; }

    float getMoisture() override { return moisture; }
    float getTemperature() override { return temperature; }
    float getHumidity() override { return humidity; }
    float getPH() override { return ph; }
    float getEC() override { return ec; }
    float getNitrogen() override { return nitrogen; }
    float getPhosphorus() override { return phosphorus; }
    float getPotassium() override { return potassium; }

    bool calibrateMoisture(float referenceValue) override
    {
        calibrateMoistureCalls.push_back(referenceValue);
        return calibrateResult;
    }

    bool calibratePH(float referenceValue) override
    {
        calibratePhCalls.push_back(referenceValue);
        return calibrateResult;
    }

    bool calibrateEC(float referenceValue) override
    {
        calibrateEcCalls.push_back(referenceValue);
        return calibrateResult;
    }

private:
    /// One scripted read() outcome; values are only meaningful when ok.
    struct Step {
        bool ok;
        int error;
        float moisture;
        float temperature;
        float humidity;
        float ph;
        float ec;
        float nitrogen;
        float phosphorus;
        float potassium;
    };

    std::vector<Step> script_;
    std::size_t next_ = 0;
};

#endif /* WATERINGSYSTEM_SENSORS_TESTING_MOCKSOILSENSOR_H */
