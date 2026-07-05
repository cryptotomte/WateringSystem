# Contract: API envelope, routes & error model

## Base

- All endpoints under `/api/v1/`. JSON request/response bodies; `Content-Type: application/json`.
- **Envelope**: success `{ "success": true, ...payload }`; error `{ "success": false, "error": "<msg>" }`.
- **Statuses**: 200 OK; 400 bad request (malformed JSON / validation); 404 unknown `/api/*`; 405 method not
  allowed (optional); 501/stub for OTA; 503 if a required subsystem is unavailable (documented per route).
- **404 for unknown `/api/*`** returns the JSON error envelope (not HTML/plain text) — parity §4.
- **No authentication** in v1 (LAN device). `getWifiPassword()` is never serialized.

## Route table (pure `ApiRoutes.h`; asserted against `docs/api/openapi.yaml`)

| Method | Path | Handler | Notes |
|---|---|---|---|
| GET  | `/api/v1/status`        | status      | system status DTO (mode/wifi/time/uptime/reset/fw/storage[/power]) |
| GET  | `/api/v1/sensors`       | sensors     | cached env+soil+level[+power]; non-blocking; valid flags |
| GET  | `/api/v1/history`       | history     | query: metric, reading?, range|start/end; series |
| GET  | `/api/v1/pumps`         | pumpsList   | capability-enumerated pump DTOs |
| POST | `/api/v1/pumps/{name}`  | pumpCmd     | body {action, durationS?}; start/run/stop |
| GET  | `/api/v1/config`        | configGet   | ConfigDto (no wifi password) |
| POST | `/api/v1/config`        | configSet   | validated all-or-nothing; returns new config |
| GET  | `/api/v1/power`         | power       | rev2 only; 404/`null` shape on rev1 |
| GET  | `/api/v1/events`        | events      | newest-first; `count` query (bounded) |
| POST | `/api/v1/selftest`      | selfTest    | sensor/RS485 self-test; structured result |
| POST | `/api/v1/ota`           | otaStub     | contract stub (PR-13) |

(Exact path style for pump command — `/pumps/{name}` with `action` in the body vs `/pumps/{name}/{action}`
— is finalized in the OpenAPI sketch; the route table + test must match whatever the sketch says.)

## Route-table contract test (host, FR-004)

- `matchRoute(method, path)` returns the `HandlerId` for each documented route and `NotFound` for unknown
  `/api/*`.
- A host test parses the path set from `docs/api/openapi.yaml` (or a committed list mirrored from it) and
  asserts it equals the `ApiRoutes` table set — every documented endpoint has a registered handler and vice
  versa. This is the CI gate for contract coverage.

## Isolation (FR-015)

Handlers run on the `esp_http_server` task, never WDT-subscribed, sharing no mutex with the watering loop
beyond the `Locked*` wrappers. A stalled/flooding client cannot delay the 10 Hz loop or the sensor task.
