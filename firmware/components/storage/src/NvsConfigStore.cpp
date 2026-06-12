// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file NvsConfigStore.cpp
 * @brief NVS-backed IConfigStore implementation.
 *
 * Builds for every target including the linux preview target, where the
 * host test suite runs it against the real nvs_flash implementation
 * (research.md D3).
 *
 * FR-004: WiFi credential VALUES must never reach any log output — only
 * key names and esp_err codes are logged in this file.
 */

#include "storage/NvsConfigStore.h"

#include <cmath>
#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {

const char* TAG = "nvsconfigstore";

// Namespace and keys per data-model.md (NVS key limit: 15 chars).
constexpr const char* kNamespace = "wscfg";
constexpr const char* kKeyMoistLow = "moist_low";
constexpr const char* kKeyMoistHigh = "moist_high";
constexpr const char* kKeyWaterDur = "water_dur";
constexpr const char* kKeySoakPause = "soak_pause";
constexpr const char* kKeyWaterEn = "water_en";
constexpr const char* kKeyReadIv = "read_iv";
constexpr const char* kKeyLogIv = "log_iv";
constexpr const char* kKeyWifiSsid = "wifi_ssid";
constexpr const char* kKeyWifiPass = "wifi_pass";

constexpr uint32_t kNoUpperBound = UINT32_MAX;

/// RAII guard for a C-API NVS handle (opened per operation, see header).
class NvsHandleGuard {
public:
    NvsHandleGuard(const char* ns, nvs_open_mode_t mode)
    {
        err_ = nvs_open(ns, mode, &handle_);
    }

    ~NvsHandleGuard()
    {
        if (err_ == ESP_OK) {
            nvs_close(handle_);
        }
    }

    NvsHandleGuard(const NvsHandleGuard&) = delete;
    NvsHandleGuard& operator=(const NvsHandleGuard&) = delete;

    bool ok() const { return err_ == ESP_OK; }
    esp_err_t error() const { return err_; }
    nvs_handle_t get() const { return handle_; }

private:
    nvs_handle_t handle_ = 0;
    esp_err_t err_ = ESP_FAIL;
};

// NVS has no float type: floats are stored as u32 bit patterns (data-model).
uint32_t floatToBits(float value)
{
    static_assert(sizeof(uint32_t) == sizeof(float));
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float bitsToFloat(uint32_t bits)
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool commitValue(nvs_handle_t handle, const char* key, esp_err_t setErr)
{
    esp_err_t err = setErr;
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "persisting %s failed: %s", key, esp_err_to_name(err));
        return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Typed helpers
// ---------------------------------------------------------------------------

float NvsConfigStore::getFloat(const char* key, float defaultValue,
                               float minValue, float maxValue) const
{
    NvsHandleGuard handle(kNamespace, NVS_READONLY);
    if (!handle.ok()) {
        // Namespace absent (factory state) or NVS unavailable: default.
        return defaultValue;
    }
    uint32_t bits = 0;
    if (nvs_get_u32(handle.get(), key, &bits) != ESP_OK) {
        return defaultValue;
    }
    const float value = bitsToFloat(bits);
    if (std::isnan(value) || value < minValue || value > maxValue) {
        // FR-002: shadow with the default; the invalid entry stays in
        // place until the next valid write.
        ESP_LOGW(TAG, "stored %s out of range, using default", key);
        return defaultValue;
    }
    return value;
}

bool NvsConfigStore::setFloat(const char* key, float value, float minValue,
                              float maxValue)
{
    if (std::isnan(value) || value < minValue || value > maxValue) {
        return false;  // FR-003: rejected, stored value untouched
    }
    NvsHandleGuard handle(kNamespace, NVS_READWRITE);
    if (!handle.ok()) {
        ESP_LOGE(TAG, "nvs_open for %s failed: %s", key,
                 esp_err_to_name(handle.error()));
        return false;
    }
    return commitValue(handle.get(), key,
                       nvs_set_u32(handle.get(), key, floatToBits(value)));
}

uint32_t NvsConfigStore::getU32(const char* key, uint32_t defaultValue,
                                uint32_t minValue, uint32_t maxValue) const
{
    NvsHandleGuard handle(kNamespace, NVS_READONLY);
    if (!handle.ok()) {
        return defaultValue;
    }
    uint32_t value = 0;
    if (nvs_get_u32(handle.get(), key, &value) != ESP_OK) {
        return defaultValue;
    }
    if (value < minValue || value > maxValue) {
        ESP_LOGW(TAG, "stored %s out of range, using default", key);
        return defaultValue;
    }
    return value;
}

bool NvsConfigStore::setU32(const char* key, uint32_t value,
                            uint32_t minValue, uint32_t maxValue)
{
    if (value < minValue || value > maxValue) {
        return false;
    }
    NvsHandleGuard handle(kNamespace, NVS_READWRITE);
    if (!handle.ok()) {
        ESP_LOGE(TAG, "nvs_open for %s failed: %s", key,
                 esp_err_to_name(handle.error()));
        return false;
    }
    return commitValue(handle.get(), key,
                       nvs_set_u32(handle.get(), key, value));
}

bool NvsConfigStore::getBool(const char* key, bool defaultValue) const
{
    NvsHandleGuard handle(kNamespace, NVS_READONLY);
    if (!handle.ok()) {
        return defaultValue;
    }
    uint8_t value = 0;
    if (nvs_get_u8(handle.get(), key, &value) != ESP_OK) {
        return defaultValue;
    }
    if (value != 0 && value != 1) {
        // Valid range for the bool item is 0/1 (data-model).
        ESP_LOGW(TAG, "stored %s out of range, using default", key);
        return defaultValue;
    }
    return value == 1;
}

bool NvsConfigStore::setBool(const char* key, bool value)
{
    NvsHandleGuard handle(kNamespace, NVS_READWRITE);
    if (!handle.ok()) {
        ESP_LOGE(TAG, "nvs_open for %s failed: %s", key,
                 esp_err_to_name(handle.error()));
        return false;
    }
    return commitValue(handle.get(), key,
                       nvs_set_u8(handle.get(), key, value ? 1 : 0));
}

std::string NvsConfigStore::getString(const char* key,
                                      std::size_t maxLen) const
{
    NvsHandleGuard handle(kNamespace, NVS_READONLY);
    if (!handle.ok()) {
        return "";
    }
    size_t len = 0;  // includes the NUL terminator
    if (nvs_get_str(handle.get(), key, nullptr, &len) != ESP_OK || len == 0) {
        return "";
    }
    std::string value(len, '\0');
    if (nvs_get_str(handle.get(), key, value.data(), &len) != ESP_OK) {
        return "";
    }
    value.resize(len - 1);  // drop the NUL terminator
    if (value.size() > maxLen) {
        // FR-002 analogue: an over-long stored credential is shadowed by
        // the factory (empty) state. Value intentionally not logged.
        ESP_LOGW(TAG, "stored %s exceeds length limit, using default", key);
        return "";
    }
    return value;
}

// ---------------------------------------------------------------------------
// IConfigStore
// ---------------------------------------------------------------------------

float NvsConfigStore::getMoistureThresholdLow() const
{
    return getFloat(kKeyMoistLow, kDefaultMoistureThresholdLow,
                    kMoistureThresholdMin, kMoistureThresholdMax);
}

bool NvsConfigStore::setMoistureThresholdLow(float percent)
{
    return setFloat(kKeyMoistLow, percent, kMoistureThresholdMin,
                    kMoistureThresholdMax);
}

float NvsConfigStore::getMoistureThresholdHigh() const
{
    return getFloat(kKeyMoistHigh, kDefaultMoistureThresholdHigh,
                    kMoistureThresholdMin, kMoistureThresholdMax);
}

bool NvsConfigStore::setMoistureThresholdHigh(float percent)
{
    return setFloat(kKeyMoistHigh, percent, kMoistureThresholdMin,
                    kMoistureThresholdMax);
}

uint32_t NvsConfigStore::getWateringDurationS() const
{
    return getU32(kKeyWaterDur, kDefaultWateringDurationS,
                  kWateringDurationMinS, kWateringDurationMaxS);
}

bool NvsConfigStore::setWateringDurationS(uint32_t seconds)
{
    return setU32(kKeyWaterDur, seconds, kWateringDurationMinS,
                  kWateringDurationMaxS);
}

uint32_t NvsConfigStore::getMinWateringIntervalS() const
{
    return getU32(kKeySoakPause, kDefaultMinWateringIntervalS,
                  kMinWateringIntervalFloorS, kNoUpperBound);
}

bool NvsConfigStore::setMinWateringIntervalS(uint32_t seconds)
{
    return setU32(kKeySoakPause, seconds, kMinWateringIntervalFloorS,
                  kNoUpperBound);
}

bool NvsConfigStore::getWateringEnabled() const
{
    return getBool(kKeyWaterEn, kDefaultWateringEnabled);
}

bool NvsConfigStore::setWateringEnabled(bool enabled)
{
    return setBool(kKeyWaterEn, enabled);
}

uint32_t NvsConfigStore::getSensorReadIntervalMs() const
{
    return getU32(kKeyReadIv, kDefaultSensorReadIntervalMs,
                  kSensorReadIntervalFloorMs, kNoUpperBound);
}

bool NvsConfigStore::setSensorReadIntervalMs(uint32_t ms)
{
    return setU32(kKeyReadIv, ms, kSensorReadIntervalFloorMs, kNoUpperBound);
}

uint32_t NvsConfigStore::getDataLogIntervalMs() const
{
    return getU32(kKeyLogIv, kDefaultDataLogIntervalMs,
                  kDataLogIntervalFloorMs, kNoUpperBound);
}

bool NvsConfigStore::setDataLogIntervalMs(uint32_t ms)
{
    return setU32(kKeyLogIv, ms, kDataLogIntervalFloorMs, kNoUpperBound);
}

std::string NvsConfigStore::getWifiSsid() const
{
    return getString(kKeyWifiSsid, kWifiSsidMaxLen);
}

std::string NvsConfigStore::getWifiPassword() const
{
    return getString(kKeyWifiPass, kWifiPasswordMaxLen);
}

bool NvsConfigStore::setWifiCredentials(const std::string& ssid,
                                        const std::string& password)
{
    if (ssid.size() > kWifiSsidMaxLen ||
        password.size() > kWifiPasswordMaxLen) {
        // Rejected without logging anything about the input (FR-004).
        return false;
    }
    NvsHandleGuard handle(kNamespace, NVS_READWRITE);
    if (!handle.ok()) {
        ESP_LOGE(TAG, "nvs_open for wifi credentials failed: %s",
                 esp_err_to_name(handle.error()));
        return false;
    }
    esp_err_t err = nvs_set_str(handle.get(), kKeyWifiSsid, ssid.c_str());
    if (err == ESP_OK) {
        err = nvs_set_str(handle.get(), kKeyWifiPass, password.c_str());
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle.get());
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "persisting wifi credentials failed: %s",
                 esp_err_to_name(err));
        return false;
    }
    return true;
}

bool NvsConfigStore::clearWifiCredentials()
{
    NvsHandleGuard handle(kNamespace, NVS_READWRITE);
    if (!handle.ok()) {
        // Namespace never created = already in the factory (empty) state.
        if (handle.error() == ESP_ERR_NVS_NOT_FOUND) {
            return true;
        }
        ESP_LOGE(TAG, "nvs_open for wifi credentials failed: %s",
                 esp_err_to_name(handle.error()));
        return false;
    }
    esp_err_t err = nvs_erase_key(handle.get(), kKeyWifiSsid);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;  // already absent
    }
    if (err == ESP_OK) {
        err = nvs_erase_key(handle.get(), kKeyWifiPass);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle.get());
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clearing wifi credentials failed: %s",
                 esp_err_to_name(err));
        return false;
    }
    return true;
}

bool NvsConfigStore::factoryReset()
{
    // Standard factory-reset sequence (research.md D5/D8). The erase call
    // deinitializes an initialized partition first, so no handle is open
    // when the erase happens (handles here are per-operation anyway).
    esp_err_t err = nvs_flash_erase_partition(NVS_DEFAULT_PART_NAME);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs partition erase failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs re-init after factory reset failed: %s",
                 esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "factory reset: all configuration restored to defaults");
    return true;
}
