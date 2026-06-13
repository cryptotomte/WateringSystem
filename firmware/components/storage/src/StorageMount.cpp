// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file StorageMount.cpp
 * @brief Target-only littlefs mount/format/stats wrapper implementation.
 *
 * See StorageMount.h. Excluded from the linux preview target build
 * (storage component CMakeLists) — esp_littlefs has no linux port.
 */

#include "storage/StorageMount.h"

#include <cstddef>
#include <cstdint>

#include "esp_littlefs.h"

esp_err_t StorageMount::mount(void)
{
    // Member-wise init (not designated init): esp_vfs_littlefs_conf_t
    // carries Kconfig-dependent fields; zero-init + explicit members is
    // robust against field-order differences across component versions.
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = kBasePath;
    conf.partition_label = kPartitionLabel;
    conf.format_if_mount_failed = true;
    conf.read_only = false;

    return esp_vfs_littlefs_register(&conf);
}

LittleFsDataStorage::StatsProvider StorageMount::statsProvider(void)
{
    return [](uint32_t& totalBytes, uint32_t& usedBytes) {
        std::size_t total = 0;
        std::size_t used = 0;
        if (esp_littlefs_info(kPartitionLabel, &total, &used) != ESP_OK) {
            return false;
        }
        totalBytes = static_cast<uint32_t>(total);
        usedBytes = static_cast<uint32_t>(used);
        return true;
    };
}
