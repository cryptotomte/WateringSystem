# Implementation Plan: Modbus Soil Sensor over RS485

**Branch**: `004-modbus-soil-sensor` | **Date**: 2026-07-02 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/004-modbus-soil-sensor/spec.md`

## Summary

Replace the legacy hand-rolled SP3485 Modbus client with esp-modbus 2.1.2 behind
ported `IModbusClient`/`ISoilSensor` interfaces. Decode/validation/calibration logic
lives in a pure-C++ `ModbusSoilSensor` (host-tested); the only hardware-touching
class is `EspModbusClient`, which configures the board-selected UART in RS485
half-duplex mode — rev1 drives DE via hardware RTS (GPIO 25), rev2 configures no
direction pin, gets its TX echo suppressed by the half-duplex receive gating (FW-4)
and its RX pin pulled up against a floating transceiver output (FW-2). Console
gains `soil`, `rs485test` and calibration commands for HIL. Design decisions and
their rationale: [research.md](research.md).

## Technical Context

**Language/Version**: C++ (modern, RAII, no Arduino layers) on ESP-IDF v6.0.1
(pinned docker image `espressif/idf:v6.0.1`)

**Primary Dependencies**: `espressif/esp-modbus==2.1.2` (new pin, added to the
`sensors` component's `idf_component.yml`; `dependencies.lock` updated
deliberately), ESP-IDF UART/GPIO drivers, esp_console (existing REPL)

**Storage**: N/A (calibration factors RAM-only this PR; persistence → PR-09/PR-11)

**Testing**: host tests on IDF linux preview target (`firmware/test_apps/host`,
exit code = number of failures), CI via esp-idf-ci-action with `target: linux`
(known pitfall: default IDF_TARGET aborts `set-target linux`); HIL checklist on the
rev1 bench rig at Checkpoint 3

**Target Platform**: ESP32-WROOM-32E (rev1 devkit rig + rev2 custom PCB), dual
board targets via Kconfig `BOARD_REV1_DEVKIT`/`BOARD_REV2`

**Project Type**: ESP-IDF component set within existing `firmware/` project

**Performance Goals**: 9600 baud 8N1 bus; one 9-register transaction well under the
5 s legacy read cadence; no busy-waiting in the driver

**Constraints**: parity contract `docs/parity-checklist.md` §5 (register map,
scaling, ranges, 3000 ms timeout, NO retry, real-read availability probe,
statistics); rev2 hardware requirements FW-2/FW-4 (spec FR-008/FR-009); pumps/
safety layer untouched by this PR

**Scale/Scope**: 1 slave device (addr 0x01), 9 holding registers + 3 calibration
registers; ~2 new interfaces, 1 new component, 3–5 console commands

## Constitution Check

*GATE: evaluated pre-Phase 0 and re-checked post-Phase 1 design — PASS (no
violations, no Complexity Tracking entries).*

- **I. Safety First**: PASS — no pump paths touched. The driver's contribution to
  safety is the validity/error contract (FR-005) that PR-11's fail-safe consumes;
  invalid-on-timeout and range-validation failure are host-tested here.
- **II. Host-Testability**: PASS — all decode/validation/calibration logic in pure
  `ModbusSoilSensor` behind `IModbusClient`; only `EspModbusClient` touches IDF
  APIs and contains no business logic; mocks provided; host suite extended in CI.
  Echo suppression sits below the interface boundary (UART hardware mode) — argued
  in research.md R3, HIL-verified rather than host-tested.
- **III. Reproducible Builds**: PASS — esp-modbus exactly `==2.1.2`,
  `dependencies.lock` committed, both board targets built in CI from clean checkout.
- **IV. Frozen Legacy**: PASS — legacy files are read-only porting reference; no
  modification.
- **V. Checkpoint-Gated Workflow**: PASS — CP1 held (calibration scope, answered A);
  this plan stops at CP2; implementation via implementer subagent; review + CP3
  before commit/PR.
- **VI. English Outward**: PASS — all artifacts/code/commits in English.
- **Additional constraints**: board differences only via `board` component flags
  (single `#if BOARD_HAS_RS485_DE` site in `EspModbusClient`); `ESP_LOG*` with
  component tag; include guards `WATERINGSYSTEM_*_H`; no partition changes.

## Project Structure

### Documentation (this feature)

```text
specs/004-modbus-soil-sensor/
├── spec.md
├── plan.md              # This file
├── research.md          # Phase 0 — 9 resolved decisions (R1–R9)
├── data-model.md        # Phase 1 — SoilReading, error codes, calibration, stats
├── contracts/
│   └── interfaces.md    # Phase 1 — IModbusClient, ISoilSensor, console contract
├── quickstart.md        # Phase 1 — host-test/build/HIL validation guide
├── checklists/
│   └── requirements.md
└── tasks.md             # Phase 2 (/speckit-tasks — not created by /speckit-plan)
```

### Source Code (repository root)

```text
firmware/
├── components/
│   ├── interfaces/include/interfaces/
│   │   ├── IModbusClient.h              # NEW — ported, pure C++
│   │   └── ISoilSensor.h                # NEW — ported, trimmed (see contracts)
│   ├── sensors/                         # NEW component (PR-02 actuators pattern)
│   │   ├── CMakeLists.txt               # excludes EspModbusClient on linux target
│   │   ├── idf_component.yml            # espressif/esp-modbus==2.1.2
│   │   ├── include/sensors/
│   │   │   ├── ModbusSoilSensor.h       # pure logic: decode/validate/calibrate
│   │   │   ├── LockedSoilSensor.h       # mutex decorator (REPL vs main loop)
│   │   │   ├── EspModbusClient.h        # esp-modbus + UART/RS485 + pull-up
│   │   │   └── testing/
│   │   │       ├── MockModbusClient.h   # scripted responses/timeouts/exceptions
│   │   │       └── MockSoilSensor.h     # for PR-11 consumers
│   │   └── src/
│   │       ├── ModbusSoilSensor.cpp
│   │       └── EspModbusClient.cpp      # target-only
│   └── board/include/board/board.h      # + BOARD_RS485_UART_PORT in both profiles (analyze I1)
├── main/
│   ├── app_main.cpp                     # wire EspModbusClient + LockedSoilSensor
│   └── diag_console.cpp                 # + soil, rs485test, soil_cal_* commands
└── test_apps/host/main/
    ├── test_soil_sensor.cpp             # NEW — decode/validate/timeout/calibration
    └── CMakeLists.txt                   # register new test file
```

**Structure Decision**: one new `sensors` component following the `actuators`
component layout (pure base + hardware class + `Locked*` decorator + `testing/`
mocks); interface headers join the existing `interfaces` component. `EspModbusClient`
is CMake-excluded from the linux/host target the same way `storage` excludes
esp_littlefs (research R7).

## Key Design Decisions (from research.md)

| # | Decision | Spec FR |
|---|---|---|
| R1 | esp-modbus 2.x handle API, `mbc_master_send_request` raw requests — no CID dictionary | FR-002 |
| R2 | `UART_MODE_RS485_HALF_DUPLEX` on both boards; RTS = DE pin on rev1, no RTS on rev2; HIL verifies TXS0108E margins (fallback: manual GPIO DE) | FR-007 |
| R3 | rev2 echo suppressed by half-duplex RX gating; no app-level scrubber; PR-14 HIL verifies | FR-008 |
| R4 | Unconditional internal pull-up on RX pin in client init | FR-009 |
| R5 | `response_tout_ms = 3000`; esp-modbus event-driven receive ≈ legacy "extend while arriving"; strictly no retry | FR-006 |
| R6 | esp_err_t → legacy-shaped error codes; 100+n exception range when surfaced, else documented divergence | FR-010 |
| R7 | Component layout (above); decode/validation pure & host-tested | FR-001/014 |
| R8 | Calibration legacy-exact (CP1 answer A), RAM-only factors | FR-012 |
| R9 | Console: `soil`, `rs485test`, `soil_cal_*` via LockedSoilSensor | FR-013 |

## Risks & Open Items (carried to tasks/HIL)

1. **RTS timing vs TXS0108E margins (R2)** — HIL item on the rig; documented
   fallback path that stays behind `IModbusClient`.
2. **esp-modbus runtime timeout setter availability (R5)** — implementer verifies
   against pinned 2.1.2; degrade `setTimeout` to init-time-only with doc note.
3. **Exception-code granularity (R6)** — possible parity divergence (100+n → single
   exception code); record in PR description like PR-06's divergences if hit.
4. **Kconfig timeout clamp (R5)** — assert `CONFIG_FMB_MASTER_TIMEOUT_MS_RESPOND`
   allows 3000 in both board sdkconfigs; CI-visible if violated.
5. **rev2 echo behavior unverifiable until PR-14** — accepted per spec assumption;
   T3.5 resync is the fallback layer.

## Agent context update

Skipped deliberately: root `CLAUDE.md` carries no `<!-- SPECKIT -->` markers in this
repo and unprompted root-CLAUDE.md edits are blocked by policy;
`.specify/feature.json` (already pointing at `specs/004-modbus-soil-sensor`) is what
downstream spec-kit commands use.

## Complexity Tracking

No constitution violations — table intentionally empty.
