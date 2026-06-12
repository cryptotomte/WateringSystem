# Implementation Plan: Pump Actuator Layer and Board Abstraction

**Branch**: `002-pump-gpio-board` | **Date**: 2026-06-10 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/002-pump-gpio-board/spec.md`

## Summary

Port the pump actuator layer to native ESP-IDF and complete the board abstraction.
All timing/safety logic (timed runs, 300 s max-runtime enforcement, runtime
statistics) lives in a pure C++ `WaterPump` base class driven by an injected
monotonic time source and polled `update()`; the only hardware touchpoint is a
virtual `applyOutput(bool)` implemented by `GpioWaterPump` (active-HIGH MOSFET,
explicit OFF at init). Host tests on the IDF linux preview target (bundled Unity,
exit-code gate) exercise the real enforcement logic via `MockWaterPump` +
`FakeTimeProvider`. An `esp_console` REPL provides rig diagnostics. The `board`
component becomes the single source of truth for pins/feature flags on both boards.

## Technical Context

**Language/Version**: C++ (gnu++26 default of ESP-IDF v6.0.1; conservative ~C++23 feature use)

**Primary Dependencies**: ESP-IDF v6.0.1 (pinned, Docker/CI) — components used: `esp_driver_gpio`, `console`, `esp_timer`, `unity` (host tests). No new managed components.

**Storage**: N/A (runtime config arrives with NVS in PR-06; this PR uses constants)

**Testing**: IDF-bundled Unity on the linux preview target (`firmware/test_apps/host/`, native executable, exit code = failure count) + CI matrix build of both boards + HIL checklist on the rev1 rig

**Target Platform**: ESP32-WROOM-32E (rev1 devkit + rev2 custom PCB), host tests on linux (CI container)

**Project Type**: Embedded firmware (ESP-IDF component architecture) + host test app

**Performance Goals**: Timed-run self-stop within 1 s of configured duration (SC-003); `update()` polled at main-loop cadence (≥ 10 Hz ample)

**Constraints**: Pumps off at boot/reset preserved (constitution I); no `esp_timer` calls in host-tested code (not simulated on linux target); no external dependencies (constitution III); frozen Arduino tree untouched (constitution IV)

**Scale/Scope**: 2 pump instances; ~6 new/changed files in `firmware/components/`, 1 host test app, 1 CI job, board.h extension

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Gate | Status |
|---|---|---|
| I Safety First | Pumps off at boot unchanged (`app_main` fail-safe stays first); `GpioWaterPump` re-asserts OFF at init; max-runtime enforced in driver for every run mode; no indefinite runs | PASS |
| II Host-Testability | Enforcement logic in pure base class, tested on host via mock + fake clock; hardware only behind `applyOutput`/interfaces | PASS |
| III Reproducible Builds | No new managed components; Unity/console are IDF-bundled; CI builds both boards + runs host tests from clean checkout | PASS |
| IV Frozen Legacy | No changes under `src/`, `include/`, `data/`, `test/`, `platformio.ini` (Arduino code is read-only reference) | PASS |
| V Checkpoint Workflow | This plan → CP2; implementation via implementer agent; review → CP3 | PASS |
| VI English Outward | All deliverables in English | PASS |

**Post-design re-check (after Phase 1)**: PASS — no violations introduced; Complexity Tracking empty.

## Project Structure

### Documentation (this feature)

```text
specs/002-pump-gpio-board/
├── spec.md
├── plan.md              # This file
├── research.md          # Phase 0 — 6 decisions (host tests, architecture, time, console, constants, mocks)
├── data-model.md        # Phase 1 — pump state machine, entities
├── quickstart.md        # Phase 1 — build/test/HIL validation guide
├── contracts/
│   ├── iwaterpump.md    # C++ interface contract + semantics
│   └── serial-diagnostic.md  # REPL command grammar
├── checklists/requirements.md
└── tasks.md             # Phase 2 (/speckit-tasks — not created by plan)
```

### Source Code (repository root)

```text
firmware/
├── components/
│   ├── interfaces/                      # NEW — header-only, no IDF deps
│   │   ├── CMakeLists.txt
│   │   └── include/interfaces/
│   │       ├── IActuator.h              # ported from Arduino include/actuators/
│   │       ├── IWaterPump.h             # ported, std::string, no Arduino types
│   │       └── ITimeProvider.h          # NEW — monotonic ms clock
│   ├── actuators/                       # NEW
│   │   ├── CMakeLists.txt               # GpioWaterPump.cpp + esp_driver_gpio dep excluded on linux target
│   │   ├── include/actuators/
│   │   │   ├── WaterPump.h              # pure C++ base: all timing/safety logic
│   │   │   ├── GpioWaterPump.h          # applyOutput → gpio_set_level (esp32 only)
│   │   │   ├── EspTimeProvider.h        # esp_timer_get_time()/1000 (esp32 only)
│   │   │   └── testing/
│   │   │       ├── MockWaterPump.h      # header-only, host tests + PR-11
│   │   │       └── FakeTimeProvider.h   # header-only, manual advance
│   │   └── src/
│   │       ├── WaterPump.cpp
│   │       └── GpioWaterPump.cpp
│   └── board/                           # EXTENDED — LED 2, buttons 5/18, rev2 TODO(SYNC1)
│       └── include/board/board.h
├── main/
│   ├── app_main.cpp                     # wire 2 pumps behind IWaterPump, poll update(), start REPL
│   ├── diag_console.cpp                 # NEW — esp_console: pump <plant|reservoir> <start s|stop|status>
│   └── CMakeLists.txt                   # + console dep
└── test_apps/
    └── host/                            # NEW — IDF project, linux preview target
        ├── CMakeLists.txt               # set(COMPONENTS main)
        ├── sdkconfig.defaults
        └── main/
            ├── CMakeLists.txt           # REQUIRES unity, actuators, interfaces
            └── test_water_pump.cpp      # Unity runner: UNITY_BEGIN/RUN_TEST/exit(UNITY_END())

.github/workflows/firmware-build.yml     # + host-test job (set-target linux, build, run elf)
```

**Structure Decision**: Two new components keep interfaces (reused by PR-03..05,
PR-11) decoupled from actuator implementation; host tests are a separate IDF
project so the esp32 `sdkconfig` is never polluted by the linux target
(`set(COMPONENTS main)` isolation). Frozen Arduino tree untouched.

## Complexity Tracking

No constitution violations — table intentionally empty.
