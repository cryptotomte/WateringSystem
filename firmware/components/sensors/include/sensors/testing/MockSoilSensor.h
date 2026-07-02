// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file MockSoilSensor.h
 * @brief Scriptable ISoilSensor test double (header-only).
 *
 * Serves the host tests of soil-sensor CONSUMERS (watering controller /
 * sensor manager, PR-11): set the seven quantity values and the
 * initialize()/read()/isAvailable() results, script the error code, and
 * assert on the call counters. The sensor's own decode/validation logic is
 * tested against the REAL ModbusSoilSensor via MockModbusClient — never
 * through this mock. Never compiled into target builds (only included from
 * test code). No IDF includes.
 */

#ifndef WATERINGSYSTEM_SENSORS_TESTING_MOCKSOILSENSOR_H
#define WATERINGSYSTEM_SENSORS_TESTING_MOCKSOILSENSOR_H

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

    // -- ISoilSensor ---------------------------------------------------------

    bool initialize() override
    {
        ++initializeCalls;
        return initializeResult;
    }

    bool read() override
    {
        ++readCalls;
        return readResult;
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
};

#endif /* WATERINGSYSTEM_SENSORS_TESTING_MOCKSOILSENSOR_H */
