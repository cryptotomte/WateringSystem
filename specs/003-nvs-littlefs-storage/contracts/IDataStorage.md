# Contract: IDataStorage

**Component**: `firmware/components/interfaces/include/interfaces/IDataStorage.h`
**Feature**: 003-nvs-littlefs-storage

Sensor history, event records, and storage statistics. Replaces the data half of
the legacy `IDataStorage`. The two legacy methods with no callers
(`getLastSensorReading`, `pruneOldReadings`) are dropped; retention is an
internal bounded-storage guarantee instead of a caller obligation (spec FR-010/
FR-012).

## Types

```text
SensorReading { metric: string, epoch: uint32, value: float }
EventRecord   { epoch: uint32, category: uint8 enum, detail: string ≤120 }
StorageStats  { total_bytes: uint32, used_bytes: uint32 }
```

Event categories: pump=1, failsafe=2, connectivity=3, ota=4, reset=5 (PR-08 may
extend; unknown values are stored and returned verbatim).

## Operations

| Operation | Contract |
|---|---|
| `storeSensorReading(metric, epoch, value) -> bool` | Appends; durable once true is returned (survives power loss). Unknown metric accepted up to 10 metric directories; the 11th distinct metric is rejected (false). Bounding/eviction is internal (≥30-day retention at default log interval, oldest-first eviction). |
| `getSensorReadings(metric, t0, t1) -> vector<SensorReading>` | Chronological, inclusive range. Empty vector on no data, unknown metric, t0 > t1, or read error — never throws/fails (legacy parity). |
| `storeEvent(epoch, category, detail) -> bool` | Appends; `detail` longer than 120 bytes is silently truncated (the event is always recorded, never rejected for length). Rotation keeps total event storage ≤ budget and always retains the newest records. |
| `getEvents(maxCount) -> vector<EventRecord>` | Newest-first, at most maxCount. Empty vector on no data/error. |
| `getStorageStats() -> StorageStats` | Total/used bytes of the data filesystem. |

## Invariants

1. History and event storage never exceed their documented budgets
   (data-model.md); writes at the bound evict oldest data, never fail the append.
2. A power loss during append loses at most the in-flight record; previously
   stored records remain readable (torn tails detected and skipped on read).
3. Reads are side-effect free except torn-tail truncation/skip.
4. Timestamps are caller-supplied epoch seconds, stored verbatim (parity
   checklist 184).
5. Header is host-includable: no IDF/hardware includes (Constitution II).

## Implementations

- `LittleFsDataStorage`: POSIX stdio against an injectable base path — `/storage`
  (VFS) on target, temp dir in host tests. On-disk formats in data-model.md.
  Stats provider injected (esp_littlefs_info on target, fake on host).
- `StorageMount` (target-only helper, not part of the interface): mount-or-format
  of the `storage` partition at startup, per research D2.
- `MockDataStorage` (header-only, `testing/`): in-memory vectors for consumer
  tests in later PRs (PR-08, PR-09).
- Concurrency: unsynchronized; cross-task consumers use the `Locked*` decorator.
