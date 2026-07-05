# Phase 1 Data Model: SNTP Time, Task Watchdog & Event Logging

In-memory firmware state + the reused PR-06 event-log record. No new persisted schema (events reuse PR-06).

## Entities

### WallClock / time state (`IWallClock` + `SyncStatus`)

| Field | Type | Rules |
|---|---|---|
| nowEpoch | uint32 (epoch s) | current wall-clock; before sync it is the low/boot value |
| isTimeSet | bool | true iff nowEpoch ≥ plausibility threshold (year ≥ 2020) |
| synced | bool | a successful SNTP sync has occurred |
| lastSyncEpoch | uint32 | epoch of the last successful sync (0 if never) |

- `IWallClock` (interfaces): `uint32_t nowEpoch() const; bool isTimeSet() const;` — pure seam.
- `SyncStatus` (pure): `synced`, `lastSyncEpoch`; updated by the SNTP callback. Exposed for the console
  `time` line.
- Plausibility threshold is a compile-time constant (min plausible epoch for 2020-01-01).

### Event log entry (reused from PR-06 `IDataStorage::EventRecord`)

| Field | Type | Source in PR-08 |
|---|---|---|
| epoch | uint32 | `IWallClock::nowEpoch()` at the moment of the event (best-known) |
| category | uint8 | `kCategoryReset/Connectivity/Pump/Failsafe/Ota` (existing constants) |
| detail | string ≤120 B | cause string built by `EventLogger` (e.g. `"reset=TASK_WDT"`, `"wifi=Connected"`, `"pump=plant start cause=console 30s"`) |

- Rotation/bounding is PR-06's (2×16 KiB, newest-retained). PR-08 does not change it.

### Reset reason

- Captured once at boot via `esp_reset_reason()` → mapped to a short string
  (`POWERON/SW/PANIC/INT_WDT/TASK_WDT/BROWNOUT/DEEPSLEEP/…`). Logged as one `kCategoryReset` event.

### Watchdog registration

| Field | Type | Notes |
|---|---|---|
| subscribed tasks | set | main loop (app_main task) + sensor task now; PR-11 tasks later |
| timeout | seconds | `CONFIG_WS_TASK_WDT_TIMEOUT_S` (default with margin over 5 s sensor cadence) |
| action | fixed | panic → reboot |
| excluded | — | WiFi task (deliberate — network stall must not reboot) |

## State / transitions

### Time sync lifecycle

```text
[boot] time-not-set (nowEpoch < threshold, synced=false)
   │  STA reaches Connected  → SntpClient.start() (once)
   ▼
 waiting-for-sync (still not-set; non-fatal if server unreachable, keeps retrying)
   │  SNTP callback fires with a plausible time → settimeofday
   ▼
 time-set (synced=true, lastSyncEpoch set, isTimeSet()=true)   ── periodic re-sync keeps it fresh
```

- Never blocks: the device runs in time-not-set indefinitely if offline. Consumers gate on `isTimeSet()`.

### Watchdog lifecycle (per subscribed task)

```text
task start → esp_task_wdt_add(NULL) → loop{ do work; esp_task_wdt_reset() } 
   │  a loop iteration exceeds the timeout without reset
   ▼
 TASK_WDT panic → reboot → pumps_force_off() first → esp_reset_reason()==TASK_WDT logged
```

## Validation rules (pure, host-tested)

- `isPlausibleEpoch(uint32 e)`: `e >= kMinPlausibleEpoch` (2020-01-01T00:00ःZ).
- `formatLocal(uint32 e) -> string`: with `TZ=CET-1CEST,M3.5.0,M10.5.0/3`, a winter epoch renders `+01:00`
  (CET), a summer epoch renders `+02:00` (CEST); DST switch at the last Sunday of March/October.
- `resetReasonString(esp_reset_reason_t) -> const char*`: total mapping over the enum.
- `EventLogger` detail builders: category chosen per producer; detail truncated by the store at 120 B
  (never rejected); a `storeEvent`==false is counted, not fatal.
