# Phase 1 Data Model: HTTP REST/JSON API v1

Plain in-memory DTOs used by the pure serialize/parse layer. No new persisted schema (config → PR-06 NVS,
history/events → PR-06 littlefs). DTOs are populated by the thin handler from the `Locked*` interfaces,
then handed to the pure serializers — so serialization is deterministic and host-tested.

## Response envelope

Every response is JSON with a consistent shape:
- Success: `{ "success": true, ...payload }`
- Error: `{ "success": false, "error": "<message>" }` with an appropriate HTTP status (4xx/5xx).
- Unknown `/api/*`: `{ "success": false, "error": "not found" }`, status 404.

## DTOs (pure structs → cJSON)

### SystemStatusDto
`mode` (string: "manual"|"automatic", from `wateringEnabled`), `wifi` { state (string), rssi, ssid,
connected/ipAcquired, ip (string, from esp_netif) }, `time` { synced (bool), epoch, local (string),
lastSync }, `uptimeMs`, `resetReason` (string), `firmware` { version, project }, `storage` { totalBytes,
usedBytes, percentUsed }. rev2 adds `power` (see below) or null on rev1.

### SensorReadingsDto
- `environmental` { valid (bool), temperature, humidity, pressure } — fresh (sensor_task 5 s).
- `soil` { valid (bool), moisture, temperature, humidity, ph, ec, nitrogen, phosphorus, potassium } — NPK
  included only when ≥ 0; **`valid=false` until PR-11's reader** (last-good/NaN otherwise).
- `level` { low { valid, waterPresent }, high { valid, waterPresent } } — fresh (10 Hz).
- `power` (rev2) { valid, busVoltage, current, power } or null (rev1) — last-good until first read.
- top-level `timestamp` (epoch; reflects not-set state per PR-08 rather than a bogus 1970).

Validity rule: `valid` is derived from the cached-getter validity (`getLastError()==0` and/or non-NaN);
the handler NEVER calls `read()`/`isAvailable()` (QUIRK 5).

### PumpDto / PumpCommand
- `PumpDto`: `name` ("plant"|"reservoir"), `running` (bool), `currentRunTimeMs`, `accumulatedRunTimeMs`,
  `lastStopReason` (string). The pump LIST is capability-enumerated (`#if BOARD_HAS_RESERVOIR_PUMP`) —
  rev1 = {plant, reservoir}, rev2 = {plant}.
- `PumpCommand` (parsed from body): `action` ("start"|"stop"|"run"), `durationS` (for run/start; 1..300).
  Semantics: `run`/`start` with duration → `runFor(durationS)`; `stop` → `stop()`. Idempotent: start on an
  already-running pump is rejected with an explicit error (matches `runFor` which does not restart the
  clock), stop on a stopped pump is a success no-op.

### ConfigDto / ConfigSetRequest
- `ConfigDto` (get): moistureThresholdLow/High (0..100), wateringDurationS (1..300), minWateringIntervalS
  (≥1), wateringEnabled, sensorReadIntervalMs (≥1000), dataLogIntervalMs (≥60000). **Never** wifi password.
- `ConfigSetRequest` (parsed): any subset of the settable fields. Validation is ALL-OR-NOTHING: each field
  is range-checked (constants from `IConfigStore.h`); if any field is out of range/malformed → 4xx error
  identifying the field, nothing applied. Valid → apply via setters (which persist) and return the new config.

### HistoryQuery / HistorySeries
- `HistoryQuery`: `metric` (string), optional `reading`, `range` (named: 1h/6h/24h/7d/30d) OR explicit
  `start`/`end` epochs (default last 24 h). Named range → [now-range, now].
- `HistorySeries`: `timestamps[]`, `values[]` (aligned), plus an echo of metric/reading/start/end/count.
  Empty arrays for a range with no data (not an error).

### EventDto
From `IDataStorage::getEvents(maxCount)` (newest-first): `epoch`, `category` (int + optional name),
`detail`. `maxCount` from a bounded query param (default e.g. 50).

### SelfTestResultDto
`{ overall (pass/fail), checks: [ { name, ok, detail } ] }` — runs the sensor/RS485 self-test. (This is the
one read path allowed to perform a bus transaction, since it is an explicit on-demand diagnostic, not a
status poll — it runs off the httpd task and must still be bounded; document the exception.)

### OtaTriggerResponse (stub)
Defined shape per the OpenAPI sketch (e.g. `{ success:false, error:"not implemented" }` / 501) — PR-13.

## Route table (`ApiRoutes.h`, pure data)

`struct ApiRoute { const char* path; HttpMethod method; HandlerId id; };` a static array covering (all
under `/api/v1/`): `GET /status`, `GET /sensors`, `GET /history`, `GET /pumps`, `POST /pumps/{name}` (or
`/pumps/{name}/{action}`), `GET /config`, `POST /config`, `GET /power` (rev2), `GET /events`,
`POST /selftest`, `POST /ota` (stub). The route-table host test asserts this set equals the OpenAPI sketch's
path set (contract coverage, FR-004).

## Validation / pure-logic rules (host-tested)

- `parseConfigSet(json) -> ConfigSetRequest | error` — rejects out-of-range per the header constants; all
  fields optional; all-or-nothing.
- `parsePumpCommand(json) -> PumpCommand | error` — action ∈ {start,run,stop}; duration 1..300 for run.
- `serialize*Dto(dto) -> std::string` — deterministic JSON (golden/round-trip tested).
- `matchRoute(method, path) -> HandlerId | NotFound` — exact match; unknown `/api/*` → NotFound (404 JSON).
- `namedRangeToWindow(range, now) -> {t0,t1}` — 1h/6h/24h/7d/30d arithmetic.
