# PR-06: nvs-littlefs-storage

> Phase 2 — infrastructure

## Goal

Port `IDataStorage` to ESP-IDF: configuration in NVS with sane factory defaults, and a
mounted littlefs partition for web assets and sensor/event data.

## Scope

- `IDataStorage` interface ported to pure C++ (config get/set, sensor data logging,
  log retrieval), host-includable.
- `NvsConfigStore`: typed configuration access on the `nvs` partition (16 K per
  partition plan). Covers the configuration the Arduino firmware keeps today
  (watering thresholds, min watering interval — Arduino default 300 s, watering
  duration, mode, WiFi credentials handed over to PR-07, log interval — Arduino
  default 5 min). Factory defaults applied on missing/erased NVS (FR13); explicit
  factory-reset operation.
- littlefs mount via `joltwallet/littlefs==1.22.1` on the `storage` partition
  (960 K, label `littlefs` in `partitions.csv`); mount-or-format-on-first-boot;
  filesystem usage reporting (parity with today's storage monitoring).
- `LittleFsDataStorage`: sensor history and event records on littlefs. Storage format
  documented; per master PRD there is **no migration** of Arduino LittleFS history or
  `wifi_config.json` — rev2 starts clean, so the format may be redesigned (note any
  divergence in the parity checklist).
- Mock `IDataStorage` for host tests.

## Out of scope

- WiFi credential provisioning UX (PR-07), serving web assets (PR-09/PR-10),
  persisted event *capture* wiring from pumps/OTA (PR-08), history API (PR-09).

## Functional requirements covered

- FR13 (NVS configuration + factory defaults); enables FR8 (littlefs assets) and the
  history part of FR7.

## Dependencies

- PR-01 (partition table, pinned littlefs component).

## Acceptance criteria

- [CI] Both targets build; littlefs image creation works in the build.
- [CI] Host test: config defaulting, set/get round-trips, factory reset semantics
  against the storage interface (NVS layer mocked or run on linux-target NVS).
- [HIL] Fresh-flashed rig boots, formats/mounts littlefs, reports usage, and persists
  a config change across power cycle.
- [HIL] Factory reset restores documented defaults.

## Estimated size

M
