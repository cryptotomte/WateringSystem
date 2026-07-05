# Contract: per-endpoint behavior (behavioral coverage of parity §4)

All reads use non-blocking cached getters through `Locked*` wrappers (QUIRK 5 — never `read()`/
`isAvailable()` in a handler).

## GET /status
Returns `SystemStatusDto`: `mode` (from `wateringEnabled`), `wifi` {state, rssi, ssid, ipAcquired, ip},
`time` {synced, epoch, local, lastSync}, `uptimeMs`, `resetReason`, `firmware` {version, project},
`storage` {totalBytes, usedBytes, percentUsed}, `power` (rev2 INA226 last-good) or null (rev1). WiFi via
`LockedWifiManager` snapshot + `esp_netif_get_ip_info`; time via `IWallClock`/`SyncStatus`/`TimeService`;
reset via `resetReasonName(esp_reset_reason())`; storage via `IDataStorage::getStorageStats()`.

## GET /sensors
`SensorReadingsDto` (env fresh; level fresh; **soil valid=false until PR-11**; power last-good rev2). Per
parity §4: environmental {temperature, humidity, pressure}; soil {moisture, temperature, humidity, ph, ec,
+ NPK only when ≥0}; per-section `valid`; top-level epoch `timestamp`. MUST NOT block on the bus.

## GET /history
Query `metric` (+ optional `reading`), `range` ∈ {1h,6h,24h,7d,30d} OR explicit `start`/`end` (default last
24 h). Returns `{ timestamps[], values[], metric, reading, start, end, count }` via
`IDataStorage::getSensorReadings(metric, t0, t1)`. Empty arrays (not an error) for a range with no data.

## GET /pumps + POST /pumps/{name}
- GET: array of `PumpDto` for the board's pumps (rev1 plant+reservoir; rev2 plant) — capability-enumerated,
  never assume two.
- POST body `{ action: start|run|stop, durationS? }`. `run`/`start` → `LockedWaterPump::runFor(1..300)`
  (hard cap enforced internally; start on already-running → explicit 4xx error, no clock restart); `stop` →
  `stop()` (success no-op if stopped). Response reports the resulting `PumpDto`. Unknown pump name → 404.

## GET /config + POST /config
- GET: `ConfigDto` (thresholds/durations/intervals/enabled) — never wifi password.
- POST: any subset; each field range-checked (constants from `IConfigStore.h`: moisture 0..100, duration
  1..300, interval ≥1, sensorRead ≥1000 ms, dataLog ≥60000 ms). ALL-OR-NOTHING: any invalid field → 400
  with the offending field, nothing applied. Valid → apply via setters (persist) → return new `ConfigDto`.

## GET /power (rev2)
INA226 last-good `{ valid, busVoltage, current, power }`. On rev1: 404 or a `null`/not-available shape
(board-capability driven, decided in the OpenAPI sketch).

## GET /events
`IDataStorage::getEvents(count)` newest-first: `[ { epoch, category, detail } ]`; `count` query param
bounded (default 50).

## POST /selftest
Runs the sensor/RS485 self-test (the one on-demand path allowed a bounded bus transaction — explicit
diagnostic, off the httpd task's critical path). Returns `{ overall, checks:[{name, ok, detail}] }`.

## POST /ota (stub)
Contract-only: returns the defined stub response (PR-13 implements execution). Documented in the OpenAPI
sketch and frozen.

## Errors (all endpoints)
Malformed JSON → 400 error envelope; unknown route → 404 error envelope; missing/absent board feature →
clear not-available response; never crash or hang the server (FR-015 / SC-003).
