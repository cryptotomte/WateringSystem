# Tasks: BME280 Environmental Sensor over I2C

**Input**: Design documents from `specs/005-bme280-i2c/`
**Prerequisites**: plan.md, research.md (R1–R10), data-model.md, contracts/interfaces.md, quickstart.md

**Tests**: Host tests are explicitly required by the spec (SC-002, Constitution II) — test
tasks are in scope and follow the harness conventions in `firmware/test_apps/host/`.

**Organization**: Tasks are grouped by user story (US1–US4 from spec.md). Hard
sequencing constraint (research R10): implementation starts only after **PR #10
(004-modbus-soil-sensor) has merged** — the `sensors` component, `diag_console`
surface and host-test harness this feature extends arrive with it.

## Phase 1: Setup

- [ ] T001 Rebase/merge branch `005-bme280-i2c` onto origin/main AFTER PR #10 has merged; verify `firmware/components/sensors/`, `diag_console_register_soil` and `test_apps/host/main/test_soil_sensor.cpp` exist on the branch; verify both board targets and the host suite build green from clean checkout (baseline before any change)

## Phase 2: Foundational (blocking prerequisites for all user stories)

- [x] T002 [P] Create `IEnvironmentalSensor` interface in `firmware/components/interfaces/include/interfaces/IEnvironmentalSensor.h` — standalone pure C++ (no IDF includes), soil-sensor conventions: initialize/read/isAvailable/getLastError + getTemperature (°C)/getHumidity (%RH)/getPressure (hPa), last-good-value validity contract in doc comments per `contracts/interfaces.md`
- [x] T003 [P] Create `II2cBus` interface in `firmware/components/interfaces/include/interfaces/II2cBus.h` — probe(address7)/readRegisters(address7, startReg, buf, len)/writeRegister(address7, reg, value), repeated-start semantics documented, no IDF includes, per `contracts/interfaces.md`
- [x] T004 Create `MockI2cBus` in `firmware/components/sensors/include/sensors/testing/MockI2cBus.h` — header-only scriptable register map (chip-ID, calibration block, data registers), probe results per address, failure injection (NACK on address, error on register read/write, corrupt data), call/write recording for config-byte assertions

## Phase 3: User Story 1 — Environmental readings on the bench rig (P1) 🎯 MVP

**Goal**: BME280 read every 5 s on the rig with parity sampling profile; console `env` command; values match the Arduino unit.

**Independent Test**: flash rig, watch 5 s log cadence, run `env`, compare against Arduino unit (±0.5 °C, ±3 %RH, ±1 hPa).

- [x] T005 [US1] Implement `Bme280Sensor` (pure logic) in `firmware/components/sensors/include/sensors/Bme280Sensor.h` + `firmware/components/sensors/src/Bme280Sensor.cpp`: constructor takes `II2cBus&`; initialize() = probe 0x76→0x77, chip-ID check (0xD0 == 0x60), calibration readout/parse (dig_T/P/H incl. split-nibble H4/H5), sampling profile writes ctrl_hum(0xF2)=0x01 → ctrl_meas(0xF4)=0x57 → config(0xF5)=0x90 (data-model.md — verify byte encodings against Bosch datasheet field tables); read() = burst 0xF7–0xFE, compensate T→P→H via t_fine (Bosch int32/int64 reference algorithms), convert to °C/%RH/hPa (P: ÷256 ÷100), NaN → error 2; error codes 0/1/2 per data-model.md
- [x] T006 [P] [US1] Host tests for init + happy-path read in `firmware/test_apps/host/main/test_bme280.cpp` (new suite `run_bme280_tests()`): scripted MockI2cBus with full register map → initialize() succeeds, exact config bytes written in order (ctrl_hum before ctrl_meas), read() produces expected values; getters-before-first-read documented behavior
- [x] T007 [P] [US1] Implement `EspI2cBus` in `firmware/components/sensors/include/sensors/EspI2cBus.h` + `firmware/components/sensors/src/EspI2cBus.cpp` (target-only): owns `i2c_master_bus_handle_t` from `BOARD_PIN_I2C_SDA/SCL`, port auto, internal pull-ups on; per-address device handles created on first use at 100 kHz; probe→`i2c_master_probe`, readRegisters→`i2c_master_transmit_receive`, writeRegister→`i2c_master_transmit`; finite timeouts; errors returned as false, logged at debug; PRIV_REQUIRES `esp_driver_i2c`
- [x] T008 [P] [US1] Implement `LockedEnvironmentalSensor` in `firmware/components/sensors/include/sensors/LockedEnvironmentalSensor.h` — header-only FreeRTOS-mutex decorator, same pattern as `LockedSoilSensor` (per-call serialization; document the read-then-getters pattern limitation + PR-11 snapshot bookkeeping)
- [x] T009 [US1] Update `firmware/components/sensors/CMakeLists.txt`: add `Bme280Sensor.cpp` to all-target SRCS, `EspI2cBus.cpp` + `esp_driver_i2c` dep inside the existing `if(NOT ${IDF_TARGET} STREQUAL "linux")` guard
- [x] T010 [US1] Implement sensor task in `firmware/main/sensor_task.cpp` + `firmware/main/sensor_task.h`: `sensor_task_start(IEnvironmentalSensor&)` creates FreeRTOS task (4096 B stack, priority 1, `vTaskDelayUntil` 5000 ms); each cycle read() + INFO log of values on success; WARN once on valid→invalid transition and on recovery, repeated failures at bounded cadence (every Nth); task starts even when sensor absent, never exits (contracts/interfaces.md)
- [x] T011 [US1] Add `env` console command: `diag_console_register_env(IEnvironmentalSensor&)` in `firmware/main/diag_console.h` + handler in `firmware/main/diag_console.cpp` — thin wrapper, one read(), success prints the three values with units, failure prints `ERROR <code>` + hint distinguishing code 1 (not found) from code 2 (read failed) per contracts/interfaces.md
- [x] T012 [US1] Wire it all in `firmware/main/app_main.cpp`: function-local static `EspI2cBus` → `Bme280Sensor` → `LockedEnvironmentalSensor`; `diag_console_register_env(...)` before `diag_console_start()`; `sensor_task_start(...)` after console registration; init failure = log-and-continue (non-safety subsystem), everything after `pumps_force_off()` — follow the PR-04 wiring pattern

**Checkpoint**: rig delivers periodic readings + `env` works → MVP demonstrable.

## Phase 4: User Story 2 — Sensor faults yield invalid data, never wrong data (P2)

**Goal**: unplug/replug and boot-without-sensor are non-events: invalid + logged, auto-recovery, no reboot.

**Independent Test**: unplug mid-run → invalid + warning, no crash; replug → recovery; boot sensorless → normal boot, later attach recovers.

- [ ] T013 [US2] Harden `Bme280Sensor` failure semantics in `firmware/components/sensors/src/Bme280Sensor.cpp`: bus error during read → error 2, getters keep last-good values, driver marked uninitialized (next call re-probes — recovery path); failed probe/chip-ID → error 1; lazy re-init from read() AND isAvailable() when uninitialized; isAvailable() = real chip-ID read, never cached, never touches lastError (contracts/interfaces.md, data-model.md state machine)
- [ ] T014 [P] [US2] Host tests for error paths in `firmware/test_apps/host/main/test_bme280.cpp`: absent sensor (probe NACK both addresses) → initialize false + error 1; mid-read bus error → read false + error 2 + last-good values intact + subsequent recovery re-probe succeeds; NaN-producing raw values → error 2; boot-sensorless then attach → lazy re-init delivers reading; isAvailable true/false against scripted bus, error code untouched
- [ ] T015 [US2] Verify sensor-task logging discipline against a scripted failing/recovering sensor: WARN on transitions, bounded repeat cadence, no task exit — host-test the pure log-decision helper if extracted, otherwise verify by targeted code review note in the task (logging policy per research R7)

## Phase 5: User Story 3 — Works with both module address variants (P3)

**Goal**: 0x76- and 0x77-strapped modules both just work; wrong chip identity rejected.

**Independent Test**: scripted buses with the device at each address; wrong-chip-ID device rejected (host); module swap on rig if hardware available.

- [ ] T016 [US3] Verify/complete probing policy in `firmware/components/sensors/src/Bme280Sensor.cpp`: probe order 0x76 → 0x77, first ACK + correct chip-ID wins; ACK with wrong chip-ID logged distinctly and rejected (continues to next candidate, else error 1); recovery after loss re-probes BOTH addresses (covers swapped-address replug edge case)
- [ ] T017 [P] [US3] Host tests for address handling in `firmware/test_apps/host/main/test_bme280.cpp`: device at 0x76 found; device at 0x77 found; wrong-chip-ID at 0x76 with real device at 0x77 → 0x77 chosen; wrong-chip-ID everywhere → error 1; loss then reappearance at the OTHER address → recovered

## Phase 6: User Story 4 — Sensor behavior testable without hardware (P4)

**Goal**: compensation math proven against Bosch reference vectors; mocks ready for PR-11 consumers; all of it in CI.

**Independent Test**: host suite passes on a hardware-less machine; CI job green.

- [ ] T018 [US4] Add Bosch reference-vector tests in `firmware/test_apps/host/main/test_bme280.cpp`: fixed (calibration set, raw T/P/H) → expected compensated outputs from the Bosch datasheet reference implementation (int32 T, int64 P, int32 H), incl. the datasheet worked example, a negative-temperature vector and extreme-but-legal raws; each vector's derivation cited in a comment (research R8); float conversions (÷100, ÷256÷100, ÷1024) asserted
- [ ] T019 [P] [US4] Create `MockEnvironmentalSensor` in `firmware/components/sensors/include/sensors/testing/MockEnvironmentalSensor.h` — scripted value/validity/error sequences WITH consistency helpers `scriptSuccessfulRead(t,h,p)` / `scriptFailedRead(error)` from the start (PR-04 lesson), for PR-11 consumer tests
- [ ] T020 [P] [US4] Host tests for MockEnvironmentalSensor consistency in `firmware/test_apps/host/main/test_bme280.cpp`: scripted sequences observed exactly; helpers keep values/validity/error coherent
- [x] T021 [US4] Register the suite: add `test_bme280.cpp` to SRCS in `firmware/test_apps/host/main/CMakeLists.txt`, declare + call `run_bme280_tests()` in `firmware/test_apps/host/main/test_main.cpp` between UNITY_BEGIN/UNITY_END — NOTE: pulled forward into implementation mission 1 (with T002–T012) so the main session can run the T006 suite immediately

## Phase 7: Polish & Cross-Cutting

- [ ] T022 [P] Update `firmware/CLAUDE.md`: directory tree (Bme280Sensor/EspI2cBus/Locked/testing files, sensor_task), console command list (`env`), a "BME280 environmental sensor" section (architecture split, shared-bus note for PR-05, parity sampling profile), host-test description
- [ ] T023 [P] Record deliberate divergences in `docs/parity-checklist.md` §6: address probing + chip-ID check, last-good getter values (legacy: NaN), live availability probe (legacy: cached), Locked-decorator synchronization (legacy: unsynchronized dual readers) — per contracts/interfaces.md list
- [ ] T024 [P] Write HIL checklist `specs/005-bme280-i2c/checklists/hil.md` following `specs/004-modbus-soil-sensor/checklists/hil.md` format: (A) periodic readings + Arduino agreement, (B) console `env` incl. absent-vs-failed distinction, (C) unplug/replug + boot-without-sensor, (D) 0x76 module swap (if hardware available, else mark host-covered), (E) regression guard (pumps OFF at boot, pump/soil console commands intact)
- [ ] T025 Full verification per `specs/005-bme280-i2c/quickstart.md`: host suite exit 0; rev1 AND rev2 build green from clean checkout (fullclean between overlays); `dependencies.lock` unchanged; capture outputs for the CP3 dossier

## Dependencies & Execution Order

- **Phase 1 → Phase 2 → Phase 3**: strictly sequential gates (T001 is a hard external gate on PR #10).
- **US1 (Phase 3)** is the MVP and blocks nothing after it: US2 (T013 hardening) modifies files created in US1; US3 refines the same probing code (T016 after T013 — same file); US4's vector tests (T018) only need T005.
- **Parallel within phases**: T002/T003 together; T006/T007/T008 after T005; T014 after T013; T017 after T016; T019/T020 anytime after T002; T022/T023/T024 together.
- **Story order**: US1 → US2 → US3 → US4 → Polish. (US4's T018 may start as soon as T005 exists if an extra pair of hands is available.)

## Implementation Strategy

MVP = Phase 1–3 (US1): rig shows parity readings every 5 s + `env` command — demonstrable and HIL-testable on its own. US2/US3 harden the same driver files incrementally (each leaves the suite green). US4 locks the math and the consumer-facing mocks. Polish updates docs and produces the HIL checklist Paul runs at Checkpoint 3.
