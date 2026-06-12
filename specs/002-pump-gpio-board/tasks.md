# Tasks: Pump Actuator Layer and Board Abstraction

**Input**: Design documents from `/specs/002-pump-gpio-board/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: Host tests are an explicit deliverable of this feature (spec US3, FR-012,
SC-004) — they are required, not optional.

**Organization**: Tasks grouped by user story. US1 = safe pump control on rig (P1),
US2 = one firmware/two boards (P2), US3 = host-testable pump behavior (P3).

## Phase 1: Setup

- [x] T001 Create `interfaces` component skeleton: `firmware/components/interfaces/CMakeLists.txt` (header-only registration, no dependencies)
- [x] T002 Create `actuators` component skeleton: `firmware/components/actuators/CMakeLists.txt` with target-conditional sources (exclude `GpioWaterPump.cpp` and the `esp_driver_gpio` dependency when `IDF_TARGET=linux`, per research.md D2)

## Phase 2: Foundational (blocking prerequisites for all stories)

- [x] T003 [P] Port `IActuator` to pure C++ in `firmware/components/interfaces/include/interfaces/IActuator.h` (from Arduino `include/actuators/IActuator.h`; `std::string`, no Arduino types; contract per `contracts/iwaterpump.md`)
- [x] T004 [P] Port `IWaterPump` with the new contract (runFor rejection rules, `update()`, runtime statistics, `StopReason`) in `firmware/components/interfaces/include/interfaces/IWaterPump.h`
- [x] T005 [P] Create `ITimeProvider` (monotonic `int64_t nowMs()`) in `firmware/components/interfaces/include/interfaces/ITimeProvider.h`
- [x] T006 Implement `WaterPump` base class (ALL timing/safety logic: state machine per data-model.md, paired `applyOutput` transitions, max-runtime 300 s enforcement in `update()`, accumulated runtime, `lastStopReason`) in `firmware/components/actuators/include/actuators/WaterPump.h` + `firmware/components/actuators/src/WaterPump.cpp`
- [x] T007 [P] Implement `EspTimeProvider` (`esp_timer_get_time()/1000`) in `firmware/components/actuators/include/actuators/EspTimeProvider.h`

**Checkpoint**: Foundation ready — all three user stories can now start.

## Phase 3: User Story 1 — Safe pump control on the bench rig (P1) 🎯 MVP

**Goal**: Pumps provably off at boot; serial start/stop/timed control with hard
300 s cap on the rev1 rig.

**Independent Test**: Flash rig → boot-off check on GPIO 26/27 → serial commands
per `contracts/serial-diagnostic.md` HIL mapping.

- [x] T008 [US1] Implement `GpioWaterPump` (`applyOutput` → `gpio_set_level`, active HIGH; `initialize()` forces OFF before anything else) in `firmware/components/actuators/include/actuators/GpioWaterPump.h` + `firmware/components/actuators/src/GpioWaterPump.cpp`
- [x] T009 [US1] Wire two pump instances ("plant" GPIO 26, "reservoir" GPIO 27, shared `EspTimeProvider`) behind `IWaterPump` in `firmware/main/app_main.cpp`; poll `update()` in the main loop (≥ 10 Hz); keep the existing boot fail-safe FIRST and unchanged
- [x] T010 [US1] Implement `esp_console` REPL with `pump <plant|reservoir> <start <s>|stop|status>` + `pump status` per `contracts/serial-diagnostic.md` in `firmware/main/diag_console.cpp`; register `console` dependency in `firmware/main/CMakeLists.txt`
- [x] T011 [US1] Verify rev1 build in the pinned container (quickstart §1) and fix anything broken; confirm boot banner + REPL prompt appear and no pump GPIO goes high at boot (log inspection)

**Checkpoint**: US1 delivers the MVP — rig-controllable, safety-capped pumps.

## Phase 4: User Story 2 — One firmware, two boards (P2)

**Goal**: Board component is the complete single source of truth; wrong/missing
board selection cannot produce a binary.

**Independent Test**: Clean-checkout builds of both variants; compile-time pin-table
check; boot banner names the right board.

- [x] T012 [P] [US2] Extend both board tables in `firmware/components/board/include/board/board.h`: status LED 2, manual button 5, config button 18 (rev2 values provisional with `TODO(SYNC1)`), keeping the FROZEN-doc warning comments and `#error` guard intact
- [x] T013 [US2] Add compile-time board sanity checks (e.g. `static_assert` on pin distinctness and flag consistency — `BOARD_HAS_RS485_DE` ⟺ DE macro defined) in `firmware/components/board/include/board/board.h`
- [x] T014 [US2] Verify BOTH board variants build green in the pinned container with correct `CONFIG_BOARD_*` in generated sdkconfig (quickstart §1); update `firmware/README.md` board table if pins were added

**Checkpoint**: US2 complete — board contract ready for PR-03..05.

## Phase 5: User Story 3 — Pump behavior testable without hardware (P3)

**Goal**: Real enforcement logic verified deterministically in CI with zero hardware.

**Independent Test**: `idf.py --preview set-target linux && idf.py build && ./build/*.elf`
exits 0 on a machine with no ESP32.

- [x] T015 [P] [US3] Create `MockWaterPump` (records `applyOutput` transitions) and `FakeTimeProvider` (manual `advance()`) header-only in `firmware/components/actuators/include/actuators/testing/MockWaterPump.h` and `.../testing/FakeTimeProvider.h`
- [x] T016 [US3] Create host test app (linux preview target, `set(COMPONENTS main)` isolation, plain Unity runner with `std::exit(UNITY_END())`) in `firmware/test_apps/host/CMakeLists.txt`, `firmware/test_apps/host/sdkconfig.defaults`, `firmware/test_apps/host/main/CMakeLists.txt` per research.md D1
- [x] T017 [US3] Write Unity tests in `firmware/test_apps/host/main/test_water_pump.cpp` covering: duration self-stop, max-runtime forced stop (+ reason + log), rejected starts (0 s, > 300 s, already-running — no output/state change), stop-when-stopped no-op, paired output transitions (invariant 1), accumulated runtime tracking, enforcement within one poll
- [x] T018 [US3] Run the host suite in the pinned container (quickstart §2), fix until exit 0 (9/9 PASS, exit 0)
- [x] T019 [US3] Add `host-test` job to `.github/workflows/firmware-build.yml` (esp-idf-ci-action, path `firmware/test_apps/host`, command per research.md D1; `if-no-files-found`/failure semantics consistent with existing jobs; include `firmware/test_apps/**` in trigger paths)

**Checkpoint**: All user stories complete.

## Phase 6: Polish & Cross-Cutting

- [x] T020 [P] Update `firmware/CLAUDE.md` (new components, host-test commands, console diagnostic) and `firmware/README.md` (host-test section per quickstart)
- [x] T021 [P] Write the HIL test checklist for CP3 (Paul runs on rig) in `specs/002-pump-gpio-board/checklists/hil.md` from `contracts/serial-diagnostic.md` §HIL mapping
- [ ] T022 Self-review pass: parity-checklist cross-check (§1/§2 items this PR claims), constitution gates re-check, no stray `esp_timer` in host-tested code

## Dependencies

```
Phase 1 (T001-T002) ──► Phase 2 (T003-T007) ──► US1 (T008-T011) ──► Polish (T020-T022)
                                          ├──► US2 (T012-T014) ──┤
                                          └──► US3 (T015-T019) ──┘
US1, US2, US3 are mutually independent after Phase 2.
Within US3: T015 ──► T016 ──► T017 ──► T018 ──► T019.
Within US1: T008 ──► T009 ──► T010 ──► T011.
```

## Parallel Execution Examples

- After Phase 2: T008 (US1), T012 (US2) and T015 (US3) touch disjoint files — parallelizable.
- Within Phase 2: T003, T004, T005, T007 are independent headers [P]; T006 depends on T003-T005.
- Polish: T020 and T021 are independent [P].

## Implementation Strategy

MVP first: Phases 1-3 (US1) give a flashable, rig-testable increment. US2 and US3
can then land in either order; US3 is the CI quality gate and should not be skipped
before review. Single implementer agent executes sequentially in this order:
Setup → Foundational → US1 → US2 → US3 → Polish (parallelism not worth the
coordination cost at this size).
