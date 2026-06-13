# Data Model: NVS Configuration and LittleFS Data Storage

**Feature**: 003-nvs-littlefs-storage | **Date**: 2026-06-11

## Configuration items (NVS, namespace `wscfg`)

| Item | NVS key | NVS type | Logical type | Default | Valid range | Unit |
|---|---|---|---|---|---|---|
| moistureThresholdLow | `moist_low` | u32 (float bits) | float | 30.0 | 0–100 | % |
| moistureThresholdHigh | `moist_high` | u32 (float bits) | float | 55.0 | 0–100 | % |
| wateringDuration | `water_dur` | u32 | uint32 | 20 | 1–300 | s |
| minWateringInterval (soak pause) | `soak_pause` | u32 | uint32 | 300 | ≥ 1 | s |
| wateringEnabled | `water_en` | u8 | bool | true | 0/1 | — |
| sensorReadInterval | `read_iv` | u32 | uint32 | 5000 | ≥ 1000 | ms |
| dataLogInterval | `log_iv` | u32 | uint32 | 300000 | ≥ 60000 | ms |
| wifiSsid | `wifi_ssid` | str | string | "" (unconfigured) | ≤ 32 bytes | — |
| wifiPassword | `wifi_pass` | str | string | "" | ≤ 64 bytes | — |

Rules:

- Read of a missing key → compiled-in default. Read of a stored value outside its
  valid range → compiled-in default (FR-002; the invalid entry is left in place
  until the next valid write).
- Write of an out-of-range value → rejected, stored value untouched (FR-003).
- Each accepted write goes directly to its own NVS entry (atomic old-or-new under
  power loss; research D5/D8).
- Factory reset: `nvs_flash_erase_partition` + re-init → every read returns
  defaults; credentials gone (FR-005). WiFi credentials never appear in logs or
  diagnostics output (FR-004).
- Reservoir flags deliberately absent (FR-006, deferred to PR-05). New items =
  new keys in the same namespace; no schema version needed (per-key layout is
  self-describing).
- Defaults/ranges mirror the legacy firmware (`src/WateringController.cpp:16-21`,
  `:445-475`) except: soak-pause semantics (2026-06-10 decision, enforced in
  PR-11) and the two interval items being settable (2026-06-11 clarification).
  Range floors for the intervals are new (legacy had none); 1 s / 1 min floors
  prevent log-storm misconfiguration.

## Sensor reading (littlefs)

Logical entity: `{metric: string, timestamp: uint32 epoch seconds, value: float}`.

Metric identifiers follow legacy naming: `env_temperature`, `env_humidity`,
`env_pressure`, `soil_moisture`, `soil_temperature`, `soil_ph`, `soil_ec`,
`soil_nitrogen`, `soil_phosphorus`, `soil_potassium`. The set is open — unknown
metrics are accepted (spec edge case), subject to a max of **10** metric
directories (budget guard sized to the legacy metric set: 10 × 80 KiB = 800 KiB
is the worst case the partition can absorb; oldest-inactive is NOT auto-evicted,
an 11th distinct metric is rejected with an error — prevents a buggy caller from
silently destroying history or blowing the budget).

On-disk layout (research D6):

```text
/storage/hist/<metric>/<first_epoch>.dat   # append-only chunk
```

- Record: 8 bytes LE: `uint32 epoch`, `float value`. No per-record framing needed
  beyond fixed size; a torn tail (file size % 8 ≠ 0) is truncated-on-read.
- Chunk cap: 8 KiB = 1024 records. New chunk when cap reached; filename = first
  record's epoch.
- Ring bound: max 10 chunks per metric (80 KiB); creating chunk #11 deletes the
  oldest (atomic remove). Guarantees ≥31.9 days at the 5-min default interval.
- Query (metric, t0, t1): pick chunks whose [first_epoch, next chunk's
  first_epoch) overlaps the range, scan, filter. Chronological order is by
  construction (appends use caller-supplied epochs; a non-monotonic timestamp is
  stored as-is — time correctness is the caller's concern, parity checklist 184).
- Empty/no-data/unknown-metric query → empty result, not an error (FR-009).

## Event record (littlefs)

Logical entity: `{timestamp: uint32 epoch, category: enum, detail: string ≤120}`.
A longer `detail` is silently truncated to 120 bytes on store (documented
contract semantics — never rejected, the event itself is always recorded).

Categories (u8): `pump=1`, `failsafe=2`, `connectivity=3`, `ota=4`, `reset=5`
(FR-011; PR-08 may extend the enum — unknown categories are stored and returned
verbatim).

On-disk layout (research D7):

```text
/storage/events/0.log
/storage/events/1.log
```

- Record framing: `0xEV-marker byte (0xE7)`, `uint32 epoch`, `uint8 category`,
  `uint8 detail_len`, `detail bytes`. Torn tail detected by marker/length
  mismatch and skipped.
- Active file appends until 16 KiB cap; rotation truncates the other file and
  switches (oldest half dropped, newest always retained — FR Acceptance US3.2).
- Retrieval: newest-first across both files, optional max-count.

## Storage statistics

`{total_bytes: uint32, used_bytes: uint32}` from `esp_littlefs_info("storage")`
on target; mocked on host. Exposed through the data-storage interface for the
serial status line and PR-09 status API (FR-008, parity checklist 106).

## Budget (worst case)

| Consumer | Budget |
|---|---|
| History: 10 metrics × 80 KiB | 800 KiB |
| Events: 2 × 16 KiB | 32 KiB |
| Headroom (littlefs metadata, future) | ~38–68 KiB |
| **Total available after web assets** | **~870–900 KiB** |

## State transitions

- **Filesystem**: unmounted → mounted (boot, valid FS) | unmounted → formatted →
  mounted (first boot/corruption; FR-007) — data loss accepted, bricking not.
- **Config item**: missing → default-on-read → set (valid write) → updated |
  unchanged (invalid write rejected) → missing (factory reset).
- **History chunk**: active (appending) → sealed (cap reached, successor created)
  → deleted (ring eviction).
- **Event file**: active (appending) → full (cap) → standby (other truncated,
  becomes active) → truncated.
