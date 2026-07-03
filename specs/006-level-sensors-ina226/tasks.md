# Tasks: Level Sensors, Single-Pump Capability Flag and INA226 Power Telemetry

**Input**: Design documents from `specs/006-level-sensors-ina226/`
**Prerequisites**: plan.md, research.md (R1–R10), data-model.md, contracts/interfaces.md, quickstart.md

**Tests**: host tests explicitly required (SC-002, Constitution II) — in scope,
existing harness conventions.

**Organization**: grouped by user story (US1–US4 from spec.md). Hard gate
(research R10): implementation starts only after **PR #11 (005-bme280-i2c) has
merged** — this feature extends its `II2cBus`/`EspI2cBus`/`MockI2cBus` and shares
app_main/diag_console/test-harness files.

## Phase 1: Setup

- [ ] T001 Rebase branch `006-level-sensors-ina226` onto origin/main AFTER PR #11 merges; verify `interfaces/II2cBus.h`, `sensors/EspI2cBus.*`, `sensors/testing/MockI2cBus.h` and the `env` console command exist; verify host suite + both board targets build green from clean checkout (baseline)

## Phase 2: Foundational

- [x] T002 [P] Create `ILevelSensor` in `firmware/components/interfaces/include/interfaces/ILevelSensor.h` — update/isValid/isWaterPresent/rawState/notifyPowerOn, contract doc comments per contracts/interfaces.md (validity gating, polarity absorption, fail-direction notes)
- [x] T003 [P] Create `IPowerSensor` in `firmware/components/interfaces/include/interfaces/IPowerSensor.h` — established sensor validity family (initialize/read/isAvailable/getLastError 0/1/2, NaN-before-first-success getters: busVoltage/current/power)
- [x] T004 Extend `II2cBus` in `firmware/components/interfaces/include/interfaces/II2cBus.h`: new virtual `writeRegister16(addr7, reg, uint16_t)` (big-endian, ONE transaction, doc: required by INA226 config/cal writes) + non-virtual `readRegister16` helper over readRegisters; update the PR-05 delegation note to point at this resolution
- [x] T005 Implement `writeRegister16` in `firmware/components/sensors/src/EspI2cBus.cpp` + `include/sensors/EspI2cBus.h` (3-byte i2c_master_transmit, same lock/log discipline as existing methods)
- [x] T006 Extend `MockI2cBus` in `firmware/components/sensors/include/sensors/testing/MockI2cBus.h`: 16-bit writes recorded big-endian into the per-address byte map + call log; scripting outcomes for 16-bit writes; host test for the extension in `firmware/test_apps/host/main/test_ina226.cpp` (mock-level roundtrip)
- [x] T007 [P] Board flags in `firmware/components/board/include/board/board.h` per data-model.md table: `BOARD_HAS_RESERVOIR_PUMP` (rev1=1; rev2=0 + `BOARD_PIN_RESERVOIR_PUMP` REMOVED), `BOARD_PIN_LEVEL_LOW/HIGH` (32/33 both), `BOARD_LEVEL_ACTIVE_LOW` (0/1), `BOARD_LEVEL_DEBOUNCE_MS` (300), `BOARD_LEVEL_SETTLE_MS` (0/500), `BOARD_INA226_ADDR` (0x40 rev2 + address-map comment); flag-guard existing reservoir-pin sanity checks, add flag⇒pin consistency assert + level-pin distinctness asserts

## Phase 3: User Story 1 — Trustworthy water-level status (P1) 🎯 MVP

- [x] T008 [US1] Implement `DebouncedLevelSensor` (pure) in `firmware/components/sensors/include/sensors/DebouncedLevelSensor.h` + `src/DebouncedLevelSensor.cpp`: injected raw-input source + ITimeProvider; settle gating (notifyPowerOn per FW-3, CP1 decision A) → warm-up → tracking with stability-window debounce (flip restarts window); polarity from constructor parameter (board macro at wiring site); state machine per data-model.md
- [x] T009 [P] [US1] Host tests in `firmware/test_apps/host/main/test_level_sensor.cpp` (new suite `run_level_sensor_tests()`): debounce boundary (change only after window; flip restarts), warm-up not-yet-valid, settle gating (500 ms rev2 case incl. notifyPowerOn re-arm), polarity equivalence (same scenario both polarities → identical logical result), chatter → single transition
- [x] T010 [P] [US1] Implement `GpioLevelSensor` raw-input provider (target-only) in `firmware/components/sensors/include/sensors/GpioLevelSensor.h` + `src/GpioLevelSensor.cpp`: input + internal pull-up (both boards, R4), no logic; CMakeLists linux-exclusion
- [x] T011 [P] [US1] Implement `LockedLevelSensor` in `firmware/components/sensors/include/sensors/LockedLevelSensor.h` (per-call mutex decorator, established pattern + cross-call limitation note)
- [x] T012 [US1] `level` console command: `diag_console_register_level(ILevelSensor &low, ILevelSensor &high)` in `firmware/main/diag_console.h/.cpp` — thin wrapper; output distinguishes not-yet-valid from wet/dry, shows logical + raw per sensor
- [x] T013 [US1] Wire level sensors in `firmware/main/app_main.cpp`: two GpioLevelSensor + DebouncedLevelSensor (+Locked wrappers), polarity/settle from board macros, `notifyPowerOn()` at boot, `update()` calls in the 10 Hz main loop, console registration before start

## Phase 4: User Story 2 — One firmware, one-pump and two-pump boards (P2)

  > NOTE (implementer, Mission 1): T007 removed `BOARD_PIN_RESERVOIR_PUMP` from
  > the rev2 board profile, so the REV2 TARGET BUILD IS EXPECTED RED until T014
  > guards app_main's unconditional references (task-order artifact of the
  > Mission 1/2 split). Host suite and rev1 target are unaffected.
- [ ] T014 [US2] Capability-gate reservoir pump wiring in `firmware/main/app_main.cpp`: instance creation, boot force-OFF and any references under `#if BOARD_HAS_RESERVOIR_PUMP`; verify the force-off-first invariant reads correctly on both boards (rev2 forces off exactly the plant pump)
- [ ] T015 [US2] Gate `pump reservoir` console registration in `firmware/main/diag_console.cpp` (compile-time absence on flag=0; `pump status` reports exactly the existing pumps)
- [ ] T016 [P] [US2] Host tests for capability gating in `firmware/test_apps/host/main/test_level_sensor.cpp` or existing pump suite: whatever pump-wiring logic is host-reachable stays green on both configs; at minimum assert the board-header contract compiles both ways (a compile-time test: rev1 path references the pin, shared code does not) — plus regression: full existing pump suite untouched (SC-004)

## Phase 5: User Story 3 — Pump power telemetry on rev2 (P3)

- [ ] T017 [US3] Implement `Ina226Sensor` (pure) in `firmware/components/sensors/include/sensors/Ina226Sensor.h` + `src/Ina226Sensor.cpp`: identity check (0xFE==0x5449, 0xFF==0x2260), config + calibration writes (verify register field values against TI datasheet SBOS547 — flagged in research R7), CAL = 0.00512/(CurrentLSB×Rshunt) from constructor shunt param, scaling (busV ×1.25 mV; current signed ×0.5 mA; power ×25×CurrentLSB), NaN placeholders, errors 0/1/2, lazy re-init, uninitialize-on-bus-error. CMake (analyze finding I1, satisfies FR-011 literally): `Ina226Sensor.cpp` builds on linux always (host tests) and on target only when `CONFIG_BOARD_REV2` — the rev1 binary contains no INA226 code
- [ ] T018 [P] [US3] Host tests in `firmware/test_apps/host/main/test_ina226.cpp` (suite `run_ina226_tests()`): scaling vectors hand-computed from datasheet formulas incl. the 5 mΩ/0.5 mA operating point (CAL=2048) and a negative-current case (sign preserved); identity mismatch → error 1; absent device → error 1; mid-read bus error → error 2 + last-good + recovery re-probe; config/cal write bytes asserted big-endian in order
- [ ] T019 [P] [US3] Implement `LockedPowerSensor` in `firmware/components/sensors/include/sensors/LockedPowerSensor.h`
- [ ] T020 [US3] Kconfig `WS_INA226_SHUNT_MILLIOHM` (int, default 5, range 1–1000) in `firmware/main/Kconfig.projbuild`; `power` console command (`diag_console_register_power(IPowerSensor&)`) in `firmware/main/diag_console.h/.cpp` — error output distinguishes 1 vs 2; wire `Ina226Sensor` on the SHARED EspI2cBus instance in `firmware/main/app_main.cpp` under `#if BOARD_HAS_INA226`

## Phase 6: User Story 4 — Behavior testable without hardware (P4)

- [ ] T021 [P] [US4] Create `MockLevelSensor` in `firmware/components/sensors/include/sensors/testing/MockLevelSensor.h` with consistency helpers (`scriptValidState(present)`, `scriptInvalid()`); host tests proving all four PR-11 truth-table combinations expressible across two instances (incl. low-dry+high-wet) with coherent validity — the SC-006 consumer-style test
- [ ] T022 [P] [US4] Fail-direction truth tests in `firmware/test_apps/host/main/test_level_sensor.cpp`: disconnected-input simulation (raw pulled HIGH) → rev1 config reads "water present", rev2 config reads "water absent" — checklist line 97 pinned per board with a comment citing the pump-topology safety rationale
- [x] T023 [US4] Register both new suites in `firmware/test_apps/host/main/CMakeLists.txt` + `test_main.cpp` (`run_level_sensor_tests`, `run_ina226_tests`)
  > NOTE (implementer, Mission 1): pulled forward and completed together with
  > T006/T009 — both suite files must exist and be registered for the host app
  > to build, so `run_level_sensor_tests` and `run_ina226_tests` were wired into
  > `test_main.cpp`/`CMakeLists.txt` in Phase 2–3. T021/T022 later APPEND tests
  > to these suites; no further registration work remains.

## Phase 7: Polish & Cross-Cutting

- [ ] T024 [P] Update `firmware/CLAUDE.md`: directory tree, console commands (`level`, `power`, gated `pump reservoir`), new sections (level sensors: polarity/debounce/settle + capability flag rationale; INA226: shared-bus rule, Kconfig shunt, PR-14 deferral), host-test description
- [ ] T025 [P] `docs/parity-checklist.md`: §6 divergence entries (debounce, not-yet-valid gating, INA226 identity check, reservoir-pump capability flag) per contracts/interfaces.md list; prepare the line-96 recording slot for the rev1 bench measurement (do NOT fill it — Paul's HIL records it)
- [ ] T026 [P] Write HIL checklist `specs/006-level-sensors-ina226/checklists/hil.md` (rev1 rig): A level status wet/dry both marks + record measured polarity in parity checklist line 96, B chatter → single transition, C fail direction (disconnect), D pump regression (`pump reservoir` + boot force-off + `env`/`soil` intact), E INA226 note (host-covered, hardware at PR-14)
- [ ] T027 Full verification per quickstart.md: host suite exit 0, rev1+rev2 green from clean checkout, `dependencies.lock` unchanged; capture outputs for the CP3 dossier

## Dependencies & Execution Order

- Phase 1 (T001, external gate on #11) → Phase 2 → user stories.
- Within Phase 2: T002/T003/T007 parallel; T004 → T005/T006.
- US1 (T008–T013) is the MVP; T008 blocks T009/T011/T012/T013.
- US2 (T014–T016) independent of US1 code but shares app_main/diag_console files — run after US1's wiring to avoid churn.
- US3 (T017–T020) needs Phase 2's bus extension (T004–T006); parallel with US1/US2 otherwise.
- US4 (T021–T023) needs T008 (fail-direction tests) and T017 (suite registration together in T023).
- Polish last; T027 = main-session verification.

## Implementation Strategy

MVP = Phases 1–3 (level sensors visible on the rig console). US2 flips the
capability flag with compile-time enforcement (biggest regression risk — isolated
commits). US3 rides the bus extension. Mission split for the implementer (process
lesson): write-only missions, verification in the main session; suggested split
Mission 1 = Phases 2–3, Mission 2 = Phases 4–7 (T027 in main session).
