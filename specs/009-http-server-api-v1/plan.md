# Implementation Plan: HTTP REST/JSON API v1

**Branch**: `009-http-server-api-v1` | **Date**: 2026-07-05 | **Spec**: [spec.md](./spec.md)

**Input**: `specs/009-http-server-api-v1/spec.md`; PR brief `docs/prd/PR-09-http-server-api-v1.md`;
parity `docs/parity-checklist.md` §4 (route behaviors) + QUIRK 5 (non-blocking reads).

## Summary

Add a versioned `/api/v1/` REST/JSON API served by a dedicated `esp_http_server` instance in station mode.
The design keeps all JSON serialization, request parsing/validation, and the route table in a **pure `api`
component** (host-tested with cJSON on the linux target), with a thin target-only `ApiServer` that wires
`esp_http_server` to those pure functions and calls the existing `Locked*` interfaces. Handlers are
non-blocking and use only cached getters (QUIRK 5); they make no watering decisions (Constitution I). A
frozen OpenAPI 3 sketch under `docs/api/` is a deliverable and the input to a route-table contract test. A
`LockedWifiManager` decorator (booked `TODO(PR-09)`) is added for the new cross-task status reader.

## Technical Context

**Language/Version**: C++17 on ESP-IDF v6.0.1 (Docker `espressif/idf:v6.0.1`), target esp32.

**Primary Dependencies**: IDF built-ins only — `esp_http_server`, `json` (cJSON), `esp_netif` (device IP),
`esp_app_format` (version). Reuses PR-06 `IConfigStore`/`IDataStorage`, PR-07 `WifiManager`, PR-08
`IWallClock`/`SyncStatus`/`resetReasonName`, the sensor `Locked*` wrappers, and the actuator
`LockedWaterPump`. No new managed components → `dependencies.lock` untouched.

**Storage**: reads config via `IConfigStore`; history + events + stats via `IDataStorage` (both through
their `Locked*` wrappers). No new persisted state (mode maps onto the existing `wateringEnabled` flag).

**Testing**: Unity host suite. cJSON links on the linux preview target, so JSON serialize/parse/validate +
the route-table contract check are host-tested; `esp_http_server` has no linux port, so the `ApiServer`
plumbing is target-only (excluded from the linux build) and HIL-verified via a scripted curl checklist.

**Target Platform**: ESP32-WROOM-32E, board targets `BOARD_REV1_DEVKIT` (plant + reservoir pumps) and
`BOARD_REV2` (plant pump only + INA226 power).

**Project Type**: Embedded firmware component + `main/` wiring.

**Performance Goals**: handlers non-blocking (never call `read()`/`isAvailable()`); a slow/stalled/flooding
client causes zero disruption to the 10 Hz watering loop or the sensor task (FR-015 / SC-005). Payloads are
small; history bounded by the storage retention + query range.

**Constraints**: fits the 1.5 MiB OTA slot (~470 KiB headroom today; add an `idf.py size` CI check);
handlers touch shared devices only through `Locked*`; never serialize `getWifiPassword()`; both boards build.

**Scale/Scope**: one `api` component (pure serializers/parsers/route-table + target `ApiServer`), one
`LockedWifiManager`, the `docs/api/` OpenAPI sketch, app_main station-branch wiring, one host suite. The
route set spans ~13 endpoints across status/readings/history/pumps/config/power/events/self-test/OTA-stub.

## Constitution Check

*GATE: evaluated before Phase 0 and re-checked after Phase 1. Result: PASS (no violations).*

- **I. Safety First (NON-NEGOTIABLE)** — PASS. HTTP handlers call ONLY the actuator/sensor/config/storage
  interfaces through their `Locked*` wrappers and make **no watering decisions**; pump actuation goes
  through `IWaterPump::runFor` (hard 300 s cap enforced internally). The API runs on the `esp_http_server`
  task(s), **not** WDT-subscribed and sharing no mutex with the watering loop beyond the `Locked*` wrappers
  — a slow/stalled client cannot stall watering (FR-015). `pumps_force_off()` stays the first `app_main`
  action, untouched. Reads use only non-blocking cached getters (QUIRK 5).
- **II. Host-Testability** — PASS. All JSON serialization, request parse/validation, and the route table
  are pure functions in the `api` component (cJSON links on linux) and host-tested; the `esp_http_server`
  plumbing (`ApiServer`) is target-only, excluded from the linux build via the
  `if(${IDF_TARGET} STREQUAL "linux")` guard.
- **III. Reproducible Builds** — PASS. Only IDF built-in components (`esp_http_server`, `json`,
  `esp_netif`); no new managed deps; `dependencies.lock` + both `esp-modbus` pins untouched. Both targets
  build; add an `idf.py size` slot check.
- **IV. Frozen Legacy** — PASS. No change under `src/`/`include/`/`data/`/`test/`/`platformio.ini`.
- **V. Checkpoint-Gated Workflow** — PASS. Implementation delegated to the `implementer` after CP2.
- **VI. English Outward** — PASS. Code, comments, and the OpenAPI doc are English.

**Deferred-scope notes (recorded so they don't surface as review findings):** the **mode switch maps onto
the existing `wateringEnabled` flag** (no automatic watering logic — PR-11 consumes the flag); **soil &amp;
power readings are served as last-good with a `valid` flag** because they have no periodic refresher yet
(PR-11 adds the soil reader) — env (5 s) and level (10 Hz) are fresh; the **OTA-trigger endpoint is a
contract stub** (PR-13); **no authentication in v1** (LAN device, parity — a future versioned change).

## Project Structure

### Documentation (this feature)

```text
specs/009-http-server-api-v1/
├── plan.md · research.md · data-model.md · quickstart.md
├── contracts/{api-envelope-and-routes.md, endpoint-contracts.md, api-server.md}
├── checklists/requirements.md
└── tasks.md            # /speckit-tasks output (not created here)
docs/api/openapi.yaml   # NEW — frozen OpenAPI 3 sketch (a deliverable; route-table test asserts parity)
```

### Source Code (repository root)

```text
firmware/
├── components/
│   ├── api/                          # NEW component
│   │   ├── CMakeLists.txt            # linux-guarded: pure sources on host (REQUIRES interfaces json
│   │   │                             #   network time events); + ApiServer.cpp on target (PRIV_REQUIRES
│   │   │                             #   esp_http_server esp_netif esp_app_format)
│   │   ├── include/api/
│   │   │   ├── ApiRoutes.h            # pure: route table {path, method, HandlerId} (data structure)
│   │   │   ├── ApiSerialize.h         # pure: status/readings/history/pumps/config/events/power -> JSON
│   │   │   ├── ApiRequests.h          # pure: parse+validate config-set / pump-command JSON bodies
│   │   │   ├── ApiEnvelope.h          # pure: success/error JSON envelope + 404/4xx bodies
│   │   │   └── ApiServer.h            # thin target-only server (opaque void* handle)
│   │   └── src/{ApiSerialize.cpp, ApiRequests.cpp, ApiRoutes.cpp, ApiEnvelope.cpp,  # pure (host+target)
│   │            ApiServer.cpp}        # target-only (esp_http_server)
│   └── network/include/network/
│       └── LockedWifiManager.h        # NEW — cross-task snapshot decorator (resolves WifiState.h TODO(PR-09))
├── main/
│   ├── app_main.cpp                  # EDIT — station branch: construct ApiServer with the Locked* refs +
│   │                                 #   wall_clock/sntp/wifi + app desc; start on first Connected
│   ├── diag_console.cpp              # (reference only — mirror its accessor calls; not required to edit)
│   └── CMakeLists.txt                # EDIT — PRIV_REQUIRES api
└── test_apps/host/main/
    ├── test_api_serialize.cpp        # NEW — serializer golden/round-trip tests
    ├── test_api_requests.cpp         # NEW — config/pump body parse+validate accept/reject
    ├── test_api_routes.cpp           # NEW — route-table contract check vs the OpenAPI sketch paths
    ├── test_main.cpp · CMakeLists.txt  # EDIT — register run_api_*_tests(); REQUIRES api json
```

**Structure Decision**: A new `api` component holds the API, split exactly like the sensor/network
components: **pure** serialize/parse/route-table sources compiled on host + target (depending only on the
project interfaces + cJSON), and a **target-only** `ApiServer` (the sole `esp_http_server` touchpoint,
opaque `void*` handle in its header per the `ProvisioningPortal` precedent) excluded from the linux build.
The pure layer takes plain snapshot structs (the thin handler reads the `Locked*` interfaces into those
structs), so serialization is deterministic and host-tested without any IDF HTTP dependency. The API server
is a **separate `esp_http_server` instance** started in the station branch (never simultaneous with the
AP-only provisioning portal). `LockedWifiManager` lands in the `network` component to give the status
handler a synchronized snapshot read.

## Complexity Tracking

> No constitution violations. Table intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| —         | —          | —                                    |
