---

description: "Task list for HTTP REST/JSON API v1 (PR-09)"
---

# Tasks: HTTP REST/JSON API v1

**Input**: Design documents from `specs/009-http-server-api-v1/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: INCLUDED (Constitution II; CI host suite is the merge gate). Pure serialize/parse/route-table is
test-first. The `ApiServer` esp_http_server plumbing is target-only (excluded from linux) and HIL-verified.

**Organization**: by user story — US1 read API (P1), US2 control+config (P2), US3 history/diagnostics (P3).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different files, no incomplete dependency)
- **[Story]**: US1 / US2 / US3 (setup/foundational/polish unlabeled)
- Paths under repo root; firmware in `firmware/`.

---

## Phase 1: Setup

- [ ] T001 Create the `api` component skeleton: `firmware/components/api/CMakeLists.txt` with the
  `if(${IDF_TARGET} STREQUAL "linux")` guard — linux + target both compile the pure sources
  (`ApiEnvelope.cpp`, `ApiRoutes.cpp`, `ApiSerialize.cpp`, `ApiRequests.cpp`) with
  `REQUIRES interfaces json network time events`; the target `else` branch adds `ApiServer.cpp` with
  `PRIV_REQUIRES esp_http_server esp_netif esp_app_format`. Create `include/api/`.
- [ ] T002 [P] Register the API host suites: create `test_api_serialize.cpp`, `test_api_requests.cpp`,
  `test_api_routes.cpp` (empty-but-linking `run_api_*_tests()`); add to `SRCS` + `api json` to `REQUIRES`
  in `firmware/test_apps/host/main/CMakeLists.txt`; declare + call all three in `test_main.cpp`.

**Checkpoint**: api component + host suites compile empty on both targets + linux.

---

## Phase 2: Foundational (Blocking Prerequisites)

- [ ] T003 `ApiEnvelope` (pure) in `firmware/components/api/include/api/ApiEnvelope.h` + `src/ApiEnvelope.cpp`:
  success/error JSON envelope builders (over cJSON), the 404 "not found" body, and a small status-code enum.
- [ ] T004 [P] `ApiRoutes` (pure) in `include/api/ApiRoutes.h` + `src/ApiRoutes.cpp`: `enum class HandlerId`,
  `enum class HttpMethod`, `struct ApiRoute{path,method,id}`, the static route array (data-model §Route
  table), and `matchRoute(method, path) -> HandlerId|NotFound` (exact match; unknown `/api/*` → NotFound).
- [ ] T005 [P] DTO headers in `include/api/` (plain structs from data-model): SystemStatusDto,
  SensorReadingsDto, PumpDto/PumpCommand, ConfigDto/ConfigSetRequest, HistoryQuery/HistorySeries, EventDto,
  SelfTestResultDto. No IDF includes.
- [ ] T006 `LockedWifiManager` in `firmware/components/network/include/network/LockedWifiManager.h`: thin
  mutex decorator exposing a synchronized `snapshot()` (single-writer wifi task; console+API readers).
  Resolves the `WifiState.h` `TODO(PR-09)`. Update the `WifiState.h` comment.

**Checkpoint**: envelope + route table + DTOs + LockedWifiManager available.

---

## Phase 3: User Story 1 - Read the system state (Priority: P1) 🎯 MVP

**Goal**: `esp_http_server` in station mode serving `/api/v1/status` + `/api/v1/sensors` (+ `/power`) as
non-blocking cached JSON; the server + routing + serialization every other endpoint reuses.

**Independent Test**: host serializer + route tests green; on the rig `GET /status` and `/sensors` return
correct JSON fast, without blocking on the bus.

### Tests for US1 (write first)

- [ ] T007 [P] [US1] Host tests in `test_api_serialize.cpp`: SystemStatusDto + SensorReadingsDto (+power)
  serialize to the documented JSON; valid flags correct; wifi password absent; not-set time renders per
  PR-08; rev1 power = null.
- [ ] T008 [P] [US1] Host tests in `test_api_routes.cpp`: `matchRoute` resolves `/status`,`/sensors`,`/power`
  and returns NotFound for unknown `/api/*`; route set matches the OpenAPI path set (extend as US2/US3 add).

### Implementation for US1

- [ ] T009 [US1] `ApiSerialize` (pure) status + sensors + power serializers in `include/api/ApiSerialize.h`
  + `src/ApiSerialize.cpp` (cJSON; take the plain DTOs).
- [ ] T010 [US1] `ApiServer` (target-only) skeleton in `include/api/ApiServer.h` (opaque `void*` handle) +
  `src/ApiServer.cpp`: `start()`/`stop()` per `contracts/api-server.md` (HTTPD_DEFAULT_CONFIG, raised
  `max_uri_handlers`, register routes, `user_ctx=this`, JSON 404 handler). Register the status/sensors/power
  handlers: read the `Locked*` cached getters + `LockedWifiManager` snapshot + `esp_netif_get_ip_info` +
  time/reset/fw/storage into DTOs, call the pure serializers, send. NON-BLOCKING (no `read()`/
  `isAvailable()`).
- [ ] T011 [US1] Wire into `app_main.cpp` station branch: construct the `ApiServer` static with the
  `Locked*` refs + `LockedWifiManager` + wall clock/sntp + time_provider + app desc; start it on the first
  `WifiState::Connected` (via `SystemObserver`, mirroring SNTP) or after `begin(Station)`. Not
  WDT-subscribed. `pumps_force_off()` stays first. Add `api` to `main/CMakeLists.txt` PRIV_REQUIRES.

**Checkpoint**: server up in STA; status + sensors + power answer; serializer/route tests green.

---

## Phase 4: User Story 2 - Control pumps, mode & config (Priority: P2)

**Goal**: `/api/v1/pumps` (list + command), `/api/v1/config` (get/set, validated), mode via
`wateringEnabled`.

**Independent Test**: host parse/validate tests green; on the rig a timed pump run auto-stops within the
cap; out-of-range config → 400 no-change; valid config persists.

### Tests for US2 (write first)

- [ ] T012 [P] [US2] Host tests in `test_api_requests.cpp`: `parseConfigSet` accept in-range subset; reject
  each out-of-range field (moisture/duration/interval/sensorRead/dataLog) all-or-nothing; `parsePumpCommand`
  accept start/run(1..300)/stop, reject bad action / out-of-range duration.
- [ ] T013 [P] [US2] Host tests in `test_api_serialize.cpp`: PumpDto + ConfigDto serialize correctly (config
  never includes wifi password); extend route tests for `/pumps`,`/pumps/{name}`,`/config`.

### Implementation for US2

- [ ] T014 [US2] `ApiRequests` (pure) in `include/api/ApiRequests.h` + `src/ApiRequests.cpp`:
  `parseConfigSet(json)` and `parsePumpCommand(json)` per data-model (range constants from `IConfigStore.h`;
  all-or-nothing; typed error).
- [ ] T015 [US2] `ApiSerialize`: add PumpDto + ConfigDto serializers.
- [ ] T016 [US2] `ApiServer`: register `GET /pumps` (capability-enumerated via `#if
  BOARD_HAS_RESERVOIR_PUMP`), `POST /pumps/{name}` (parse → `LockedWaterPump::runFor(1..300)`/`stop`,
  idempotent semantics, unknown name → 404), `GET /config` (ConfigDto, no password), `POST /config`
  (parseConfigSet → apply via setters, all-or-nothing → 400 on reject → return new config). Mode reads/writes
  `wateringEnabled`.

**Checkpoint**: pump control + config get/set work; validation host-tested.

---

## Phase 5: User Story 3 - History, diagnostics & maintenance (Priority: P3)

**Goal**: `/api/v1/history`, `/api/v1/events`, `/api/v1/selftest`, `/api/v1/ota` (stub).

**Independent Test**: history/events serialize correctly (host); on the rig history returns a series/empty,
events newest-first, self-test a structured result, OTA the stub.

### Tests for US3 (write first)

- [ ] T017 [P] [US3] Host tests: HistorySeries + EventDto + SelfTestResultDto serializers; `namedRangeToWindow`
  (1h/6h/24h/7d/30d) arithmetic; empty-series serialization; extend route tests for
  `/history`,`/events`,`/selftest`,`/ota`.

### Implementation for US3

- [ ] T018 [US3] `ApiSerialize`/`ApiRequests`: history query parse (metric/reading/range|start-end,
  `namedRangeToWindow`) + HistorySeries, EventDto, SelfTestResultDto serializers.
- [ ] T019 [US3] `ApiServer`: register `GET /history` (`IDataStorage::getSensorReadings`, empty not error),
  `GET /events` (`getEvents(count)` newest-first, bounded count), `POST /selftest` (bounded on-demand
  sensor/RS485 self-test → structured result), `POST /ota` (defined stub response, PR-13).

**Checkpoint**: all endpoints answer; full route set covered.

---

## Phase 6: Polish & Cross-Cutting

- [ ] T020 Author `docs/api/openapi.yaml` — OpenAPI 3 sketch of every endpoint (request/response shapes,
  errors), marked frozen at merge. Ensure the route-table test's path set matches it exactly.
- [ ] T021 Add an "HTTP API (feature 009)" section to `firmware/CLAUDE.md` (api component, pure/target
  split, the /api/v1/ contract + docs/api/, non-blocking-reads rule, mode↔wateringEnabled, soil/power
  staleness note, LockedWifiManager).
- [ ] T022 [P] Confirm `dependencies.lock` + both `esp-modbus` pins unchanged (only IDF built-ins added).
- [ ] T023 `idf.py size` both targets — confirm fit in the 1.5 MiB OTA slot; record margin.
- [ ] T024 Author `specs/009-http-server-api-v1/checklists/hil.md` from quickstart §HIL (scripted curl:
  status/sensors-nonblocking/pumps/config/history/events/selftest/ota/robustness/rev2-power). Note the
  soil/power staleness limitation + deferral path if no rig time.
- [ ] T025 Full host suite + both board builds green (quickstart CI section).

---

## Dependencies & Execution Order

- **Setup (P1)** → **Foundational (P2, blocks all)** → US1 → US2 → US3 → Polish.
- US1/US2/US3 all extend the SAME files (`ApiSerialize.*`, `ApiServer.*`, `ApiRoutes` set,
  `test_api_*`) — sequence the phases; within a phase the [P] test tasks precede impl.
- `ApiServer` (T010) must exist before US2/US3 add handlers (T016/T019).
- `LockedWifiManager` (T006) is needed by the US1 status handler (T010/T011).
- OpenAPI sketch (T020) and the route-table test (T004/T008) must agree — keep them in lockstep as routes
  are added; T020 finalizes the frozen doc.
- Pure/host-tested: T003–T005, T007–T009, T012–T015, T017–T018. Target/HIL: T010, T016, T019 (ApiServer),
  T011 (app_main wiring).

### Parallel opportunities
- T004/T005 (foundational) parallel. Within each story the test tasks are [P]. T022 parallel in polish.

---

## Implementation Strategy

- **MVP = US1** (read API: server + status + sensors) — the server/routing/serialization spine every other
  endpoint reuses, and immediate monitoring value.
- Then US2 (control + config), then US3 (history/diagnostics/OTA-stub).
- Commit after each phase; verify host tests fail before implementing the pure logic.
- Build via Docker on an rsync'd `/tmp/ws009-firmware`; fullclean + rm sdkconfig between board builds.
- Never modify frozen legacy. Keep `pumps_force_off()` first. Handlers non-blocking, no watering decisions,
  never serialize the wifi password.
