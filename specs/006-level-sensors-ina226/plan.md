# Implementation Plan: Level Sensors, Single-Pump Capability Flag and INA226 Power Telemetry

**Branch**: `006-level-sensors-ina226` | **Date**: 2026-07-02 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/006-level-sensors-ina226/spec.md`

## Summary

Three riders on one PR, per the mini-PRD: (1) XKC-Y26 level sensors behind a new
`ILevelSensor` interface — polarity owned by the board profile (FW-5: rev1 active
HIGH, rev2 active LOW via 2N7002), stability-window debounce (300 ms, deliberate
divergence from legacy's bare reads) and FW-3 settle gating (rev2 500 ms;
rail *control* deferred to PR-14 per CP1 decision A); (2) the
`BOARD_HAS_RESERVOIR_PUMP` capability flag (rev1=1, rev2=0 — single-pump decision)
with compile-time enforcement (reservoir pin undefined on rev2, RS485-DE pattern)
rippling through app_main/diag_console; (3) `Ina226Sensor` raw power telemetry
(bus V / signed current / power) on the shared I2C bus at 0x40, identity-checked,
datasheet-formula scaling from a Kconfig shunt value (default 5 mΩ) — rev2 only,
host-tested everywhere. The PR-03 `II2cBus` seam is extended with a 16-bit write
(one-transaction pointer+2-byte, contract-sanctioned). Decisions: [research.md](research.md).

## Technical Context

**Language/Version**: C++ (~C++23, RAII, no Arduino) on ESP-IDF v6.0.1 (pinned
docker image)

**Primary Dependencies**: none new — GPIO via `esp_driver_gpio` (already used),
I2C via the existing shared `EspI2cBus` (PR-03), esp_console. `dependencies.lock`
unchanged.

**Storage**: N/A (readings RAM-only; API/logging exposure is PR-09)

**Testing**: host tests on the linux preview target (existing harness; exit code =
failures; CI `target: linux`); HIL checklist on the rev1 rig at CP3 (level sensors
+ capability regression; INA226 hardware validation deferred to PR-14)

**Target Platform**: ESP32-WROOM-32E, dual board targets via Kconfig; rev2 pins
provisional until SYNC1

**Project Type**: extension of existing `firmware/` components (interfaces,
sensors, board) + main wiring

**Performance Goals**: level `update()` = one GPIO read at 10 Hz (negligible);
INA226 reads on demand (console) — two 2-byte I2C reads per query at 100 kHz

**Constraints**: parity checklist §3 lines 95–97 (pins, pull-ups, measured
polarity, fail direction); FW-3/FW-5 consumed here; single-pump decision (master
PRD FR4); safety invariant (existing pumps forced OFF at boot) must hold
capability-aware on both boards; implementation starts only after PR #11 merges
(code dependency on II2cBus/EspI2cBus + file overlap — research R10)

**Scale/Scope**: 2 level-sensor instances, 1 INA226 device (0x40; 0x41 reserved),
3 new interfaces (`ILevelSensor`, `IPowerSensor`, +II2cBus extension), 2 console
commands, ~7 board macros, no new tasks (main-loop polling)

## Constitution Check

*GATE: evaluated pre-Phase 0, re-checked post-Phase 1 — PASS (no violations).*

- **I. Safety First**: PASS — pump force-off becomes capability-aware but the
  invariant (every existing pump OFF first) is unchanged and stays host-tested;
  level-sensor validity gating (not-yet-valid ≠ water absent) is designed so
  PR-11's fail-safe cannot mistake a settling sensor for an empty reservoir;
  fail directions per board pinned by host tests (checklist line 97). No
  protection/alarm logic rides on INA226 (out of scope by PRD).
- **II. Host-Testability**: PASS — all policy (debounce, settle, polarity,
  scaling, identity, error paths) in pure classes over injected seams
  (raw-input + ITimeProvider; II2cBus); hardware classes are logic-free
  (GpioLevelSensor raw read; EspI2cBus extension); mocks for PR-11 included.
- **III. Reproducible Builds**: PASS — no new managed dependencies; one new
  Kconfig int; both targets in CI from clean checkout.
- **IV. Frozen Legacy**: PASS — legacy is read-only reference.
- **V. Checkpoint-Gated Workflow**: PASS — CP1 held (FW-3 split → A); stops at
  CP2; implementer subagent after #11 merges; review + CP3 before push.
- **VI. English Outward**: PASS.
- **Additional constraints**: board differences only via board macros/Kconfig
  (single `#if BOARD_HAS_*` sites in wiring code, none in logic); `ESP_LOG*`
  tags; include guards; no partition changes; no non-trivial global constructors.

## Project Structure

### Documentation (this feature)

```text
specs/006-level-sensors-ina226/
├── spec.md
├── plan.md              # This file
├── research.md          # Phase 0 — R1–R10
├── data-model.md        # Phase 1 — states, board table, registers, scaling
├── contracts/
│   └── interfaces.md    # Phase 1 — ILevelSensor, IPowerSensor, II2cBus ext, console
├── quickstart.md        # Phase 1 — validation guide
├── checklists/
│   └── requirements.md
└── tasks.md             # Phase 2 (/speckit-tasks)
```

### Source Code (repository root; against post-#11 file state)

```text
firmware/
├── components/
│   ├── interfaces/include/interfaces/
│   │   ├── ILevelSensor.h               # NEW
│   │   ├── IPowerSensor.h               # NEW
│   │   └── II2cBus.h                    # + writeRegister16 (virtual), readRegister16 (helper)
│   ├── sensors/
│   │   ├── CMakeLists.txt               # + DebouncedLevelSensor.cpp, Ina226Sensor.cpp (all targets),
│   │   │                                #   GpioLevelSensor.cpp (target-only)
│   │   ├── include/sensors/
│   │   │   ├── DebouncedLevelSensor.h   # pure: settle/debounce/polarity state machine
│   │   │   ├── GpioLevelSensor.h        # target-only raw input (pull-up config)
│   │   │   ├── Ina226Sensor.h           # pure: identity/config/cal/scaling
│   │   │   ├── LockedLevelSensor.h      # decorator
│   │   │   ├── LockedPowerSensor.h      # decorator
│   │   │   ├── EspI2cBus.h/.cpp         # + writeRegister16
│   │   │   └── testing/
│   │   │       ├── MockLevelSensor.h    # PR-11 truth-table capable
│   │   │       └── MockI2cBus.h         # + 16-bit write scripting
│   │   └── src/ (…as above)
│   └── board/include/board/board.h      # BOARD_HAS_RESERVOIR_PUMP, level macros,
│                                        #   INA226 addr, guarded sanity checks
├── main/
│   ├── app_main.cpp                     # capability-aware pump wiring; level sensors
│   │                                    #   (10 Hz update); INA226 on shared bus (rev2)
│   ├── diag_console.cpp/.h              # + level, power; pump reservoir gated
│   └── Kconfig.projbuild                # + CONFIG_WS_INA226_SHUNT_MILLIOHM (default 5)
└── test_apps/host/main/
    ├── test_level_sensor.cpp            # NEW — debounce/settle/polarity/fail-direction/mock
    ├── test_ina226.cpp                  # NEW — scaling vectors, identity, errors, 16-bit bus ext
    ├── test_main.cpp / CMakeLists.txt   # registration
```

**Structure Decision**: extend the `sensors` component (established home; one
concern per class, not per component — precedent: soil + BME280 already coexist);
interfaces stay header-only in `interfaces`. No new FreeRTOS task — level polling
rides the existing 10 Hz main loop (R8).

## Complexity Tracking

No constitution violations — intentionally empty.
