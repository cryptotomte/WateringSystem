# PR-08: sntp-watchdog-logging

> Phase 2 — infrastructure (completes phase 2 exit criteria)

## Goal

Correct wall-clock time (SNTP, Swedish pool, CET/CEST), hardware task watchdog on all
critical tasks, and structured logging with persistence of important events.

## Scope

### SNTP / time (FR10)

- `esp_sntp` against the Swedish pool (`se.pool.ntp.org`), sync status exposed.
- Timezone CET/CEST via TZ string `CET-1CEST,M3.5.0,M10.5.0/3`; all timestamps in
  local time where user-facing, epoch internally.
- Watering schedule logic must behave sanely before first sync (time-not-set state is
  explicit, consumed by PR-11).

### Task watchdog

- `esp_task_wdt` registered on all critical tasks (sensor task, watering/control task,
  reservoir control) — replaces the Arduino software loop-watchdog (30 s).
- Watchdog reset path verified: pumps are OFF after a watchdog-induced reboot
  (safety NFR: pumps off at boot, after watchdog reset, after OTA restart).

### Logging & event persistence

- ESP_LOG levels configured per component (Kconfig).
- Persistent event log on littlefs (PR-06 storage): pump start/stop with cause,
  sensor-failure fail-safe activations, WiFi state changes, OTA events, watchdog/
  brownout reset reasons (from `esp_reset_reason`). Bounded size with rotation.
- Event log readable via serial diagnostics now; API exposure lands in PR-09.

## Out of scope

- The watering schedule itself (PR-11), API endpoints (PR-09), OTA event sources
  (PR-13 emits into the log defined here).

## Functional requirements covered

- FR10 (SNTP + CET/CEST); logging/persistence NFR; reliability NFR (esp_task_wdt on
  critical tasks); FR12 (partially: event-log serial diagnostics).

## Dependencies

- PR-06 (littlefs for event persistence), PR-07 (network for SNTP).

## Acceptance criteria

- [CI] Both targets build; host test for timezone conversion (CET/CEST DST
  boundaries) and event-log rotation against mock storage.
- [HIL] Rig shows correct Swedish local time after boot; survives across DST rules
  (unit-tested) and reports sync status.
- [HIL] Deliberately starved task triggers the watchdog; device reboots, pumps
  measured OFF immediately after reset, reset reason persisted in the event log.
- [HIL] Power-cycle preserves the event log; pump start/stop events appear with
  timestamps and cause.

## Estimated size

M
