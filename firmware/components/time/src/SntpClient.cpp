// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file SntpClient.cpp
 * @brief SNTP synchroniser implementation — target-only (excluded on linux).
 *
 * Non-blocking, non-fatal SNTP against CONFIG_WS_SNTP_SERVER (default
 * se.pool.ntp.org). The device never blocks waiting for sync; the wall clock
 * stays un-set until the first successful step. The sync callback updates the
 * live instance's SyncStatus so the console `time` line can report it.
 */

#include "time/SntpClient.h"

#include <cstdlib>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

#include "sdkconfig.h"

namespace {
const char* TAG = "sntp";

// The ESP-IDF SNTP module is a process-global service; the C-ABI callback has
// no user context, so it routes into the single live SntpClient instance.
SntpClient* s_instance = nullptr;
}  // namespace

SntpClient::SntpClient()
{
    s_instance = this;
}

SntpClient::~SntpClient()
{
    if (started_) {
        esp_netif_sntp_deinit();
        started_ = false;
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void SntpClient::applyTimezone()
{
    // Swedish local time: CET (+01) in winter, CEST (+02) in summer, DST on the
    // last Sunday of March/October (matches TimeService::formatLocal()).
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

void SntpClient::start()
{
    if (started_) {
        return;
    }

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_WS_SNTP_SERVER);
    cfg.start = true;
    cfg.sync_cb = &SntpClient::onSyncCb;

    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err == ESP_ERR_INVALID_STATE) {
        // Already initialised (idempotent re-invocation) — treat as success.
        started_ = true;
        return;
    }
    if (err != ESP_OK) {
        // Non-fatal: log and keep running. isTimeSet() stays false until a
        // later attempt succeeds; the caller must never block on sync.
        ESP_LOGW(TAG, "esp_netif_sntp_init failed: %s", esp_err_to_name(err));
        return;
    }

    // Step-set (immediate) mode: apply the server time in one jump rather than
    // slewing — appropriate for a device that boots with an un-set clock.
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    ESP_LOGI(TAG, "SNTP started against %s", CONFIG_WS_SNTP_SERVER);
    started_ = true;
}

void SntpClient::onSyncCb(struct timeval* tv)
{
    if (s_instance == nullptr || tv == nullptr) {
        return;
    }
    s_instance->status_.synced = true;
    s_instance->status_.lastSyncEpoch = static_cast<uint32_t>(tv->tv_sec);
    ESP_LOGI(TAG, "wall clock synced: epoch=%u",
             static_cast<unsigned>(tv->tv_sec));
}
