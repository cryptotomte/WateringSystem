// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file NvsConfigStore.h
 * @brief IConfigStore backed by NVS (namespace `wscfg`).
 *
 * Schema per specs/003-nvs-littlefs-storage/data-model.md: one NVS entry
 * per item, floats stored as u32 bit patterns (NVS has no float type).
 * Requires nvs_flash to be initialized before use (boot wiring in user
 * story 4; the host test fixture initializes it explicitly).
 *
 * NVS handles are opened per operation, never held: factoryReset() erases
 * the whole partition, which would invalidate any long-lived handle.
 *
 * Unsynchronized by design — cross-task consumers wrap the store in the
 * Locked* decorator (research.md D9, PR-02 CP3 precedent).
 */

#ifndef WATERINGSYSTEM_STORAGE_NVSCONFIGSTORE_H
#define WATERINGSYSTEM_STORAGE_NVSCONFIGSTORE_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "interfaces/IConfigStore.h"

/**
 * @brief NVS-backed configuration store (target + linux-target emulation).
 */
class NvsConfigStore : public IConfigStore {
public:
    NvsConfigStore() = default;

    // IConfigStore
    float getMoistureThresholdLow() const override;
    bool setMoistureThresholdLow(float percent) override;
    float getMoistureThresholdHigh() const override;
    bool setMoistureThresholdHigh(float percent) override;
    uint32_t getWateringDurationS() const override;
    bool setWateringDurationS(uint32_t seconds) override;
    uint32_t getMinWateringIntervalS() const override;
    bool setMinWateringIntervalS(uint32_t seconds) override;
    bool getWateringEnabled() const override;
    bool setWateringEnabled(bool enabled) override;
    uint32_t getSensorReadIntervalMs() const override;
    bool setSensorReadIntervalMs(uint32_t ms) override;
    uint32_t getDataLogIntervalMs() const override;
    bool setDataLogIntervalMs(uint32_t ms) override;
    std::string getWifiSsid() const override;
    std::string getWifiPassword() const override;
    bool setWifiCredentials(const std::string& ssid,
                            const std::string& password) override;
    bool clearWifiCredentials() override;
    bool factoryReset() override;

private:
    // Typed helpers over per-operation NVS handles. Getters shadow a
    // missing or out-of-range stored value with the default (FR-002);
    // setters reject out-of-range input without touching storage (FR-003).
    float getFloat(const char* key, float defaultValue, float minValue,
                   float maxValue) const;
    bool setFloat(const char* key, float value, float minValue,
                  float maxValue);
    uint32_t getU32(const char* key, uint32_t defaultValue, uint32_t minValue,
                    uint32_t maxValue) const;
    bool setU32(const char* key, uint32_t value, uint32_t minValue,
                uint32_t maxValue);
    bool getBool(const char* key, bool defaultValue) const;
    bool setBool(const char* key, bool value);
    std::string getString(const char* key, std::size_t maxLen) const;
};

#endif /* WATERINGSYSTEM_STORAGE_NVSCONFIGSTORE_H */
