---

description: "Task list for the WateringController port (PR-11)"
---

# Tasks: Watering Controller (host-tested application logic)

**Input**: Design documents from `specs/011-watering-controller-host-tests/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: INCLUDED and CENTRAL — the master-PRD criterion is 100 % host-tested watering logic. Pure logic
is test-first. On-target wiring (tasks, app_main, rs485test) is HIL-verified.

**Organization**: US1 automatic+fail-safe (P1), US2 reservoir (P2), US3 manual/logging/integration (P3).

## Format: `[ID] [P?] [Story] Description`
- **[P]** parallelizable; **[Story]** US1/US2/US3; paths under `firmware/`.

---

## Phase 1: Setup

- [ ] T001 Create the `control` component: `firmware/components/control/CMakeLists.txt`
  (`idf_component_register(SRCS ... INCLUDE_DIRS include REQUIRES interfaces events)`; pure — compiles on
  host + target, no linux guard). Create `include/control/`.
- [ ] T002 [P] Register host suites: create `test_watering_controller.cpp` + `test_reservoir.cpp` (empty
  `run_watering_controller_tests()` / `run_reservoir_tests()`); add to `SRCS` + `control` to `REQUIRES` in
  `firmware/test_apps/host/main/CMakeLists.txt`; declare + call both in `test_main.cpp`.

**Checkpoint**: control component + suites compile empty on host + both targets.

---

## Phase 2: Foundational (enablers — block the controller + coherent tests)

- [ ] T003 Add the consistent-snapshot helper to `LockedSoilSensor.h` (`SoilSnapshot snapshot()` — one lock,
  read/last-good + all getters + availability + error) per `contracts/snapshot-helpers.md`. Resolve the
  documented cross-call gap.
- [ ] T004 [P] Add `snapshot()` to `LockedEnvironmentalSensor.h` (`EnvSnapshot`) + `LockedLevelSensor.h`
  (`LevelSnapshot`), resolving their `TODO(PR-11)`.
- [ ] T005 [P] Add coherent `scriptSuccessfulRead(...)` / `scriptFailedRead(error)` to
  `MockSoilSensor.h` (mirror `MockEnvironmentalSensor`), so soil test state stays coherent.

**Checkpoint**: snapshot helpers + soil-mock helpers available; existing host suite still green.

---

## Phase 3: User Story 1 - Safe, pulsed automatic watering (Priority: P1) 🎯 MVP

**Goal**: pure `WateringController` automatic logic — moisture thresholds + soak gate + fail-safe stops +
gate-on-read + graceful degradation — 100 % host-tested.

**Independent Test**: host tests over mocks + fakes prove start/soak/stop/fail-safe branches.

### Tests for US1 (write first)

- [ ] T006 [P] [US1] `test_watering_controller.cpp`: automatic branches — start at ≤ low; NO start during
  soak (assert none at soak-1ms, start at soak); restart after soak while still dry; stop at ≥ high;
  gate-on-read (no action before first successful read); config runtime change picked up next tick.
- [ ] T007 [P] [US1] fail-safe branches — soil unavailable / stale (>30 000 ms / never) / moisture
  out-of-range each → immediate stop + no watering, in automatic; **a pending soak pause does NOT delay a
  safety stop**; graceful degradation (constructs + manual works when a sensor failed to init).

### Implementation for US1

- [ ] T008 [US1] `WateringController` (pure) — `include/control/WateringController.h` + `src/…cpp`:
  constructor injects `ISoilSensor&/IWaterPump&/IConfigStore&/IDataStorage&/ITimeProvider&/IWallClock&/
  EventLogger&` (or the Locked snapshot wrappers). `tick()` per `data-model.md` order: `pump.update()` →
  soil snapshot → fail-safe (unconditional, before the gate) → watering decision (start ≤low + soak-elapsed
  from burst END; stop ≥high; set lastBurstEndMs). Reads all thresholds/durations from `config` each tick.
  No IDF includes. `logFailsafe` on each safety stop.

**Checkpoint**: automatic + fail-safe + soak host-tested green; controller pure/host-buildable.

---

## Phase 4: User Story 2 - Reservoir auto-fill state machine (Priority: P2)

**Goal**: pure `ReservoirController` — level truth table (+ invalid + implausible rows), stop-on-high,
max-fill abort, feature gate — host-tested regardless of board.

### Tests for US2 (write first)

- [ ] T009 [P] [US2] `test_reservoir.cpp`: all 5 truth-table rows (incl. invalid-sensor + implausible → no
  action); start on low-dry+high-dry; stop-on-high-wet while running; max-fill abort at 300 s; feature
  disabled forces off; **post-abort cooldown** — after a MaxRuntimeForced abort, NO new auto fill until the
  cooldown elapses even if still low-dry, then a fill starts (a normal high-wet stop does NOT arm cooldown;
  manual bypasses it).

### Implementation for US2

- [ ] T010 [US2] `ReservoirController` (pure) — `include/control/ReservoirController.h` + `src/…cpp` per
  `contracts/reservoir-controller.md`: two `ILevelSensor` snapshots, `IWaterPump`, injected clock; `tick(
  enabled, autoLevelControl)`; the truth table; stop-on-high; the pump's `runFor`/cap for the 300 s abort;
  the post-abort cooldown (`kReservoirRefillCooldownMs` ~60 s, suppress new auto fill after a
  MaxRuntimeForced stop; normal high-wet stop does not arm it; manual bypasses); force-off when disabled.
  Board-independent.

**Checkpoint**: reservoir state machine fully host-tested (both boards compile it).

---

## Phase 5: User Story 3 - Manual override, logging & on-target integration (Priority: P3)

**Goal**: manual override + data logging in the controller (host-tested), then wire the controller +
periodic soil reader on-target driven by the mode flag; fix the rs485test race.

### Tests for US3 (write first)

- [ ] T011 [P] [US3] `test_watering_controller.cpp`: manual override — `startManual` runs despite sensor
  failure (bypass); capped at 300 s; auto-started run is automatic (fail-safe applies); `stop()` clears the
  override. Data logging — cadence + epoch timestamp + NPK≥0 + time-not-set handled.

### Implementation for US3

- [ ] T012 [US3] Add `startManual(int)` / `stop()` + the data-log path to `WateringController` (manual =
  `manualRunActive`, exempt from fail-safe; `stop()` clears it; log env+soil every `dataLogInterval` via
  `IWallClock::nowEpoch()` gated on `isTimeSet()`; fail-safe events via `logFailsafe`, NOT pump transitions
  — those stay with `SystemObserver`).
- [ ] T013 [US3] Periodic soil reader: add a `main/soil_reader_task.{h,cpp}` (or extend `sensor_task` to
  also read soil) at the config sensor interval, refreshing the locked soil snapshot — makes PR-09
  `/sensors` soil `valid`. Watchdog-subscribed + fed like `sensor_task`.
- [ ] T014 [US3] Controller task: `main/watering_task.{h,cpp}` (or fold into the 10 Hz main loop) —
  watchdog-registered (`watchdog_subscribe_current_task()` + `watchdog_feed()` each cycle); calls
  `controller.tick()` + `reservoir.tick(enabled, auto)`. Isolated from network/HTTP.
- [ ] T015 [US3] Wire into `app_main.cpp`: construct `WateringController` + (rev1) `ReservoirController` with
  the live `Locked*` refs + config/storage/clocks/EventLogger; start the controller + soil-reader tasks;
  the API mode flag (`getWateringEnabled()`) drives automatic vs. suspended (no direct ApiServer↔controller
  call). Keep `pumps_force_off()` first; add `control` to `main/CMakeLists.txt` PRIV_REQUIRES.
- [ ] T016 [US3] Fix the `diag_console.cpp` rs485test race: route it through a locked path (via the sensor
  or a locked client) so it no longer drives the raw `EspModbusClient` beneath `LockedSoilSensor` once the
  periodic reader runs. Update the `sensor_task.cpp:79` read+error to the snapshot helper if trivial.

**Checkpoint**: manual + logging host-tested; controller runs on-target driven by the mode flag; live soil.

---

## Phase 6: Polish & Cross-Cutting

- [ ] T017 Add a "Watering controller (feature 011)" section to `firmware/CLAUDE.md` (control component,
  pure/host-tested logic, soak-gate divergence, manual=flag + 300 s cap, reservoir truth table + invalid
  row, fail-safe precedence, snapshot helpers, periodic soil reader, controller task + watchdog).
- [ ] T018 [P] Confirm `dependencies.lock` + esp-modbus pins unchanged (no new managed deps).
- [ ] T019 `idf.py size` both targets — confirm fit in the 1.5 MiB OTA slot; record margin.
- [ ] T020 Author `specs/011-watering-controller-host-tests/checklists/hil.md` from quickstart §HIL (auto
  waters, RS485-pull fail-safe within staleness, manual override, reservoir fill/stop/abort, live soil,
  WiFi-loss isolation). Note the deferral path if no rig time.
- [ ] T021 Branch-coverage checklist for the PR (per the acceptance criteria — every decision/safety branch
  ticked) + full host suite + both board builds green.

---

## Dependencies & Execution Order

- **Setup (P1)** → **Foundational (P2, blocks all — snapshot + mock helpers)** → US1 → US2 → US3 → Polish.
- US1 (`WateringController`) and US3 (manual/logging on it) edit the SAME class — sequence T012 after T008.
- US3 integration (T013–T016) edits `app_main.cpp`/`sensor_task`/`diag_console` — sequence after the pure
  logic (US1/US2) is green.
- Pure/host-tested: T003–T012 (snapshot helpers, controllers, their tests). Target/HIL: T013–T016 (tasks,
  wiring, rs485test).
- MockSoilSensor helpers (T005) are needed by the US1/US3 soil tests.

### Parallel opportunities
- T004/T005 (foundational) parallel. Test tasks T006/T007, T009, T011 are [P] within their story. T018 [P].

---

## Implementation Strategy

- **MVP = US1** (safe pulsed automatic watering — the core + the fail-safe, 100 % host-tested).
- Then US2 (reservoir), then US3 (manual/logging + on-target integration).
- Commit after each phase; write host tests FIRST for the pure logic and confirm they fail before impl.
- Build via Docker on an rsync'd `/tmp/ws011-firmware`; fullclean + rm sdkconfig between boards; rm the host
  build dir if stale.
- Never modify frozen legacy (`src/` is read-only reference). Keep `pumps_force_off()` first. Controller
  pure (no IDF/esp_timer). Avoid `/*` inside block comments (`-Werror=comment`). Coordinate fail-safe vs
  pump-transition logging with `SystemObserver` (no double-log).
