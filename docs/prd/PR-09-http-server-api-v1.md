# PR-09: http-server-api-v1

> Phase 3 — web

## Goal

Replace ESPAsyncWebServer with `esp_http_server` and deliver the frozen `/api/v1/`
REST/JSON contract (OpenAPI sketch) — the documented API contract is itself a project
deliverable and the input to the future frontend PRD.

## Scope

- `esp_http_server` setup (this PR removes the *raison d'être* of the old
  `-UHTTP_GET` PlatformIO hack — no framework macro conflicts).
- `/api/v1/` endpoint set (consistent REST/JSON, FR7):
  - System status (mode, WiFi, time-sync, uptime, reset reason, firmware version).
  - Environmental + soil sensor readings (incl. validity flags).
  - Sensor history from littlefs storage.
  - Pump control: start/stop/timed-run for both pumps, mode switch manual/automatic.
    Honor the Arduino lesson that pump operations must be reliable — define idempotent
    semantics and explicit error responses; the old GET/POST-form-data workaround is
    replaced by a clean JSON contract.
  - Configuration get/set (thresholds, intervals, durations) with validation.
  - INA226 readings per pump on rev2 (`null`/absent on rev1) (FR6 exposure).
  - Diagnostics (FR12 API side): event log retrieval, RS485/sensor self-test trigger —
    exact diagnostics scope is the master PRD's open question, settled in this spec.
  - OTA-trigger endpoint **stub** (contract defined here, implementation in PR-13).
- **OpenAPI 3 sketch** committed under `docs/api/` and marked frozen at merge;
  contract changes after this PR require explicit version bump discussion.
- JSON via a pinned, IDF-registry-available approach (cJSON in IDF) — no ArduinoJson.
- Safety invariant: HTTP layer calls into controller/actuator interfaces only; no
  watering decisions in handlers.

## Out of scope

- Serving the frontend assets (PR-10). Automatic-mode logic behind the mode switch
  (PR-11 — until then the endpoints operate pumps/config directly). OTA execution
  (PR-13). Frontend modernization (separate PRD).

## Functional requirements covered

- FR7 (full API + frozen contract); FR6 (API exposure); FR12 (API diagnostics).

## Dependencies

- PR-06 (config/history storage), PR-07 (network). Live sensor data presumes
  PR-03/PR-04/PR-05 are merged (they are, in graph order).

## Acceptance criteria

- [CI] Both targets build. Host/CI contract check: every endpoint in the OpenAPI
  sketch has a registered handler (route-table test), JSON serialization unit-tested.
- [HIL] All endpoints answer per contract on the rig (scripted curl checklist
  included in the PR); pump start/stop/timed-run works and respects max runtimes.
- [HIL] Invalid input (bad JSON, out-of-range config) returns 4xx with error body,
  never crashes or hangs the server.
- Review: OpenAPI sketch approved and marked frozen.

## Estimated size

L
