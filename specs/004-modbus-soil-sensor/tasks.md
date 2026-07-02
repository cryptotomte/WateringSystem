# Tasks: Modbus Soil Sensor over RS485

**Input**: Design documents from `specs/004-modbus-soil-sensor/`
**Prerequisites**: plan.md, research.md (R1–R9), data-model.md, contracts/interfaces.md, quickstart.md

**Tests**: included — the spec's [CI] acceptance criteria explicitly require host
tests (FR-014); test tasks precede/accompany implementation per constitution II.

**Process rules (PR-06 lessons)**: implementer agent missions are WRITE-ONLY — no
docker builds inside agent missions. Every task marked **[VERIFY-MAIN]** is executed
by the orchestrator in the main session via Bash (docker + rsync-to-/tmp pattern,
see quickstart.md). Commit after each phase at minimum; tasks are sized to survive
agent death.

**Organization**: tasks grouped by user story (US1 readings, US2 fault handling,
US3 dual-board, US4 host-testability) so each story is independently verifiable.

## Phase 1: Setup

- [ ] T001 Create `sensors` component skeleton: `firmware/components/sensors/CMakeLists.txt`
      (REQUIRES `interfaces`, `board`; register `src/ModbusSoilSensor.cpp` always and
      `src/EspModbusClient.cpp` only when `IDF_TARGET != linux`, same guard style as
      `firmware/components/storage/CMakeLists.txt`) and
      `firmware/components/sensors/idf_component.yml` pinning `espressif/esp-modbus: "==2.1.2"`
- [ ] T002 [VERIFY-MAIN] Confirm `CONFIG_FMB_MASTER_TIMEOUT_MS_RESPOND` bounds allow
      3000 ms in esp-modbus 2.1.2 Kconfig (inspect managed component after a
      dependency fetch) and add any required override to `firmware/sdkconfig.defaults`
      (plan risk 4)

## Phase 2: Foundational (blocking prerequisites for all stories)

- [ ] T003 [P] Port `IModbusClient` to
      `firmware/components/interfaces/include/interfaces/IModbusClient.h` per
      contracts/interfaces.md (pure C++, guard `WATERINGSYSTEM_INTERFACES_IMODBUSCLIENT_H`,
      SPDX header, contract notes as doc comments: one attempt/no retry, write echo
      verification, statistics semantics)
- [ ] T004 [P] Port `ISoilSensor` to
      `firmware/components/interfaces/include/interfaces/ISoilSensor.h` per
      contracts/interfaces.md (trimmed surface — no setValidRange/isWithinValidRange;
      document the trim and the read()/validity contract)
- [ ] T005 [P] Create `MockModbusClient` in
      `firmware/components/sensors/include/sensors/testing/MockModbusClient.h`:
      scriptable register payloads per (address, startRegister, count), forced
      timeout/CRC/exception errors, call recording (incl. writeSingleRegister log),
      statistics counters — style of `actuators/testing` and `storage/testing` mocks

**Checkpoint**: interfaces + mock exist — all story phases can start.

## Phase 3: User Story 1 — Soil readings on the bench rig (P1) 🎯 MVP

**Goal**: real sensor read on the rig: all 9 registers in one transaction, parity
scaling incl. signed temperature, console `soil`/`rs485test` output.

**Independent test**: quickstart.md §1 decode tests green + §3 HIL steps 1–3.

- [ ] T006 [P] [US1] Write host tests for register decode in
      `firmware/test_apps/host/main/test_soil_sensor.cpp`: known 9-register payload
      decodes to expected moisture/temp/EC/pH/N/P/K (data-model scaling table),
      negative temperature case (0xFF38 → −20.0 °C), humidity ≡ moisture,
      salinity/TDS not exposed; register in
      `firmware/test_apps/host/main/CMakeLists.txt`
- [ ] T007 [US1] Implement `ModbusSoilSensor` (pure logic) in
      `firmware/components/sensors/include/sensors/ModbusSoilSensor.h` +
      `firmware/components/sensors/src/ModbusSoilSensor.cpp`: initialize/read
      (one readHoldingRegisters(0x01, 0x0000, 9) call), decode per data-model.md,
      getters from last successful read, real-bus-read isAvailable() (1 register,
      parity), getLastError; port reference `src/sensors/ModbusSoilSensor.cpp`
      (READ-ONLY legacy)
- [ ] T008 [US1] Implement `EspModbusClient` core in
      `firmware/components/sensors/include/sensors/EspModbusClient.h` +
      `firmware/components/sensors/src/EspModbusClient.cpp`: R1 create/start sequence
      (`mbc_master_create_serial`, ser_opts 9600 8N1 `response_tout_ms=3000`, UART
      port + pins from `board/board.h`), `uart_set_mode(UART_MODE_RS485_HALF_DUPLEX)`,
      RTS = `BOARD_PIN_RS485_DE` under `#if BOARD_HAS_RS485_DE` else no RTS pin (R2),
      readHoldingRegisters/writeSingleRegister via `mbc_master_send_request`
      (commands 0x03/0x06), esp_err_t → error-code mapping per data-model.md (R6),
      statistics counters
- [ ] T009 [US1] Add `LockedSoilSensor` decorator in
      `firmware/components/sensors/include/sensors/LockedSoilSensor.h` (per-call
      mutex, pattern of `actuators/LockedWaterPump.h`; document per-call-not-
      cross-call atomicity like `storage/Locked*`)
- [ ] T010 [US1] Wire sensor into app in `firmware/main/app_main.cpp`:
      construct `EspModbusClient` + `ModbusSoilSensor` + `LockedSoilSensor` after
      storage init; boot log line with client init result; no periodic read task
      (out of scope, PR-11)
- [ ] T011 [US1] Add console commands `soil` and `rs485test` in
      `firmware/main/diag_console.cpp` per contracts/interfaces.md console contract
      (values or error code+name; raw probe + statistics dump), going through
      `LockedSoilSensor`/client
- [ ] T012 [VERIFY-MAIN] [US1] Host suite green (quickstart §1) — decode tests pass,
      exit code 0
- [ ] T013 [VERIFY-MAIN] [US1] rev1 target builds green (quickstart §2, rev1 half)

**Checkpoint**: MVP flashable — HIL steps 1–3 executable by Paul at CP3.

## Phase 4: User Story 2 — Faults yield invalid data, never wrong data (P2)

**Goal**: timeout/validation/exception paths flag invalid + log, one attempt only,
auto-recovery on next read.

**Independent test**: host tests for all fault paths green; HIL steps 4–5.

- [ ] T014 [P] [US2] Extend `firmware/test_apps/host/main/test_soil_sensor.cpp` with
      fault-path tests: timeout → read() false + timeout error + getters not
      presented as fresh; out-of-range moisture/temp/pH → validation error 5;
      Modbus exception → distinct 100+n (or documented generic) code; exactly ONE
      client call per read() on failure (no retry — assert via mock call recording);
      recovery: failing mock then working mock → next read() true; statistics
      increment correctness
- [ ] T015 [US2] Implement fault handling in
      `firmware/components/sensors/src/ModbusSoilSensor.cpp`: range validation per
      data-model table (fail read, error 5, EC/NPK unenforced per parity), validity
      flag semantics, ESP_LOG error on every failed read (tag `soil_sensor`),
      no-retry invariant, lazy availability recovery (no permanent-failure state)
- [ ] T016 [US2] Verify/complete error discrimination in
      `firmware/components/sensors/src/EspModbusClient.cpp`: timeout vs invalid-
      response vs exception mapping (R6); if 2.1.2 hides the slave exception code,
      map to the single documented exception code and note the parity divergence in
      a code comment + PR notes
- [ ] T017 [VERIFY-MAIN] [US2] Host suite green including all fault-path tests

**Checkpoint**: fail-safe contract (FR-005/FR-006/FR-010) host-proven.

## Phase 5: User Story 3 — One driver, two RS485 generations (P3)

**Goal**: rev2 build without DE pin, echo handled, RX pull-up active; rev1
unaffected.

**Independent test**: both targets build (quickstart §2); rev2 binary has no DE
reference; pull-up call present.

- [ ] T018 [US3] Add FW-2 RX pull-up in
      `firmware/components/sensors/src/EspModbusClient.cpp`: after pin setup,
      `gpio_pullup_en(BOARD_PIN_RS485_RX)` unconditionally with comment referencing
      FW-2/THVD1426 SHDN̅–SENS_PWR_EN coupling (R4)
- [ ] T019 [US3] Document FW-4 echo strategy in
      `firmware/components/sensors/src/EspModbusClient.cpp` (comment at
      uart_set_mode site): half-duplex RX gating suppresses THVD1426 echo on rev2,
      T3.5 resync as fallback, PR-14 HIL verifies (R3); confirm no rev1-only
      assumptions in the shared path
- [ ] T020 [VERIFY-MAIN] [US3] Both board targets build green from clean config
      (quickstart §2) and `strings`/grep of rev2 build objects show no
      `BOARD_PIN_RS485_DE` usage compiled in (board.h sanity would have failed the
      build — record the check output)

**Checkpoint**: FR-007/008/009/015 satisfied at build+code level; electrical proof
deferred to PR-14 per spec assumption.

## Phase 6: User Story 4 — Testable without hardware + calibration (P4)

**Goal**: complete host-test surface incl. calibration (CP1 answer A) and the mock
soil sensor PR-11 will consume; CI runs it all.

**Independent test**: full host suite green in CI on linux target.

- [ ] T021 [P] [US4] Extend `firmware/test_apps/host/main/test_soil_sensor.cpp` with
      calibration tests: factor = reference/raw from fresh read; factor applied to
      subsequent reads (EC, pH, moisture); best-effort write to 0x0100/0x0101/0x0102
      fn 0x06 with legacy ×100 encoding (assert via mock write log); write failure
      → calibrate returns success for local part + factor still applied (parity
      non-fatal)
- [ ] T022 [US4] Implement calibration in
      `firmware/components/sensors/src/ModbusSoilSensor.cpp` +
      `ModbusSoilSensor.h`: `calibrateMoisture/PH/EC` porting the exact legacy
      formula from `src/sensors/ModbusSoilSensor.cpp:207-322` (READ-ONLY reference);
      factors RAM-only (R8)
- [ ] T023 [P] [US4] Create `MockSoilSensor` in
      `firmware/components/sensors/include/sensors/testing/MockSoilSensor.h`
      (settable values/validity/errors — the PR-11 consumer fixture)
- [ ] T024 [US4] Add calibration console commands `soil_cal_moisture`,
      `soil_cal_ph`, `soil_cal_ec` in `firmware/main/diag_console.cpp` per console
      contract (factor + write-result reporting)
- [ ] T025 [VERIFY-MAIN] [US4] Full host suite green (all soil tests + existing 50
      tests, exit 0); confirm CI workflow needs no change beyond the new files
      (esp-idf-ci-action already runs `test_apps/host` with `target: linux`)

**Checkpoint**: all [CI] acceptance criteria met.

## Phase 7: Polish & cross-cutting

- [ ] T026 [P] Write HIL checklist to
      `specs/004-modbus-soil-sensor/checklists/hil.md` from quickstart §3 (steps,
      expected outcomes, sign-off boxes; note rev2 items deferred to PR-14) for
      Paul at Checkpoint 3
- [ ] T027 [P] Update `firmware/CLAUDE.md`: `sensors` component summary, esp-modbus
      pin, console command additions, host-test file list
- [ ] T028 Record parity divergences (if any hit during implementation: exception-
      code granularity R6, setTimeout runtime support R5, RTS-timing fallback R2)
      in code comments + `specs/004-modbus-soil-sensor/plan.md` Risks section
      updates
- [ ] T029 [VERIFY-MAIN] Final clean-checkout verification: rsync fresh copy,
      both board builds + host suite from scratch (quickstart §1+§2), commit
      `dependencies.lock` change deliberately (constitution III)

## Dependencies & execution order

- Phase 1 → Phase 2 → Phase 3 (US1) → Phase 4 (US2) → Phase 5 (US3) → Phase 6
  (US4) → Phase 7. Stories after US1 are logically independent but share
  `ModbusSoilSensor.cpp`/`EspModbusClient.cpp`, so run sequentially (single
  implementer at a time per file; [P] marks the safe parallel writes).
- T002 can run any time before T012. T005 blocks T006/T014/T021. T008 blocks
  T010–T013. Console tasks (T011, T024) depend on wiring (T010).

## Parallel opportunities

- Phase 2: T003, T004, T005 (three different files).
- T006 (tests) alongside T007/T008 (different files).
- Phase 6: T021 ∥ T023; Phase 7: T026 ∥ T027.

## Implementation strategy

MVP = Phase 1–3 (US1): flashable rig build with `soil`/`rs485test` — already
HIL-demonstrable. Then US2 (safety contract), US3 (rev2 deltas), US4
(calibration + full CI surface), polish. Implementer missions per phase (write-only,
one commit per phase minimum); orchestrator runs every [VERIFY-MAIN] task in the
main session between missions; fixer agent handles review findings at CP3.
