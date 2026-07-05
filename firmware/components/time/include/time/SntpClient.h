// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file SntpClient.h
 * @brief SNTP wall-clock synchroniser + timezone setup — target only.
 *
 * Thin, non-blocking starter around esp_netif_sntp. applyTimezone() installs
 * the Swedish TZ (CET-1CEST,M3.5.0,M10.5.0/3) once; start() launches the SNTP
 * service against CONFIG_WS_SNTP_SERVER in step-set mode and is idempotent and
 * non-fatal (server unreachable is retried by the SNTP service, never a boot
 * failure). A static sync callback updates the client's SyncStatus on each
 * successful sync. Excluded from the linux host build (esp_* dependencies).
 */

#ifndef WATERINGSYSTEM_TIME_SNTPCLIENT_H
#define WATERINGSYSTEM_TIME_SNTPCLIENT_H

#include <sys/time.h>

#include "time/SyncStatus.h"

/**
 * @brief Starts SNTP synchronisation and tracks the resulting SyncStatus.
 *
 * Single-instance by design: the ESP-IDF SNTP module is a process-global
 * service, and the sync callback routes into the one live instance so the
 * console `time` line can report sync state.
 */
class SntpClient {
public:
    SntpClient();
    ~SntpClient();

    SntpClient(const SntpClient&) = delete;
    SntpClient& operator=(const SntpClient&) = delete;

    /**
     * @brief Install the Swedish timezone (CET/CEST) into the process env.
     *
     * `setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();`. Call once at
     * init, before any local-time rendering.
     */
    void applyTimezone();

    /**
     * @brief Start the SNTP service (idempotent, non-blocking, non-fatal).
     *
     * Initialises esp_netif_sntp against CONFIG_WS_SNTP_SERVER in step-set
     * (immediate) mode with the sync callback registered. Re-invocation is a
     * no-op (an already-initialised service is treated as success). Reaching the
     * server later is the SNTP service's own job; this call never blocks on it.
     *
     * @return true if the SNTP service is now running (init OK, or already
     *         inited); false if init failed — the caller should retry later.
     */
    bool start();

    /// Immutable view of the current synchronisation state.
    const SyncStatus& status() const { return status_; }

private:
    /// C-ABI SNTP time-sync callback; routes to the live instance.
    static void onSyncCb(struct timeval* tv);

    SyncStatus status_;
    bool started_ = false;
};

#endif /* WATERINGSYSTEM_TIME_SNTPCLIENT_H */
