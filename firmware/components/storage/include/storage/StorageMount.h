// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file StorageMount.h
 * @brief Target-only littlefs mount/format/stats wrapper (FR-007, FR-008).
 *
 * The ONLY littlefs-specific code in the storage component (research.md
 * D2/D4): mounts the `storage` partition at /storage with
 * format-if-mount-failed (legacy `LittleFS.begin(true)` parity — a
 * corrupted filesystem is reformatted, never bricks the unit) and exposes
 * the esp_littlefs_info-based statistics provider that boot wiring
 * injects into LittleFsDataStorage.
 *
 * NOT built for the linux preview target — esp_littlefs has no linux
 * port; the host tests exercise LittleFsDataStorage over a POSIX temp
 * directory with a fake stats provider instead (see the storage
 * component CMakeLists). Contains no logic beyond the IDF calls.
 */

#ifndef WATERINGSYSTEM_STORAGE_STORAGEMOUNT_H
#define WATERINGSYSTEM_STORAGE_STORAGEMOUNT_H

#include "esp_err.h"

#include "storage/LittleFsDataStorage.h"

/**
 * @brief Mount-or-format of the `storage` partition (static-only helper).
 */
class StorageMount {
public:
    /// Partition name in firmware/partitions.csv (littlefs is the subtype).
    static constexpr const char* kPartitionLabel = "storage";

    /// VFS mount point; base path for LittleFsDataStorage on target.
    static constexpr const char* kBasePath = "/storage";

    StorageMount() = delete;

    /**
     * @brief Register the littlefs VFS: mount `storage` at /storage.
     *
     * format_if_mount_failed is set, so a fresh or corrupted partition is
     * formatted and mounted instead of failing (FR-007; data loss
     * accepted, bricking not).
     *
     * @return ESP_OK on success, the failing esp_err_t otherwise.
     */
    static esp_err_t mount(void);

    /**
     * @brief Stats provider over esp_littlefs_info for the mounted
     *        partition; inject into LittleFsDataStorage (FR-008).
     */
    static LittleFsDataStorage::StatsProvider statsProvider(void);
};

#endif /* WATERINGSYSTEM_STORAGE_STORAGEMOUNT_H */
