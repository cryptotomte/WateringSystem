# Partition Plan — ESP-IDF firmware, 4 MB flash

**Status:** Decided (Phase 0, work package B1)
**Target:** ESP32-WROOM-32E (N4 = 4 MB flash), both `BOARD_REV1_DEVKIT` and `BOARD_REV2`
**Related:** `docs/PRD-esp-idf-migration.md` (FR11, open questions), `docs/parity-checklist.md`

This document records the partition layout for the ESP-IDF firmware, with the byte math, margin analysis and growth fallbacks. **It closes the PRD open question** *"Partitionslayout: ryms 2 × app + NVS + littlefs i 4MB?"* — **YES**, with the margins shown below.

## Layout

```csv
# Name,    Type, SubType,  Offset,   Size
nvs,       data, nvs,      0x9000,   0x4000
otadata,   data, ota,      0xd000,   0x2000
phy_init,  data, phy,      0xf000,   0x1000
ota_0,     app,  ota_0,    0x10000,  0x180000
ota_1,     app,  ota_1,    0x190000, 0x180000
storage,   data, littlefs, 0x310000, 0xF0000
```

## Byte math

The region below `0x9000` is reserved by the ESP32 boot ROM convention: second-stage bootloader at `0x1000`, partition table at `0x8000` (one 4 KiB sector).

| Region | Offset | End | Size (hex) | Size (bytes) | Size |
|---|---|---|---|---|---|
| bootloader + partition table | 0x0 | 0x9000 | 0x9000 | 36,864 | 36 KiB |
| `nvs` | 0x9000 | 0xD000 | 0x4000 | 16,384 | 16 KiB |
| `otadata` | 0xD000 | 0xF000 | 0x2000 | 8,192 | 8 KiB |
| `phy_init` | 0xF000 | 0x10000 | 0x1000 | 4,096 | 4 KiB |
| `ota_0` | 0x10000 | 0x190000 | 0x180000 | 1,572,864 | 1.5 MiB |
| `ota_1` | 0x190000 | 0x310000 | 0x180000 | 1,572,864 | 1.5 MiB |
| `storage` | 0x310000 | 0x400000 | 0xF0000 | 983,040 | 960 KiB |
| **Total** | | | **0x400000** | **4,194,304** | **4 MiB exactly** |

Sum check: `0x9000 + 0x4000 + 0x2000 + 0x1000 + 0x180000 + 0x180000 + 0xF0000 = 0x400000`. Every partition is contiguous (each offset equals the previous end) and `ota_0` starts at the required 64 KiB-aligned `0x10000`.

## Rationale

### A/B OTA, no factory partition (FR11)

FR11 requires A/B app partitions with automatic rollback. Two 1.5 MiB OTA slots + a factory partition would need ≥ 4.5 MiB of app space alone — impossible in 4 MB — so there is **no factory partition**. That is safe because:

- With `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, a new image must mark itself valid after boot; if it doesn't (crash, failed self-test), the bootloader reverts to the *other* OTA slot, which always holds the last known-good app. The previous good image therefore plays the role a factory image would.
- Initial bring-up flashes `ota_0` over the wire; from then on every update goes through the A/B cycle (PRD: Paul never flashes by cable after bring-up).
- Residual risk — both slots corrupted simultaneously — is only plausible with flash hardware failure, and is recoverable by wired flash (CP2102N/USB-C on rev2, devkit USB on rev1).

`otadata` is the standard 2-sector (0x2000) boot-selection partition used by the A/B mechanism; `phy_init` is the standard 4 KiB RF calibration data partition.

### App slot size: 1.5 MiB with measured margin

- Current Arduino firmware: `.pio/build/wateringsystem/firmware.bin` = **949,968 bytes (~928 KiB ≈ 950 KB)**, built 2026-04-12. It occupies **60.4%** of a 1.5 MiB slot.
- A comparable ESP-IDF app (WiFi + esp_http_server + esp-modbus + littlefs + OTA) typically lands at **1.1–1.3 MB**:
  - 1.1 MB → 472,864 B free = **30% headroom**
  - 1.3 MB → 272,864 B free = **17% headroom**
  - i.e. roughly **15–30% headroom** across the expected range.
- For comparison, the Arduino build uses PlatformIO `default.csv` with 1.25 MiB (`0x140000`) app slots — the new layout *increases* per-slot app space by 256 KiB.

**Mitigations if the app grows toward the limit** (in order):

1. `CONFIG_COMPILER_OPTIMIZATION_SIZE` (`-Os`) — typically 5–10%.
2. Reduce default log verbosity (`CONFIG_LOG_DEFAULT_LEVEL`/`CONFIG_LOG_MAXIMUM_LEVEL`) — log strings are a real share of .rodata.
3. Disable unused IDF components (Bluetooth/BLE off, trim mbedTLS feature set, IPv6 if unused).
4. Keep web assets gzipped on littlefs (already planned; assets never live in the app image).

**Documented fallback (per PRD):** switch rev2 to an ESP32-WROOM-32E-**N8** or **N16** module — a BOM-only change on the rev2 board. **Decision trigger: release app `.bin` exceeding ~1.3 MB (>85% of slot) sustained across releases.**

### `storage` (littlefs): 960 KiB

Measured web assets today (`data/`):

| File | Raw | Gzipped |
|---|---|---|
| index.html | 22,796 | 4,102 |
| script.js | 56,471 | 11,021 |
| styles.css | 9,217 | 2,178 |
| wifi_setup.html | 14,377 | 3,184 |
| favicon.ico | 15,406 | 2,297 |
| **Total** | **118,267 (~118 KB)** | **22,782 (~23 KB)** |

Serving pre-gzipped assets (FR8; the Arduino server already supports `.gz` variants), the asset footprint is ~23–40 KB even with some growth. After LittleFS metadata overhead (~5%, ~48 KiB on a 960 KiB volume) that leaves **~870–900 KiB for configuration + sensor history**.

**History capacity estimate.** The Arduino format stores `{"timestamp":<10 digits>,"value":<float>}` ≈ 40 bytes/entry; 10 metrics logged every 5 minutes = 2,880 entries ≈ 115 KB/day. ~900 KiB therefore holds **~22,500 entries ≈ 7–8 days** at parity cadence in the naive JSON format — and **several months** if the IDF port uses a compact binary ring buffer (~10 B/entry) or actually prunes (the Arduino code ships a `pruneOldReadings()` that is never called; see parity checklist §6). Note the Arduino `default.csv` gives littlefs 1.375 MiB, so this layout trades ~0.45 MiB of history space for the larger A/B app slots — acceptable since the PRD explicitly excludes history migration (fresh start on rev2).

### `nvs`: 16 KiB

16 KiB = 4 NVS pages (4 KiB each); NVS reserves one page for garbage collection, leaving 3 active pages ≈ **378 entries**. Expected usage:

- WiFi credentials/state stored by `esp_wifi` (`nvs.net80211`): ~10 entries, < 1 KiB.
- Watering configuration (FR13): the 7 parity keys (thresholds, durations, intervals, enabled flag) as one ~200 B blob or individual entries.
- Reservoir feature flags, board/provisioning settings, a few counters (boot count, OTA bookkeeping; the A/B selection itself lives in `otadata`).

Estimated steady-state usage **< 2 KiB (~15%)** — ample, including wear-leveling headroom. Factory reset = erase the `nvs` partition; defaults are compiled in (FR13).

## Conclusion

2 × 1.5 MiB app + 16 KiB NVS + 960 KiB littlefs + system partitions fit a 4 MB module **exactly** (sum = 0x400000), with ~15–30% app headroom against a realistic IDF binary and ~900 KiB of filesystem free space after gzipped assets. The PRD assumption "[ANTAGANDE: 4MB räcker]" is confirmed; the N8/N16 fallback remains documented above with a concrete trigger.
