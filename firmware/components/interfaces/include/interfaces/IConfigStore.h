// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file IConfigStore.h
 * @brief Typed, validated access to persisted configuration.
 *
 * Replaces the config half of the legacy IDataStorage (string-keyed
 * storeConfig/getConfig over one JSON blob) — a deliberate contract
 * redesign, same approach as PR-02's IWaterPump. Normative contract:
 * specs/003-nvs-littlefs-storage/contracts/IConfigStore.md; item schema,
 * defaults and ranges: specs/003-nvs-littlefs-storage/data-model.md.
 *
 * Invariants:
 *  1. A getter never returns an out-of-range value — the compiled-in
 *     factory default shadows missing or invalid storage (FR-002).
 *  2. A failed/rejected write never alters the stored value (FR-003):
 *     old-or-new, never torn.
 *  3. No operation blocks on or is affected by network state.
 *  4. WiFi credential values never appear in any diagnostic or log
 *     output (FR-004).
 *
 * Part of the header-only `interfaces` component: no IDF includes allowed
 * (compiled on the host in the linux-target test suite).
 */

#ifndef WATERINGSYSTEM_INTERFACES_ICONFIGSTORE_H
#define WATERINGSYSTEM_INTERFACES_ICONFIGSTORE_H

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * @brief Typed configuration store with compiled-in factory defaults.
 *
 * Implementations are unsynchronized by design; cross-task consumers wrap
 * them in the Locked* decorator (research.md D9, PR-02 CP3 precedent).
 */
class IConfigStore {
public:
    // Factory defaults and valid ranges (data-model.md table) — the single
    // source of truth shared by implementations, mocks and contract tests.
    // Defaults mirror the frozen firmware (src/WateringController.cpp:16-21)
    // except the documented divergences: the two interval items are settable
    // and have new range floors (1 s / 1 min) against log-storm
    // misconfiguration.
    static constexpr float kMoistureThresholdMin = 0.0f;    ///< %
    static constexpr float kMoistureThresholdMax = 100.0f;  ///< %
    static constexpr float kDefaultMoistureThresholdLow = 30.0f;
    static constexpr float kDefaultMoistureThresholdHigh = 55.0f;

    static constexpr uint32_t kWateringDurationMinS = 1;
    static constexpr uint32_t kWateringDurationMaxS = 300;
    static constexpr uint32_t kDefaultWateringDurationS = 20;

    static constexpr uint32_t kMinWateringIntervalFloorS = 1;
    static constexpr uint32_t kDefaultMinWateringIntervalS = 300;

    static constexpr bool kDefaultWateringEnabled = true;

    static constexpr uint32_t kSensorReadIntervalFloorMs = 1000;
    static constexpr uint32_t kDefaultSensorReadIntervalMs = 5000;

    static constexpr uint32_t kDataLogIntervalFloorMs = 60000;
    static constexpr uint32_t kDefaultDataLogIntervalMs = 300000;

    static constexpr std::size_t kWifiSsidMaxLen = 32;      ///< bytes
    static constexpr std::size_t kWifiPasswordMaxLen = 64;  ///< bytes

    virtual ~IConfigStore() = default;

    /**
     * @brief Lower soil-moisture threshold in percent (0–100).
     *
     * Never fails: returns the stored value if present and in range,
     * otherwise kDefaultMoistureThresholdLow.
     */
    virtual float getMoistureThresholdLow() const = 0;

    /**
     * @brief Set the lower soil-moisture threshold.
     *
     * In range (0–100, not NaN): persisted atomically, survives a power
     * cycle, returns true. Out of range: returns false, stored value
     * untouched. May also return false on a persistence failure.
     */
    virtual bool setMoistureThresholdLow(float percent) = 0;

    /// Upper soil-moisture threshold in percent (0–100); same semantics.
    virtual float getMoistureThresholdHigh() const = 0;

    /// Set the upper soil-moisture threshold; same semantics as the low one.
    virtual bool setMoistureThresholdHigh(float percent) = 0;

    /// Watering run duration in seconds (1–300).
    virtual uint32_t getWateringDurationS() const = 0;

    /// Set the watering run duration; rejects values outside 1–300 s.
    virtual bool setWateringDurationS(uint32_t seconds) = 0;

    /// Minimum interval between watering runs (soak pause) in seconds (>= 1).
    virtual uint32_t getMinWateringIntervalS() const = 0;

    /// Set the soak pause; rejects 0.
    virtual bool setMinWateringIntervalS(uint32_t seconds) = 0;

    /// Whether automatic watering is enabled.
    virtual bool getWateringEnabled() const = 0;

    /// Enable/disable automatic watering (no range to violate; false only
    /// on a persistence failure).
    virtual bool setWateringEnabled(bool enabled) = 0;

    /// Sensor read interval in milliseconds (>= 1000).
    virtual uint32_t getSensorReadIntervalMs() const = 0;

    /// Set the sensor read interval; rejects values below 1000 ms.
    virtual bool setSensorReadIntervalMs(uint32_t ms) = 0;

    /// Data log interval in milliseconds (>= 60000).
    virtual uint32_t getDataLogIntervalMs() const = 0;

    /// Set the data log interval; rejects values below 60000 ms.
    virtual bool setDataLogIntervalMs(uint32_t ms) = 0;

    /**
     * @brief Stored WiFi SSID; empty string = unconfigured (factory state).
     *
     * Divergence from legacy: the Arduino firmware marked the unconfigured
     * state with a CONFIGURE_ME sentinel in /wifi_config.json.
     */
    virtual std::string getWifiSsid() const = 0;

    /// Stored WiFi password; empty string in the factory state.
    virtual std::string getWifiPassword() const = 0;

    /**
     * @brief Store WiFi credentials as one pair.
     *
     * Length-validated (SSID <= 32 bytes, password <= 64 bytes); over-long
     * input is rejected with false, stored values untouched.
     * Implementations MUST NOT log the values (FR-004).
     */
    virtual bool setWifiCredentials(const std::string& ssid,
                                    const std::string& password) = 0;

    /// Return both credential items to the factory (empty) state.
    virtual bool clearWifiCredentials() = 0;

    /**
     * @brief Factory reset: erase the underlying config storage.
     *
     * Afterwards every item reads its factory default and the credentials
     * are removed (FR-005). The store remains usable without
     * re-construction.
     */
    virtual bool factoryReset() = 0;
};

#endif /* WATERINGSYSTEM_INTERFACES_ICONFIGSTORE_H */
