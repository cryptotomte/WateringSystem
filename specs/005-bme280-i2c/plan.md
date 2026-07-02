# Implementation Plan: BME280 Environmental Sensor over I2C

**Branch**: `005-bme280-i2c` | **Date**: 2026-07-02 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/005-bme280-i2c/spec.md`

## Summary

Port the BME280 environmental sensor from the Adafruit library to a self-contained
driver on ESP-IDF's new `i2c_master` API. All policy — 0x76/0x77 probing, chip-ID
verification, calibration parsing, the exact legacy sampling profile (NORMAL,
T×2/P×16/H×1, IIR ×16, standby 500 ms — CP1 decision), Bosch datasheet compensation
math, NaN-fails-read, lazy re-init — lives in a pure C++ `Bme280Sensor` behind a new
minimal `II2cBus` seam, host-tested against Bosch reference vectors with a
`MockI2cBus`. The only hardware-touching class is `EspI2cBus`, which owns the single
shared bus handle (board pins, 100 kHz) that PR-05's INA226 will reuse. A dedicated
5 s sensor task (CP1 decision: env-only) publishes readings through
`LockedEnvironmentalSensor`; the console gains an `env` command for HIL. No registry
component: `espressif/bme280` was rejected on verified grounds (floating dependency,
bus-ownership conflict, un-host-testable compensation) — see [research.md](research.md) R1.

## Technical Context

**Language/Version**: C++ (modern, RAII, no Arduino layers) on ESP-IDF v6.0.1
(pinned docker image `espressif/idf:v6.0.1`)

**Primary Dependencies**: ESP-IDF `esp_driver_i2c` (new `driver/i2c_master.h` API —
the deprecated legacy `driver/i2c.h` is never included), esp_console (existing
REPL). **No new managed components** — the Bosch compensation math is implemented
in-repo (research R1); `dependencies.lock` stays unchanged.

**Storage**: N/A (readings are RAM-only; history/logging is PR-06/PR-09 territory)

**Testing**: host tests on IDF linux preview target (`firmware/test_apps/host`,
exit code = number of failures), CI via esp-idf-ci-action with `target: linux`
(known pitfall: default IDF_TARGET aborts `set-target linux`); HIL checklist on the
rev1 bench rig at Checkpoint 3

**Target Platform**: ESP32-WROOM-32E (rev1 devkit rig + rev2 custom PCB), dual
board targets via Kconfig `BOARD_REV1_DEVKIT`/`BOARD_REV2` (same I2C pins 21/22 on
both, rev2 provisional until SYNC1)

**Project Type**: ESP-IDF component extension within existing `firmware/` project

**Performance Goals**: one burst read (0xF7–0xFE, 8 bytes + register pointer) at
100 kHz ≪ the 5 s poll cadence; no busy-waiting; finite bus timeouts everywhere

**Constraints**: parity contract `docs/parity-checklist.md` §5 (pins SDA 21/SCL 22,
sampling profile, °C/%RH/hPa with Pa→hPa, NaN fails read, lazy re-init, graceful
degradation); shared-bus requirement for PR-05 (spec FR-003); pumps/safety layer
untouched; implementation starts only after PR #10 (004-modbus-soil-sensor) merges
— file overlap documented in research R10

**Scale/Scope**: 1 I2C device (2 candidate addresses), 2 new interfaces
(`IEnvironmentalSensor`, `II2cBus`), 1 component extended (`sensors`), 1 new
console command, 1 new FreeRTOS task, 1 new host-test suite

## Constitution Check

*GATE: evaluated pre-Phase 0 and re-checked post-Phase 1 design — PASS (no
violations, no Complexity Tracking entries).*

- **I. Safety First**: PASS — no pump paths touched. Contribution to safety is the
  validity/error contract (FR-001/FR-007) PR-11's fail-safe will consume; note the
  legacy fail-safe gates on the SOIL sensor only, so this PR adds no new safety
  gate — it delivers the validity signal. Sensor task starts after
  `pumps_force_off()` in the established app_main order.
- **II. Host-Testability**: PASS — all policy in pure `Bme280Sensor` behind
  `II2cBus`; compensation math verified against Bosch reference vectors on the
  host; only `EspI2cBus` touches IDF APIs and contains no business logic; mocks
  provided; host suite extended in CI.
- **III. Reproducible Builds**: PASS — no new managed dependencies (own
  compensation code was chosen partly BECAUSE the registry alternative carries a
  floating `i2c_bus: "*"` dependency, research R1); `dependencies.lock` unchanged;
  both board targets built in CI from clean checkout.
- **IV. Frozen Legacy**: PASS — legacy files are read-only porting reference.
- **V. Checkpoint-Gated Workflow**: PASS — CP1 held (2 questions, both answered A:
  parity sampling profile; task now, env-only); this plan stops at CP2;
  implementation via implementer subagent after PR #10 merges; review + CP3 before
  commit/PR.
- **VI. English Outward**: PASS — all artifacts/code/commits in English.
- **Additional constraints**: pins only from the board component; `ESP_LOG*` with
  component tag; include guards `WATERINGSYSTEM_*_H`; no partition changes; no
  non-trivial global constructors (bus + sensor are function-local statics in
  app_main).

## Project Structure

### Documentation (this feature)

```text
specs/005-bme280-i2c/
├── spec.md
├── plan.md              # This file
├── research.md          # Phase 0 — decisions R1–R10
├── data-model.md        # Phase 1 — reading, registers, sampling profile, errors
├── contracts/
│   └── interfaces.md    # Phase 1 — IEnvironmentalSensor, II2cBus, console, task
├── quickstart.md        # Phase 1 — host-test/build/HIL validation guide
├── checklists/
│   └── requirements.md
└── tasks.md             # Phase 2 (/speckit-tasks — not created by /speckit-plan)
```

### Source Code (repository root)

Planned against PR-04's file versions (worktree `004-modbus-soil-sensor`);
implementation rebases on main once PR #10 has merged (research R10).

```text
firmware/
├── components/
│   ├── interfaces/include/interfaces/
│   │   ├── IEnvironmentalSensor.h       # NEW — standalone, soil-sensor style (R5)
│   │   └── II2cBus.h                    # NEW — minimal I2C register seam (R6)
│   ├── sensors/                         # EXTENDED (component exists after PR-04)
│   │   ├── CMakeLists.txt               # + Bme280Sensor.cpp (all targets),
│   │   │                                #   EspI2cBus.cpp (target-only, linux-excluded)
│   │   ├── include/sensors/
│   │   │   ├── Bme280Sensor.h           # pure logic: probe/chip-ID/calib/compensation
│   │   │   ├── EspI2cBus.h              # i2c_master bus owner (shared with PR-05)
│   │   │   ├── LockedEnvironmentalSensor.h  # mutex decorator
│   │   │   └── testing/
│   │   │       ├── MockI2cBus.h         # scripted registers/probe/failures
│   │   │       └── MockEnvironmentalSensor.h  # for consumer tests (PR-11)
│   │   └── src/
│   │       ├── Bme280Sensor.cpp
│   │       └── EspI2cBus.cpp            # target-only
│   └── board/include/board/board.h      # unchanged (I2C pins already defined)
├── main/
│   ├── app_main.cpp                     # wire EspI2cBus + Bme280Sensor + Locked
│   │                                    #   wrapper + sensor task (5 s, env-only)
│   ├── sensor_task.cpp/.h               # NEW — app-level poller (R7)
│   └── diag_console.cpp/.h              # + env command
└── test_apps/host/main/
    ├── test_bme280.cpp                  # NEW — Bosch vectors, probe/chip-ID,
    │                                    #   error paths, recovery, mock consistency
    ├── test_main.cpp                    # + run_bme280_tests()
    └── CMakeLists.txt                   # + test_bme280.cpp
```

**Structure Decision**: extend the `sensors` component (PR-04's home for sensor
drivers) rather than create a new component — BME280 is a sensor driver with the
same architecture split (pure logic / hardware touchpoint / Locked / mocks). The
sensor task is app wiring in `main/` (not a component) because PR-11's controller
is expected to absorb its cadence (R7).

## Complexity Tracking

No constitution violations — table intentionally empty.
