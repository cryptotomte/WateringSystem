// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file MockConfigStore.h
 * @brief In-memory IConfigStore test double (header-only).
 *
 * Holds the same contract invariants as the real store (FR-012):
 * range-validated setters, default-shadowed getters, factory reset,
 * credential length limits. `stored` is public so tests can inject
 * out-of-range raw values and exercise the shadowing path; `failWrites`
 * simulates a persistence failure. For consumer tests in later PRs
 * (PR-07, PR-09, PR-11). Never compiled into target builds (only
 * included from test code). No IDF includes.
 */

#ifndef WATERINGSYSTEM_STORAGE_TESTING_MOCKCONFIGSTORE_H
#define WATERINGSYSTEM_STORAGE_TESTING_MOCKCONFIGSTORE_H

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

#include "interfaces/IConfigStore.h"

/**
 * @brief IConfigStore over plain members, instrumented for tests.
 */
class MockConfigStore : public IConfigStore {
public:
    /**
     * @brief Raw stored state, one optional per NVS entry (data-model.md).
     *
     * The bool item is a u8 — exactly like its NVS entry — so tests can
     * inject an invalid stored value (neither 0 nor 1).
     */
    struct Stored {
        std::optional<float> moistureThresholdLow;
        std::optional<float> moistureThresholdHigh;
        std::optional<uint32_t> wateringDurationS;
        std::optional<uint32_t> minWateringIntervalS;
        std::optional<uint8_t> wateringEnabled;
        std::optional<uint32_t> sensorReadIntervalMs;
        std::optional<uint32_t> dataLogIntervalMs;
        std::optional<std::string> wifiSsid;
        std::optional<std::string> wifiPassword;
    };

    Stored stored;

    // Instrumentation.
    int acceptedWrites = 0;   ///< setters that persisted a value
    int rejectedWrites = 0;   ///< setters rejected (validation or failWrites)
    int factoryResets = 0;    ///< successful factoryReset() calls
    bool failWrites = false;  ///< true: every write fails, state untouched

    // IConfigStore — getters (defaults shadow missing/invalid storage)
    float getMoistureThresholdLow() const override
    {
        return shadowFloat(stored.moistureThresholdLow,
                           kDefaultMoistureThresholdLow);
    }

    float getMoistureThresholdHigh() const override
    {
        return shadowFloat(stored.moistureThresholdHigh,
                           kDefaultMoistureThresholdHigh);
    }

    uint32_t getWateringDurationS() const override
    {
        return shadowU32(stored.wateringDurationS, kDefaultWateringDurationS,
                         kWateringDurationMinS, kWateringDurationMaxS);
    }

    uint32_t getMinWateringIntervalS() const override
    {
        return shadowU32(stored.minWateringIntervalS,
                         kDefaultMinWateringIntervalS,
                         kMinWateringIntervalFloorS, kNoUpperBound);
    }

    bool getWateringEnabled() const override
    {
        if (!stored.wateringEnabled.has_value() ||
            (*stored.wateringEnabled != 0 && *stored.wateringEnabled != 1)) {
            return kDefaultWateringEnabled;
        }
        return *stored.wateringEnabled == 1;
    }

    uint32_t getSensorReadIntervalMs() const override
    {
        return shadowU32(stored.sensorReadIntervalMs,
                         kDefaultSensorReadIntervalMs,
                         kSensorReadIntervalFloorMs, kNoUpperBound);
    }

    uint32_t getDataLogIntervalMs() const override
    {
        return shadowU32(stored.dataLogIntervalMs, kDefaultDataLogIntervalMs,
                         kDataLogIntervalFloorMs, kNoUpperBound);
    }

    std::string getWifiSsid() const override
    {
        return shadowString(stored.wifiSsid, kWifiSsidMaxLen);
    }

    std::string getWifiPassword() const override
    {
        return shadowString(stored.wifiPassword, kWifiPasswordMaxLen);
    }

    // IConfigStore — setters (reject out-of-range, leave state untouched)
    bool setMoistureThresholdLow(float percent) override
    {
        return writeFloat(stored.moistureThresholdLow, percent);
    }

    bool setMoistureThresholdHigh(float percent) override
    {
        return writeFloat(stored.moistureThresholdHigh, percent);
    }

    bool setWateringDurationS(uint32_t seconds) override
    {
        return writeU32(stored.wateringDurationS, seconds,
                        kWateringDurationMinS, kWateringDurationMaxS);
    }

    bool setMinWateringIntervalS(uint32_t seconds) override
    {
        return writeU32(stored.minWateringIntervalS, seconds,
                        kMinWateringIntervalFloorS, kNoUpperBound);
    }

    bool setWateringEnabled(bool enabled) override
    {
        if (failWrites) {
            ++rejectedWrites;
            return false;
        }
        stored.wateringEnabled = enabled ? 1 : 0;
        ++acceptedWrites;
        return true;
    }

    bool setSensorReadIntervalMs(uint32_t ms) override
    {
        return writeU32(stored.sensorReadIntervalMs, ms,
                        kSensorReadIntervalFloorMs, kNoUpperBound);
    }

    bool setDataLogIntervalMs(uint32_t ms) override
    {
        return writeU32(stored.dataLogIntervalMs, ms, kDataLogIntervalFloorMs,
                        kNoUpperBound);
    }

    bool setWifiCredentials(const std::string& ssid,
                            const std::string& password) override
    {
        if (failWrites || ssid.size() > kWifiSsidMaxLen ||
            password.size() > kWifiPasswordMaxLen) {
            ++rejectedWrites;
            return false;
        }
        stored.wifiSsid = ssid;
        stored.wifiPassword = password;
        ++acceptedWrites;
        return true;
    }

    bool clearWifiCredentials() override
    {
        if (failWrites) {
            ++rejectedWrites;
            return false;
        }
        stored.wifiSsid.reset();
        stored.wifiPassword.reset();
        ++acceptedWrites;
        return true;
    }

    bool factoryReset() override
    {
        if (failWrites) {
            return false;
        }
        stored = Stored{};
        ++factoryResets;
        return true;
    }

private:
    static constexpr uint32_t kNoUpperBound = UINT32_MAX;

    static float shadowFloat(const std::optional<float>& value,
                             float defaultValue)
    {
        if (!value.has_value() || std::isnan(*value) ||
            *value < kMoistureThresholdMin || *value > kMoistureThresholdMax) {
            return defaultValue;
        }
        return *value;
    }

    static uint32_t shadowU32(const std::optional<uint32_t>& value,
                              uint32_t defaultValue, uint32_t minValue,
                              uint32_t maxValue)
    {
        if (!value.has_value() || *value < minValue || *value > maxValue) {
            return defaultValue;
        }
        return *value;
    }

    static std::string shadowString(const std::optional<std::string>& value,
                                    std::size_t maxLen)
    {
        // Factory default for both credential items is the empty string.
        if (!value.has_value() || value->size() > maxLen) {
            return "";
        }
        return *value;
    }

    bool writeFloat(std::optional<float>& slot, float value)
    {
        if (failWrites || std::isnan(value) ||
            value < kMoistureThresholdMin || value > kMoistureThresholdMax) {
            ++rejectedWrites;
            return false;
        }
        slot = value;
        ++acceptedWrites;
        return true;
    }

    bool writeU32(std::optional<uint32_t>& slot, uint32_t value,
                  uint32_t minValue, uint32_t maxValue)
    {
        if (failWrites || value < minValue || value > maxValue) {
            ++rejectedWrites;
            return false;
        }
        slot = value;
        ++acceptedWrites;
        return true;
    }
};

#endif /* WATERINGSYSTEM_STORAGE_TESTING_MOCKCONFIGSTORE_H */
