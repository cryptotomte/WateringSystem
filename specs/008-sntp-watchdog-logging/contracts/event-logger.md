# Contract: `EventLogger` (over PR-06 `IDataStorage`)

`firmware/main/event_logger.{h,cpp}`. Composes `IDataStorage& storage` + `IWallClock& clock`. Formatting +
category selection are host-tested against `MockDataStorage` + `FakeWallClock`. Does NOT extend
`IDataStorage`.

## Methods (typed producers)

| Method | Category | Example `detail` (≤120 B) |
|---|---|---|
| `void logReset(esp_reset_reason_t r)` | `kCategoryReset` | `"reset=TASK_WDT"` |
| `void logWifi(WifiState s)` | `kCategoryConnectivity` | `"wifi=Connected"` / `"wifi=Reconnecting"` |
| `void logPumpStart(pumpName, cause)` | `kCategoryPump` | `"pump=plant start cause=console dur=30s"` |
| `void logPumpStop(pumpName, cause)` | `kCategoryPump` | `"pump=plant stop cause=timeout"` |
| `void logFailsafe(detail)` | `kCategoryFailsafe` | `"failsafe=soil-invalid pump=plant"` (producer = PR-11) |
| `void logOta(detail)` | `kCategoryOta` | (producer = PR-13) |

## Behavioral contract

- Every method calls `storage.storeEvent(clock.nowEpoch(), category, detail)`.
- **Never throws / never blocks watering.** A `storeEvent` returning `false` is counted (a `droppedEvents`
  counter) and `ESP_LOGW`'d once; the caller is unaffected (FR-014).
- Credential values are never logged (WiFi events log the *state*, not SSID/password).
- Detail strings are built deterministically so the host tests can assert exact bytes; the store truncates
  at 120 B (never rejects) so builders need not pre-truncate but should stay concise.
- `EventLogger` holds references (not ownership); the injected `IDataStorage` must be the cross-task
  `LockedDataStorage` when shared (it is — `app_main` passes the locked wrapper).

## Host tests (`test_event_logger.cpp`)

- Each producer writes exactly one event with the expected category and a detail matching the documented
  format (assert against `MockDataStorage`'s captured records + `FakeWallClock`'s epoch).
- `storeEvent`==false path: `MockDataStorage` set to fail writes → `EventLogger` increments its dropped
  counter and does not crash.
- Reset-reason mapping is total over `esp_reset_reason_t` (mapping function host-tested with the enum
  values available on the host, or a small local mirror enum if the IDF enum isn't linux-visible).
