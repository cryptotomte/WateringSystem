# Feature Specification: HTTP REST/JSON API v1

**Feature Branch**: `009-http-server-api-v1`

**Created**: 2026-07-05

**Status**: Draft

**Input**: PR-09 (`docs/prd/PR-09-http-server-api-v1.md`) — replace the legacy web server with an
`esp_http_server`-based, frozen `/api/v1/` REST/JSON contract plus a committed OpenAPI 3 sketch, covering
system status, sensor readings + history, pump control, configuration, power telemetry, diagnostics, and an
OTA-trigger stub. Behavioral coverage of `docs/parity-checklist.md` §4 (not URL-for-URL parity). Depends on
PR-06 (storage) and PR-07 (network); live data from PR-03/04/05.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Read the system's state over the network (Priority: P1)

An operator (or a future frontend/dashboard) fetches the device's current state — mode, network, time
status, uptime, firmware version, storage usage, and the latest environmental + soil readings — as clean
JSON over HTTP, without ever disturbing the watering system. These reads always return quickly and never
block on a slow field-bus sensor.

**Why this priority**: Visibility is the foundation of the whole API and the frontend that follows. It is
also the lowest-risk slice — pure reads that touch only cached state — and it delivers immediate value
(remote monitoring) on its own. It exercises the server, routing, and JSON serialization that every other
endpoint reuses.

**Independent Test**: With the device on the network, request the status and sensor-reading endpoints and
confirm they return well-formed JSON matching the contract, with correct validity flags and a timestamp,
and that the response is fast even while a sensor read/RS485 transaction is in progress (no blocking).

**Acceptance Scenarios**:

1. **Given** the device is connected, **When** the status endpoint is requested, **Then** it returns JSON
   with mode, WiFi state, time-sync status, uptime, reset reason, firmware version, and storage usage.
2. **Given** cached sensor values exist, **When** the sensor-readings endpoint is requested, **Then** it
   returns environmental and soil sections with per-section validity/success flags and an epoch timestamp,
   serving the last task-read values — it MUST NOT trigger a fresh blocking field-bus read in the handler.
3. **Given** a sensor is currently invalid/absent, **When** its readings are requested, **Then** the
   response marks that section not-valid rather than blocking, erroring the whole response, or fabricating
   values.
4. **Given** the device is on rev2, **When** the status/power data is requested, **Then** the pump power
   (INA226) channel is included; **on rev1** that field is null/absent.
5. **Given** any request, **When** the server handles it, **Then** it never blocks or delays the watering
   task, even under a slow or stalled client.

---

### User Story 2 - Control pumps, mode, and configuration over the network (Priority: P2)

An operator starts/stops a pump, runs a pump for a fixed duration, switches between manual and automatic
mode, and reads/updates configuration (moisture thresholds, intervals, durations) — all via clean JSON
requests with explicit success/error responses and safe, idempotent semantics.

**Why this priority**: This is the interactive control surface. It must be reliable (the legacy GET/
form-data workarounds are replaced by a clean JSON contract) and safe — every actuation goes through the
existing actuator/config interfaces with their validation and the hard runtime cap, and no watering
decision is made in the HTTP layer.

**Independent Test**: Start a timed pump run via the API and confirm it runs and auto-stops within the cap;
stop it via the API; set an out-of-range config value and confirm a 4xx error with a body and no state
change; set a valid config value and confirm it persists.

**Acceptance Scenarios**:

1. **Given** the board's pump set, **When** the pump list is requested, **Then** the response enumerates
   exactly the pumps the board exposes (rev1: plant + reservoir; rev2: plant only) rather than assuming two.
2. **Given** a named pump, **When** a start / timed-run / stop is requested, **Then** the pump acts
   accordingly, a timed run is capped at the hard maximum runtime, and the response reports success or an
   explicit error.
3. **Given** a pump is already running, **When** a start is requested again, **Then** the operation has
   well-defined idempotent semantics (no crash, no undefined double-start) and the response reflects the
   resulting state.
4. **Given** a configuration change, **When** valid values are submitted, **Then** they are validated and
   persisted; **when** out-of-range or malformed values are submitted, **then** the response is a 4xx with
   an error body and nothing is changed.
5. **Given** a mode switch request, **When** manual/automatic is set, **Then** the mode flag is stored and
   reflected in status (the automatic watering logic itself is delivered later — PR-11).

---

### User Story 3 - Retrieve history, diagnostics, and trigger maintenance (Priority: P3)

An operator retrieves historical sensor trends over a chosen time range, reads the persistent event log,
triggers a sensor/RS485 self-test, and (contract-only) sees the OTA-trigger endpoint — enough to diagnose
the device and, later, update it.

**Why this priority**: Observability and maintenance round out the API. These endpoints depend on the
storage/event surfaces from PR-06/PR-08 and are less time-critical than status and control, so they come
last while still completing the frozen contract.

**Independent Test**: Query history for a metric over a range and confirm timestamps/values are returned;
read the event log and confirm recent events appear; trigger the self-test and confirm a structured result;
confirm the OTA-trigger endpoint responds per its stub contract.

**Acceptance Scenarios**:

1. **Given** stored history, **When** history is queried by metric + reading + time range (named range or
   explicit start/end), **Then** the response returns aligned timestamps and values plus an echo of the
   query, and an empty result for a range with no data (not an error).
2. **Given** the persistent event log, **When** it is requested, **Then** recent events are returned
   (newest-first) with their timestamp, category, and detail.
3. **Given** a self-test trigger, **When** invoked, **Then** the device runs the sensor/RS485 self-test and
   returns a structured pass/fail result.
4. **Given** the OTA-trigger endpoint, **When** called, **Then** it responds per the defined stub contract
   (the actual OTA execution is delivered in PR-13).

---

### Edge Cases

- **Unknown route**: a request to an unknown `/api/*` path returns a JSON error body (not HTML/plain text)
  with a 404 status; the contract's error shape is consistent across endpoints.
- **Malformed JSON body**: rejected with a 4xx error body; never crashes or hangs the server.
- **Field absent on this board**: rev1 requests for INA226/power or a rev2-only pump return a clear
  not-available response, not a crash or a fabricated value.
- **Slow/stalled client**: a client that stops reading mid-response must not stall the watering task (the
  server is isolated on its own task; watering is unaffected).
- **History range with no data / missing metric**: returns an empty series, not an error.
- **Config set partially valid**: a request mixing valid and out-of-range fields is rejected as a whole
  with an error identifying the offending field(s); no partial application that leaves inconsistent state.
- **Concurrent requests touching a shared device**: serialized through the existing per-device locks; no
  torn reads or races with the watering/sensor tasks.
- **Request before time is set**: timestamps reflect the not-set state (per PR-08) rather than a bogus
  1970 value presented as valid.

## Requirements *(mandatory)*

### Functional Requirements

**Server & contract**

- **FR-001**: The system MUST serve a versioned `/api/v1/` REST/JSON API over HTTP while in station mode,
  on its own task, isolated so that HTTP activity never blocks, delays, or influences the watering path.
- **FR-002**: The API MUST use JSON request/response bodies with a consistent success/error envelope;
  unknown `/api/*` routes MUST return a JSON error body with a 404 status.
- **FR-003**: An OpenAPI 3 sketch of the full contract MUST be committed under `docs/api/` and marked
  frozen at merge; later contract changes require an explicit version-bump decision.
- **FR-004**: Every endpoint defined in the OpenAPI sketch MUST have a registered handler (verifiable
  without hardware via a route-table contract check).

**Read endpoints**

- **FR-005**: The system MUST expose a status endpoint returning mode (manual/automatic), WiFi state,
  time-sync status, uptime, reset reason, firmware version, and storage usage.
- **FR-006**: The system MUST expose sensor readings (environmental + soil) with per-section validity/
  success flags and an epoch timestamp, serving CACHED task-read values and NEVER performing a blocking
  field-bus read in the request handler.
- **FR-007**: On rev2 the system MUST expose the watering-pump power (INA226) channel; on rev1 that data
  MUST be represented as null/absent (board-capability driven).
- **FR-008**: The system MUST expose sensor history queryable by metric + reading + a time range (named
  ranges and/or explicit start/end), returning aligned timestamps and values and an empty series (not an
  error) when no data matches.

**Control & configuration**

- **FR-009**: The system MUST expose the set of pumps the board actually has (capability-enumerated:
  rev1 plant + reservoir; rev2 plant only) and MUST NOT assume a fixed count.
- **FR-010**: The system MUST support start / stop / timed-run per pump via JSON, enforcing the hard
  maximum runtime cap, with defined idempotent semantics and explicit success/error responses.
- **FR-011**: The system MUST support reading and updating configuration (thresholds, intervals,
  durations); updates MUST be validated (out-of-range/malformed rejected with a 4xx error body and no
  change) and persisted.
- **FR-012**: The system MUST support switching mode (manual/automatic) as a stored, status-reflected flag.
  (The automatic watering behavior behind the flag is out of scope — PR-11.)

**Diagnostics & maintenance**

- **FR-013**: The system MUST expose retrieval of the persistent event log (newest-first, with timestamp,
  category, detail) and a sensor/RS485 self-test trigger returning a structured result.
- **FR-014**: The system MUST expose an OTA-trigger endpoint whose contract is defined here as a stub;
  actual OTA execution is out of scope (PR-13).

**Safety & isolation**

- **FR-015**: HTTP handlers MUST call only into the actuator/sensor/config/storage interfaces and MUST make
  no watering decisions. All cross-task access to shared devices MUST go through the existing per-device
  locks. Invalid input MUST NOT crash or hang the server.

### Key Entities *(include if feature involves data)*

- **API request/response envelope**: the consistent JSON success/error shape (e.g. a success flag, an
  optional message/error, and a payload) used across all endpoints.
- **System status**: mode, network, time-sync, uptime, reset reason, firmware version, storage usage.
- **Sensor reading snapshot**: cached environmental + soil values with validity flags + epoch timestamp;
  rev2 adds a pump-power reading.
- **Pump descriptor + command**: the enumerated pump (name/capability) and a start/stop/timed-run command
  with duration, bounded by the max-runtime cap.
- **Configuration item set**: the validated, persisted thresholds/intervals/durations.
- **History query + series**: metric + reading + range → timestamps[] + values[].
- **Event-log entry**: timestamp, category, detail (from PR-06/PR-08).
- **OpenAPI contract document**: the frozen `docs/api/` sketch that is itself a deliverable.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A user/frontend can retrieve the device's full status and latest sensor readings as valid
  JSON over the network, and the read stays responsive even while a sensor/RS485 transaction is in flight.
- **SC-002**: A user can start, timed-run (auto-stopping within the hard cap), and stop each board-present
  pump over the API, with success/error clearly reported.
- **SC-003**: Submitting an out-of-range or malformed request returns a 4xx with an error body and changes
  nothing; the server never crashes or hangs on bad input.
- **SC-004**: Every endpoint in the frozen OpenAPI sketch has a working handler (verified without hardware),
  and JSON serialization/deserialization + config validation pass host tests.
- **SC-005**: A WiFi/HTTP client that stalls or floods the server causes measurably zero disruption to
  watering timing or sensor polling.
- **SC-006**: History and event-log queries return correct data (or an empty series/list) for any valid
  range, and the self-test + OTA-trigger endpoints answer per contract.
- **SC-007**: Both firmware board targets build with the API included, and the OpenAPI contract is reviewed
  and marked frozen at merge.

## Assumptions

- **No authentication in v1**: the API is unauthenticated on the trusted home LAN, matching legacy behavior
  (no credentials/tokens). If auth is ever required it is a separate, versioned contract change. (The
  provisioning AP password from PR-07 is unrelated — it guards the setup AP, not this station-mode API.)
- **Contract is behavioral coverage, not URL parity**: the new `/api/v1/` shape supersedes the legacy
  duplicated `/api/...` + prefix-less routes; the legacy frontend is adapted to it later (FR8/PR-10). The
  legacy endpoints in parity §4 define the *behaviors* that must be covered, not the exact URLs.
- **Two separate HTTP servers**: the PR-07 provisioning portal is a minimal server that runs only in AP/
  provisioning mode; this API server runs in station mode. They do not run simultaneously (mode-exclusive).
- **Mode/automatic scope**: the mode switch stores and reports a flag; until PR-11 delivers the automatic
  controller, the control endpoints operate pumps/config directly and "automatic" does not itself water.
- **JSON payload sizes are small/bounded**: status/reading/config payloads are small; history responses are
  bounded by the stored retention and the query range (the storage layer already bounds history).
- **Cached sensor values come from the existing sensor task**: the read endpoints surface the values the
  periodic sensor task already maintains (via the locked sensor wrappers), never a fresh in-handler read.
- **CORS/content-type niceties** are applied as needed for a browser frontend but are not gold-plated
  beyond what the frontend (PR-10) requires.

### Dependencies

- **PR-06 (storage)** — `IConfigStore` for config get/set, `IDataStorage` for history + event-log reads.
- **PR-07 (network)** — station-mode connectivity and the WiFi status surfaced by the status endpoint;
  the separate provisioning portal precedent for a standalone HTTP server.
- **PR-03/04/05 (sensors, merged)** — cached environmental/soil/level/INA226 readings via the locked
  sensor wrappers.
- **PR-08 (time + event log, merged)** — epoch timestamps / time-sync status and the event-log surface.
- **Actuator layer** — the pump interfaces (capability-enumerated, hard max-runtime cap) the control
  endpoints call.

### Out of Scope

- Serving the frontend static assets (PR-10) and any frontend modernization (separate PRD).
- The automatic-mode watering logic behind the mode switch (PR-11).
- OTA execution (PR-13) — only the trigger endpoint's stub contract is in scope here.
- Authentication/authorization (not in v1; a future versioned change if ever needed).
