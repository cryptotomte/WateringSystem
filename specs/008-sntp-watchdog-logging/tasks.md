---

description: "Task list for SNTP time, task watchdog & event logging (PR-08)"
---

# Tasks: SNTP Time, Task Watchdog & Event Logging

**Input**: Design documents from `specs/008-sntp-watchdog-logging/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: INCLUDED (Constitution II; CI host suite is the merge gate). Pure logic is test-first. Hardware
paths (`SntpClient`, `esp_task_wdt` wiring, `esp_reset_reason`) are excluded from the linux build and
HIL-verified.

**Organization**: by user story — US1 time (P1), US2 event log (P2), US3 watchdog (P3).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different files, no incomplete dependency)
- **[Story]**: US1 / US2 / US3 (setup/foundational/polish unlabeled)
- Paths under repo root; firmware in `firmware/`.

---

## Phase 1: Setup (Shared Infrastructure)

- [ ] T001 Create the `time` component skeleton: `firmware/components/time/CMakeLists.txt` with the
  `if(${IDF_TARGET} STREQUAL "linux")` guard (linux: pure sources `TimeService.cpp` + include dir; target:
  + `SntpClient.cpp`, `SystemWallClock.cpp` with `PRIV_REQUIRES` for the IDF SNTP/netif/esp_system comps);
  `REQUIRES interfaces`. Create `include/time/`.
- [ ] T002 Add Kconfig to `firmware/main/Kconfig.projbuild` (mirror the `WS_*` style): `WS_SNTP_SERVER`
  (string, default `"se.pool.ntp.org"`), `WS_TASK_WDT_TIMEOUT_S` (int, default ~20, range with a safe
  margin over the 5 s sensor cadence), and a per-component log-level knob if warranted (else document
  using `CONFIG_LOG_*`). Add the `CONFIG_ESP_TASK_WDT_*` settings needed in `firmware/sdkconfig.defaults`
  (do NOT disable `COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE`).
- [ ] T003 [P] Register two host suites: create `firmware/test_apps/host/main/test_time.cpp` and
  `test_event_logger.cpp` (empty-but-linking `run_time_tests()` / `run_event_logger_tests()`); add both to
  `SRCS` in `test_apps/host/main/CMakeLists.txt` (add `time` to `REQUIRES`), forward-declare + call both in
  `test_main.cpp`.

**Checkpoint**: component + host suites compile empty on both targets + linux.

---

## Phase 2: Foundational (Blocking Prerequisites)

- [ ] T004 Define `IWallClock` in `firmware/components/interfaces/include/interfaces/IWallClock.h` (pure,
  no `<ctime>`/IDF): `uint32_t nowEpoch() const` + `bool isTimeSet() const`; include guard
  `WATERINGSYSTEM_INTERFACES_IWALLCLOCK_H`.
- [ ] T005 [P] Host fake `FakeWallClock` in `firmware/components/time/include/time/testing/FakeWallClock.h`
  (settable epoch + is-set), for `EventLogger`/consumer tests.
- [ ] T006 [P] `SyncStatus` in `firmware/components/time/include/time/SyncStatus.h` (pure: `synced`,
  `lastSyncEpoch`).

**Checkpoint**: seam + fake + sync-status type available.

---

## Phase 3: User Story 1 - Correct local time after boot, safe before sync (Priority: P1) 🎯 MVP

**Goal**: SNTP sets Swedish local time after STA connect; explicit time-not-set state before sync; DST
correct.

**Independent Test**: host DST/plausibility tests pass; on the rig the console `time` shows not-set →
correct CET/CEST after sync.

### Tests for US1 (write first)

- [ ] T007 [P] [US1] Host tests in `test_time.cpp`: `isPlausibleEpoch(0)`=false, 2020+ epoch=true;
  `formatLocal` winter→CET(+01:00), summer→CEST(+02:00); DST boundary (last Sun Mar/Oct) flips (set
  `TZ=CET-1CEST,M3.5.0,M10.5.0/3` in the test).

### Implementation for US1

- [ ] T008 [US1] Implement pure `TimeService` in `firmware/components/time/include/time/TimeService.h` +
  `src/TimeService.cpp`: `isPlausibleEpoch`, `formatLocal(epoch)`, `status()`/`onSynced(epoch)` per the
  contract. No IDF includes; uses `<ctime>` (`localtime_r`) which is host-available.
- [ ] T009 [US1] Implement `SystemWallClock` (target) in `include/time/SystemWallClock.h` +
  `src/SystemWallClock.cpp`: `nowEpoch()`=`time(nullptr)`, `isTimeSet()`=`TimeService::isPlausibleEpoch`.
- [ ] T010 [US1] Implement `SntpClient` (target-only) in `include/time/SntpClient.h` +
  `src/SntpClient.cpp`: `applyTimezone()` (`setenv TZ`+`tzset`), `start()` (`esp_netif_sntp_init` against
  `CONFIG_WS_SNTP_SERVER`, step-set, sync callback → `SyncStatus`), idempotent, non-blocking, non-fatal.
- [ ] T011 [US1] Add a `time` console command in `firmware/main/diag_console.{h,cpp}` (mirror `wifi_cmd`):
  print sync status + local time (via injected `IWallClock`/`SyncStatus`), or "time not set". Register via
  a new `diag_console_register_time(...)`; nullptr-tolerant.

**Checkpoint**: DST/plausibility green in CI; time console line ready (HIL shows real time after sync).

---

## Phase 4: User Story 2 - Persistent record of important events (Priority: P2)

**Goal**: reset reason + WiFi state changes + pump start/stop logged to the PR-06 event log with timestamp
+ cause, readable over serial, surviving power-cycle.

**Independent Test**: `EventLogger` host tests (formatting/category/failure) pass; on the rig `storage
events` lists the events with timestamps after a power-cycle.

### Tests for US2 (write first)

- [ ] T012 [P] [US2] Host tests in `test_event_logger.cpp` (vs `MockDataStorage` + `FakeWallClock`): each
  producer (`logReset/logWifi/logPumpStart/logPumpStop`) writes one event with the expected category +
  detail; a write-failure (`MockDataStorage.failWrites`) increments the dropped counter without crashing;
  reset-reason mapping is total.

### Implementation for US2

- [ ] T013 [US2] Implement `EventLogger` in `firmware/main/event_logger.{h,cpp}`: composes `IDataStorage&`
  + `IWallClock&`; typed producers per `contracts/event-logger.md`; `storeEvent`==false counted +
  `ESP_LOGW`, never throws/blocks; never logs credential values. Reset-reason→string mapping here.
- [ ] T014 [US2] Reset-reason boot event in `firmware/main/app_main.cpp`: after storage is up, call
  `esp_reset_reason()` and `EventLogger::logReset(...)` once. Keep `pumps_force_off()` first.
- [ ] T015 [US2] `system_observer` in `firmware/main/system_observer.{h,cpp}`: holds `LockedDataStorage`
  (via `EventLogger`) + `WifiManager*` + pump handles; polled from the main loop. Detects WiFi
  `snapshot().state` transitions → `EventLogger::logWifi`; detects pump start/stop transitions →
  `EventLogger::logPumpStart/Stop` with cause. (SNTP kickoff added in T018.)
- [ ] T016 [US2] Wire the `system_observer` into the `app_main` 10 Hz loop (call its `poll()` each
  iteration) and register the event-log/time consumers with the diag console. Confirm the existing
  `storage events [n]` command reads these entries (enrich formatting to render local time if cheap).

**Checkpoint**: events logged + readable; host formatting green; power-cycle persistence is HIL.

---

## Phase 5: User Story 3 - Automatic recovery from a hung task, pumps safe (Priority: P3)

**Goal**: esp_task_wdt on the watering-critical tasks; hung task → reboot; pumps OFF after; reset reason
logged. WiFi task excluded.

**Independent Test**: HIL — starve a subscribed task → reboot → pumps OFF → `reset=TASK_WDT` logged; no
spurious reboots.

### Implementation for US3

- [ ] T017 [US3] `task_watchdog` helper in `firmware/main/task_watchdog.{h,cpp}` per
  `contracts/task-watchdog.md`: `watchdog_init()` (timeout from `CONFIG_WS_TASK_WDT_TIMEOUT_S`,
  panic-on-timeout; `ESP_ERR_INVALID_STATE`→OK), `watchdog_subscribe_current_task()`, `watchdog_feed()`.
- [ ] T018 [US3] Subscribe + feed the watering-critical tasks: the `app_main` 10 Hz main loop
  (subscribe once, `watchdog_feed()` each iteration) and `firmware/main/sensor_task.cpp` (subscribe at
  task start, feed each 5 s cycle). Do NOT subscribe `wifi_task` or the console REPL. Also start SNTP once
  in the `system_observer`/main loop on the first WiFi `Connected` (`SntpClient::start()` + `applyTimezone`
  at init).
- [ ] T019 [US3] Confirm the boot fail-safe + reset-reason path end-to-end: `pumps_force_off()` remains the
  first `app_main` action and the `reset=TASK_WDT` event (T013/T014) is emitted after a watchdog reset
  (verified at HIL). No code beyond ensuring ordering/wiring is correct.

**Checkpoint**: watchdog subscribed with correct policy; auto-recovery + pumps-safe verified at HIL.

---

## Phase 6: Polish & Cross-Cutting

- [ ] T020 Add an SNTP/watchdog/event-log section to `firmware/CLAUDE.md` (component layout, the
  watchdog subscription policy, the time-not-set contract for PR-11).
- [ ] T021 `idf.py size` on both targets — confirm the app fits the 1.5 MiB OTA slot; note margin.
- [ ] T022 [P] Confirm `dependencies.lock` + both `esp-modbus` pins unchanged (only IDF built-ins added).
- [ ] T023 Author `specs/008-sntp-watchdog-logging/checklists/hil.md` from quickstart §HIL (time+sync,
  watchdog reboot + pumps OFF + reset event, event-log power-cycle persistence, WiFi-outage-no-reboot,
  non-fatal SNTP). Note deferral path if no rig time.
- [ ] T024 Full host suite + both board builds green (quickstart CI section).

---

## Dependencies & Execution Order

- **Setup (P1)** → **Foundational (P2, blocks all)** → US1 → US2 → US3 → Polish.
- US2's `system_observer` (T015) and US3's SNTP-start + watchdog-feed (T018) both edit the `app_main` main
  loop and `system_observer` — sequence US3's T018 after US2's T015/T016.
- US1's `SyncStatus`/`SntpClient` (T010) is consumed by US3's SNTP-start wiring (T018) — US1 before US3.
- `EventLogger` (T013) is used by the reset event (T014), the observer (T015), and later PR-11 fail-safe.
- Pure/host-tested: T007 (time), T012 (event logger). Hardware/HIL: T009/T010 (sntp/wall-clock),
  T017/T018 (watchdog), T014/T019 (boot/reset wiring).

### Parallel opportunities

- T005/T006 (foundational) parallel. T007 and T012 (tests) parallel. T022 parallel with other polish.

---

## Implementation Strategy

- **MVP = US1** (correct time + not-set state) — the base every timestamp/schedule depends on.
- Then US2 (durable event record incl. reset reason), then US3 (watchdog auto-recovery).
- Commit after each phase/logical group; verify host tests fail before implementing the pure logic.
- Build via Docker on an rsync'd `/tmp/ws008-firmware` copy; fullclean + rm sdkconfig between board builds.
- Never modify frozen legacy paths. Keep `pumps_force_off()` first in `app_main`. WiFi task stays out of
  the watchdog.
