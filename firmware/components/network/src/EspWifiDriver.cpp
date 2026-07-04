// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file EspWifiDriver.cpp
 * @brief esp_wifi/esp_netif/esp_event implementation of IWifiDriver
 *        (target-only; see EspWifiDriver.h).
 *
 * Error handling is explicit (not ESP_ERROR_CHECK): a WiFi bring-up failure
 * must be visible in the logs but must never abort — WiFi is not boot-critical
 * and the watering path never depends on it (FR-014). The esp_event callbacks
 * run on the default event-loop task and only push a WifiEvent onto a FreeRTOS
 * queue; pollEvent() drains it from the wifi task. The FreeRTOS queue is the
 * thread-safe hand-off between the two tasks (xQueueSendFromISR is not needed —
 * esp_event handlers run in task context, so a plain xQueueSend/xQueueReceive
 * pair is the correct, lock-free primitive here).
 */

#include "network/EspWifiDriver.h"

#include <algorithm>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "espwifidriver";

namespace {

/// Bounded event queue: a handful of lifecycle transitions between two ticks
/// is plenty; the manager drains the whole queue every tick (250 ms).
constexpr UBaseType_t kEventQueueLen = 8;

/// Copy a std::string into a fixed-size, NUL-terminated esp_wifi field
/// (ssid/password are uint8_t[] arrays in wifi_config_t). Never logs the
/// content — callers decide what is safe to log.
void copyField(uint8_t *dst, std::size_t dstSize, const std::string &src)
{
    const std::size_t n = std::min(src.size(), dstSize - 1);
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

/// Translate a raw WIFI_EVENT id to a WifiEvent, or None for events the
/// manager does not track.
WifiEvent translateWifiEvent(int32_t id)
{
    switch (id) {
    case WIFI_EVENT_STA_CONNECTED:
        return WifiEvent::Connected;
    case WIFI_EVENT_STA_DISCONNECTED:
        // A disconnect also serves as the "connect attempt failed" signal:
        // the manager treats Disconnected/ConnectFailed identically.
        return WifiEvent::Disconnected;
    default:
        return WifiEvent::None;
    }
}

/// WIFI_EVENT handler: arg is the WifiEvent queue handle (registered in
/// init()). Runs on the default event-loop task; pushes non-blocking.
void wifiEventHandler(void *arg, esp_event_base_t /*base*/, int32_t id,
                      void * /*data*/)
{
    QueueHandle_t queue = static_cast<QueueHandle_t>(arg);
    const WifiEvent event = translateWifiEvent(id);
    if (event != WifiEvent::None && queue != nullptr) {
        // Non-blocking send: if the queue is somehow full the oldest news is
        // simply the manager's problem to catch up on next tick — never block
        // the event loop.
        (void)xQueueSend(queue, &event, 0);
    }
}

/// IP_EVENT handler: only IP_EVENT_STA_GOT_IP matters — it is the "usable
/// link" signal the manager treats as fully connected.
void ipEventHandler(void *arg, esp_event_base_t /*base*/, int32_t id,
                    void * /*data*/)
{
    if (id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    QueueHandle_t queue = static_cast<QueueHandle_t>(arg);
    const WifiEvent event = WifiEvent::GotIp;
    if (queue != nullptr) {
        (void)xQueueSend(queue, &event, 0);
    }
}

}  // namespace

EspWifiDriver::~EspWifiDriver()
{
    // Function-local static in app_main: this never actually runs (the program
    // lives forever). Kept correct for completeness / unit reasoning.
    if (started_) {
        esp_wifi_stop();
    }
    if (initialized_) {
        esp_wifi_deinit();
    }
    if (eventQueue_ != nullptr) {
        vQueueDelete(static_cast<QueueHandle_t>(eventQueue_));
    }
}

esp_err_t EspWifiDriver::init()
{
    if (initialized_) {
        return ESP_OK;  // idempotent
    }

    // The default STA + AP netifs. esp_netif_init() and the default event loop
    // are created earlier in app_main (T007); creating both interfaces up front
    // lets staConnect/apStart switch modes without re-plumbing netifs.
    staNetif_ = esp_netif_create_default_wifi_sta();
    apNetif_ = esp_netif_create_default_wifi_ap();
    if (staNetif_ == nullptr || apNetif_ == nullptr) {
        ESP_LOGE(TAG, "failed to create default WiFi netifs");
        return ESP_FAIL;
    }

    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Thread-safe hand-off between the esp_event task (producer) and the wifi
    // task (consumer, via pollEvent()).
    QueueHandle_t queue = xQueueCreate(kEventQueueLen, sizeof(WifiEvent));
    if (queue == nullptr) {
        ESP_LOGE(TAG, "failed to create WiFi event queue");
        esp_wifi_deinit();
        return ESP_ERR_NO_MEM;
    }
    eventQueue_ = queue;

    // The handlers receive the queue handle as their arg, so they need no
    // access to the driver instance (keeps them file-local and header-clean).
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &wifiEventHandler, queue, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WIFI_EVENT handler register failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &ipEventHandler, queue, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "IP_EVENT handler register failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    // Store WiFi calibration/config in RAM only: NVS holds our own config
    // (wscfg namespace) and we never want esp_wifi to persist scratch there.
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_storage(RAM) failed: %s",
                 esp_err_to_name(err));
    }

    initialized_ = true;
    ESP_LOGI(TAG, "WiFi driver initialized (STA + AP netifs, event queue)");
    return ESP_OK;
}

bool EspWifiDriver::staConnect(const std::string &ssid,
                               const std::string &password)
{
    if (!initialized_) {
        ESP_LOGE(TAG, "staConnect before init — ignored");
        return false;
    }

    // STA and AP are mutually exclusive here: switching to STA mode tears down
    // any running SoftAP.
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(STA) failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    wifi_config_t wc = {};
    copyField(wc.sta.ssid, sizeof(wc.sta.ssid), ssid);
    copyField(wc.sta.password, sizeof(wc.sta.password), password);
    err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) {
        // Password value never logged (FR-004).
        ESP_LOGE(TAG, "esp_wifi_set_config(STA) failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    if (!started_) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
            return false;
        }
        started_ = true;
    }

    // Non-blocking: the outcome arrives later as a WifiEvent. A synchronous
    // failure here is reported false; the manager stays deterministic.
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "STA connect requested (ssid=%s)", ssid.c_str());
    return true;
}

void EspWifiDriver::staStop()
{
    if (!initialized_) {
        return;
    }
    // Idempotent: esp_wifi_disconnect returns an error when not connected,
    // which is a benign no-op here.
    (void)esp_wifi_disconnect();
}

bool EspWifiDriver::apStart(const std::string &ssid,
                            const std::string &password)
{
    if (!initialized_) {
        ESP_LOGE(TAG, "apStart before init — ignored");
        return false;
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(AP) failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    wifi_config_t wc = {};
    copyField(wc.ap.ssid, sizeof(wc.ap.ssid), ssid);
    wc.ap.ssid_len = static_cast<uint8_t>(
        std::min(ssid.size(), sizeof(wc.ap.ssid)));
    copyField(wc.ap.password, sizeof(wc.ap.password), password);
    wc.ap.channel = 1;
    wc.ap.max_connection = 4;
    // WPA2 by default; an empty password yields an open AP (documented
    // non-secret provisioning window — Kconfig WS_PROV_AP_PASSWORD help).
    wc.ap.authmode =
        password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    err = esp_wifi_set_config(WIFI_IF_AP, &wc);
    if (err != ESP_OK) {
        // Password value never logged (FR-004).
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    if (!started_) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
            return false;
        }
        started_ = true;
    }
    // Default AP address is 192.168.4.1 (esp_netif_create_default_wifi_ap).
    ESP_LOGI(TAG, "SoftAP started (ssid=%s, %s) at 192.168.4.1", ssid.c_str(),
             password.empty() ? "open" : "WPA2");
    return true;
}

void EspWifiDriver::apStop()
{
    if (!initialized_ || !started_) {
        return;  // idempotent
    }
    (void)esp_wifi_stop();
    started_ = false;
}

WifiEvent EspWifiDriver::pollEvent()
{
    if (eventQueue_ == nullptr) {
        return WifiEvent::None;
    }
    WifiEvent event = WifiEvent::None;
    // Non-blocking (timeout 0): return the next queued event or None.
    if (xQueueReceive(static_cast<QueueHandle_t>(eventQueue_), &event, 0) ==
        pdTRUE) {
        return event;
    }
    return WifiEvent::None;
}

int8_t EspWifiDriver::rssi() const
{
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    // Unspecified when not connected (contract): report 0.
    return 0;
}
