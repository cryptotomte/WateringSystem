# Implementation Plan: SNTP Time, Task Watchdog & Event Logging

**Branch**: `008-sntp-watchdog-logging` | **Date**: 2026-07-04 | **Spec**: [spec.md](./spec.md)

**Input**: `specs/008-sntp-watchdog-logging/spec.md`; PR brief `docs/prd/PR-08-sntp-watchdog-logging.md`;
parity `docs/parity-checklist.md` §7 (NTP), QUIRK 2/3, §6 (event-log surface).

## Summary

Complete the phase-2 exit criteria with three loosely-coupled additions, all keeping watering untouched:
(1) **SNTP wall-clock time** — set system time from the Swedish pool after the STA connects, apply the
Europe/Stockholm TZ, and expose an explicit "time-not-set" state; (2) **hardware task watchdog** — subscribe
the watering-critical tasks (the 10 Hz main loop that drives pump `update()`, and the sensor task) to
`esp_task_wdt` so a hung task forces a safe reboot (pumps OFF at boot, reset reason persisted), while the
WiFi task is deliberately excluded; (3) **event logging** — wire producers (reset reason at boot, WiFi
state changes, pump start/stop with cause) into the **existing** PR-06 `IDataStorage` rotating event log
via a small host-tested `EventLogger`, readable over the serial console. Pure logic (TZ/DST conversion,
plausibility, event formatting, reset-reason mapping) is host-tested; the IDF surfaces (`esp_sntp`,
`esp_task_wdt`, `esp_reset_reason`) are thin and target-only.

## Technical Context

**Language/Version**: C++17 on ESP-IDF v6.0.1 (Docker `espressif/idf:v6.0.1`), target esp32.

**Primary Dependencies**: IDF built-ins only — `esp_netif_sntp`/`esp_sntp`, `esp_task_wdt`,
`esp_system` (`esp_reset_reason`), standard C `<ctime>` (`localtime_r`, `setenv("TZ")`). No new managed
components → `dependencies.lock` untouched. Reuses PR-06 `IDataStorage` (event log) and PR-07 `WifiManager`
(connection snapshot).

**Storage**: Event log reuses PR-06 `IDataStorage::storeEvent(epoch, category, detail)` / `getEvents(n)` —
categories already defined (`kCategoryPump=1, kCategoryFailsafe=2, kCategoryConnectivity=3, kCategoryOta=4,
kCategoryReset=5`), 2×16 KiB rotation, `detail` ≤120 B. **No interface extension needed.**

**Testing**: Unity host suite (`test_apps/host/`). New host tests: CET/CEST DST conversion (via
`setenv("TZ")`+`localtime_r` on the linux host), time-not-set plausibility, `EventLogger` formatting/
category selection against `MockDataStorage`, reset-reason→string mapping, and event-log rotation/content
via the existing `LittleFsDataStorage` tempdir pattern. Hardware paths (`SntpClient`, `esp_task_wdt`
wiring, `esp_reset_reason`) are HIL-verified.

**Target Platform**: ESP32-WROOM-32E, board targets `BOARD_REV1_DEVKIT` and `BOARD_REV2`.

**Project Type**: Embedded firmware component(s) + `main/` wiring.

**Performance Goals**: SNTP/logging never block or delay watering (FR-014). Watchdog timeout has a safe
margin over the slowest critical-task cadence (sensor task 5 s → default timeout well above that).

**Constraints**: `pumps_force_off()` stays the first action in `app_main`; watchdog action = reboot;
WiFi task excluded from the watchdog; storage-write failure while logging is contained (never crashes or
touches watering); epoch stored, local time only user-facing; app binary stays within the 1.5 MiB OTA slot.

**Scale/Scope**: One `time` component (SNTP + wall-clock seam + pure conversion), one `EventLogger` +
watchdog helper (in `main/` or a small `system` component), reset-reason at boot, a WiFi/SNTP observer at a
snapshot consumer, pump-event logging at the main-loop pump observer, one Kconfig block, one host suite.

## Constitution Check

*GATE: evaluated before Phase 0 and re-checked after Phase 1. Result: PASS (no violations).*

- **I. Safety First (NON-NEGOTIABLE)** — PASS, and this feature *strengthens* safety. The watchdog gives
  real auto-recovery from a hung watering task (replacing the ineffective legacy software watchdog, QUIRK
  3); after a watchdog reset `pumps_force_off()` (unchanged, first action) guarantees pumps OFF (FR-008);
  the reset reason is persisted (FR-009). SNTP/logging run outside the watering path and are non-blocking
  (FR-014); a storage-write failure is contained. The **WiFi task is deliberately NOT watchdog-subscribed**
  so a network stall cannot reboot the device and interrupt watering (preserves PR-07 isolation). The
  constitution's "safety-relevant events MUST be persisted" is exactly what the event log delivers.
- **II. Host-Testability** — PASS. TZ/DST conversion, time-not-set plausibility, `EventLogger`
  formatting/category selection, and reset-reason mapping are pure and host-tested; `esp_sntp`/
  `esp_task_wdt`/`esp_reset_reason` are thin target-only shims behind seams (`IWallClock`, the SNTP starter,
  the watchdog helper), excluded from the linux build.
- **III. Reproducible Builds** — PASS. Only IDF built-ins; no new managed components;
  `dependencies.lock` and both `esp-modbus` pins untouched. Both board targets build.
- **IV. Frozen Legacy** — PASS. No change under `src/`/`include/`/`data/`/`test/`/`platformio.ini`.
- **V. Checkpoint-Gated Workflow** — PASS. Implementation delegated to the `implementer` after CP2.
- **VI. English Outward** — PASS.

**Additional-constraints note:** watchdog config lives in Kconfig/sdkconfig (`CONFIG_ESP_TASK_WDT_*` +
`WS_TASK_WDT_TIMEOUT_S`), board differences stay in the board component (no scattered ifdefs), and the
`COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE` safety pin in `sdkconfig.defaults` is left untouched.

## Project Structure

### Documentation (this feature)

```text
specs/008-sntp-watchdog-logging/
├── plan.md · research.md · data-model.md · quickstart.md
├── contracts/{wall-clock-time.md, event-logger.md, task-watchdog.md}
├── checklists/requirements.md
└── tasks.md            # /speckit-tasks output (not created here)
```

### Source Code (repository root)

```text
firmware/
├── components/
│   ├── interfaces/include/interfaces/
│   │   └── IWallClock.h            # NEW — pure seam: nowEpoch(), isTimeSet() (no IDF)
│   ├── time/                       # NEW component
│   │   ├── CMakeLists.txt          # linux-guarded (pure on host, SNTP on target)
│   │   ├── include/time/
│   │   │   ├── TimeService.h        # pure: plausibility + local-time formatting + sync-status
│   │   │   ├── SyncStatus.h         # pure: synced flag, last-sync epoch
│   │   │   ├── SntpClient.h          # thin esp_netif_sntp starter (target-only)
│   │   │   ├── SystemWallClock.h     # IWallClock over time(nullptr)/settimeofday (target)
│   │   │   └── testing/FakeWallClock.h  # host fake (settable epoch / is-set)
│   │   └── src/{TimeService.cpp, SntpClient.cpp, SystemWallClock.cpp}
│   └── (storage/network/... reused unchanged; event log = PR-06 IDataStorage)
├── main/
│   ├── app_main.cpp                # EDIT — esp_reset_reason() boot event; construct TimeService +
│   │                               #   SntpClient + EventLogger; subscribe main loop to watchdog + feed
│   ├── event_logger.{h,cpp}        # NEW — EventLogger over IDataStorage + IWallClock (host-testable core)
│   ├── task_watchdog.{h,cpp}       # NEW — esp_task_wdt subscribe/feed helpers + registration pattern
│   ├── system_observer.{h,cpp}     # NEW — polls WifiManager snapshot: start SNTP on first Connected,
│   │                               #   log WiFi state changes + pump start/stop transitions (holds storage)
│   ├── sensor_task.cpp             # EDIT — subscribe to watchdog + feed each cycle
│   ├── Kconfig.projbuild           # EDIT — WS_SNTP_SERVER, WS_TASK_WDT_TIMEOUT_S, log-level option(s)
│   └── sdkconfig.defaults          # EDIT — CONFIG_ESP_TASK_WDT_* as needed (per-component log levels)
└── test_apps/host/main/
    ├── test_time.cpp               # NEW — DST conversion, plausibility/time-not-set
    ├── test_event_logger.cpp       # NEW — EventLogger formatting/category vs MockDataStorage
    ├── test_main.cpp · CMakeLists.txt   # EDIT — register run_time_tests()/run_event_logger_tests()
```

**Structure Decision**: A new `time` component holds SNTP + the wall-clock seam + pure conversion (mirrors
the sensor/network components; SNTP source excluded from the linux build via the
`if(${IDF_TARGET} STREQUAL "linux")` guard). `IWallClock` goes in `interfaces` so pure consumers never see
`<ctime>`/IDF. `EventLogger`, `task_watchdog`, and the `system_observer` live in `main/` because they are
wiring that composes existing components (storage + wifi + pumps) — the same place `sensor_task`/
`wifi_task` live; `EventLogger`'s formatting core is header-light and host-tested against `MockDataStorage`.
The event log itself is **not** re-implemented — it is PR-06's `IDataStorage`.

## Complexity Tracking

> No constitution violations. Table intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| —         | —          | —                                    |
