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
    // Single-instance by design (the C-ABI sync callback has no user context and
    // routes into s_instance). A second live instance would silently steal the
    // callback route — flag it loudly rather than overwriting quietly.
    if (s_instance != nullptr) {
        ESP_LOGE(TAG, "second SntpClient constructed — sync callback routes to "
                      "the latest instance only");
    }
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

bool SntpClient::start()
{
    if (started_) {
        return true;
    }

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_WS_SNTP_SERVER);
    cfg.start = true;
    cfg.sync_cb = &SntpClient::onSyncCb;

    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err == ESP_ERR_INVALID_STATE) {
        // Already initialised (idempotent re-invocation) — the service is
        // running, so report success.
        started_ = true;
        return true;
    }
    if (err != ESP_OK) {
        // Non-fatal: log and keep running. Init failed, so the service is NOT
        // running; report failure so the caller can retry on a later Connected
        // transition. The caller must never block on sync.
        ESP_LOGW(TAG, "esp_netif_sntp_init failed: %s", esp_err_to_name(err));
        return false;
    }

    // Step-set (immediate) mode: apply the server time in one jump rather than
    // slewing — appropriate for a device that boots with an un-set clock.
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    ESP_LOGI(TAG, "SNTP started against %s", CONFIG_WS_SNTP_SERVER);
    started_ = true;
    return true;
}

void SntpClient::onSyncCb(struct timeval* tv)
{
    if (s_instance == nullptr || tv == nullptr) {
        return;
    }
    // synced() is derived from lastSyncEpoch, so stamping the epoch is the whole
    // update. This runs on the SNTP task; the console `time` command reads
    // status_ from the REPL task without a lock — a deliberate benign divergence
    // (aligned word-size reads, diagnostic-only), so no Locked* wrapper is used.
    s_instance->status_.lastSyncEpoch = static_cast<uint32_t>(tv->tv_sec);
    ESP_LOGI(TAG, "wall clock synced: epoch=%u",
             static_cast<unsigned>(tv->tv_sec));
}
