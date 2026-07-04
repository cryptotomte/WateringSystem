# Phase 0 Research: SNTP Time, Task Watchdog & Event Logging

Grounded in the verified codebase map (origin/main, PR-06 + PR-07 merged) and the pre-made decisions in
`docs/prd/PR-08-*.md` + `docs/parity-checklist.md`. No open `NEEDS CLARIFICATION`.

## D1 — Event log reuses PR-06 `IDataStorage` (no interface extension)

- **Decision**: Wire PR-08's events into the existing `IDataStorage::storeEvent(epoch, category, detail)` /
  `getEvents(n)`. Categories are already defined (`kCategoryPump/Failsafe/Connectivity/Ota/Reset`, open
  uint8); `detail` (≤120 B) carries the cause; caller supplies the epoch. `storage events [n]` already
  reads it over serial.
- **Rationale**: The surface fully covers PR-08's needs (research §1). Inventing new storage would
  duplicate PR-06 and diverge the rotation logic. `LockedDataStorage` already provides cross-task safety.
- **Alternatives**: A new event API — rejected (redundant); a severity field — rejected (category is the
  only axis; severity can be encoded in the detail prefix if ever needed).

## D2 — SNTP sets system time; a thin `IWallClock` seam makes epoch testable

- **Decision**: Start `esp_netif_sntp` against `CONFIG_WS_SNTP_SERVER` (default `se.pool.ntp.org`) after
  the STA connects; `setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3")` + `tzset()` at init so `localtime_r`
  yields Swedish local time. Introduce `IWallClock { uint32_t nowEpoch(); bool isTimeSet(); }` — target
  impl over `time(nullptr)`, host `FakeWallClock` — so consumers (EventLogger, console) get a testable
  epoch source. System time set by SNTP also makes the existing raw `time(nullptr)` call sites correct.
- **Rationale**: `ITimeProvider` is monotonic-ms only (research §2) — no wall-clock seam exists. TZ/DST
  conversion is host-testable with `setenv("TZ")`+`localtime_r` (available on the linux host). Keeping the
  epoch behind `IWallClock` lets EventLogger tests be deterministic.
- **Alternatives**: Extend `ITimeProvider` with epoch — rejected (conflates monotonic vs wall-clock, and
  its many existing consumers don't want wall-clock); SNTP smooth-slew — rejected, use step-set
  (immediate) so time becomes correct in one jump (simpler; the log tolerates the discontinuity).

## D3 — Explicit time-not-set via plausibility threshold

- **Decision**: `isTimeSet()` = current epoch ≥ a plausible minimum (year ≥ 2020, matching the legacy
  plausibility check). Before first sync the state is "not set"; consumers (PR-11 schedule) gate on it.
- **Rationale**: Parity §7 (legacy waited for year ≥ 2020). A pure predicate is trivially host-tested and
  prevents acting on a 1970 clock (FR-004, safety-relevant for PR-11).

## D4 — Watchdog: subscribe watering-critical tasks; exclude WiFi; action = reboot

- **Decision**: Enable `esp_task_wdt` (config via sdkconfig + `WS_TASK_WDT_TIMEOUT_S`), subscribe the
  **10 Hz `app_main` main loop** (it drives pump `update()`) and the **sensor task**; each calls
  `esp_task_wdt_add(NULL)` + `esp_task_wdt_reset()` in its loop. The **WiFi task is NOT subscribed**. On a
  non-servicing task the watchdog panics → reboot; `pumps_force_off()` (unchanged) runs first at the next
  boot.
- **Rationale**: The watering-critical loops are the main loop (pump/level `update()`) and the sensor task;
  a hang there must self-heal. The WiFi task must never reboot the device (PR-07 isolation, FR-014) — a
  network stall is not a watering failure. Replaces the ineffective legacy software watchdog (QUIRK 3).
  Timeout margin: sensor task services every 5 s, so a default well above that (e.g. 15–30 s) avoids false
  positives while still catching a genuine hang.
- **Alternatives**: Subscribe all tasks incl. WiFi — rejected (a WiFi stall would reboot and interrupt
  watering); watchdog = log-only — rejected (no auto-recovery; the PRD requires reboot).
- **Scope note**: PR-11's watering/reservoir-control tasks subscribe through the same helper when they land
  (recorded so it is not a review finding).

## D5 — Reset reason captured at boot → event

- **Decision**: Call `esp_reset_reason()` early in `app_main` (after storage is up) and log it via
  `EventLogger` as a `kCategoryReset` event (mapping POWERON/SW/PANIC/INT_WDT/TASK_WDT/BROWNOUT/… to a
  short detail string). Epoch is the best-known (pre-sync) time at boot.
- **Rationale**: FR-009 / acceptance criterion. `esp_reset_reason` is not called anywhere today (research
  §3). Logging pre-sync is acceptable (D2/edge case) — the reset fact matters more than its exact epoch.

## D6 — Event producers wired at snapshot consumers (isolation-safe)

- **Decision**: A `system_observer` (polled from the main loop, holding `LockedDataStorage` + the
  `WifiManager*` + the pump handles) detects and logs: WiFi state transitions (from `snapshot().state`),
  and pump start/stop transitions with cause. It also kicks off SNTP once on the first `Connected`. The
  reset event is logged directly at boot.
- **Rationale**: `WifiManager` structurally holds no storage (FR-014) and exposes no observer hook
  (research §4) — state-change logging must live at a consumer that already reads `snapshot()`. The main
  loop already polls pumps and can hold storage. Fail-safe-activation events (sensor-invalid → pump stop)
  are produced by the **PR-11 controller**; PR-08 defines the `kCategoryFailsafe` logging path and the
  `EventLogger` helper so PR-11 just calls it (scope note).
- **Alternatives**: Inject `IDataStorage` into `WifiManager` — rejected (breaks FR-014 isolation); a global
  event bus — rejected (over-engineered for a handful of producers).

## D7 — `EventLogger` is a thin, host-tested formatting layer over `IDataStorage`

- **Decision**: `EventLogger` composes an `IDataStorage&` + `IWallClock&` and offers typed helpers
  (`logReset(reason)`, `logWifi(state)`, `logPumpStart/Stop(pump, cause)`, `logFailsafe(detail)`,
  `logOta(detail)`), each building the `detail` string + category and calling `storeEvent(nowEpoch, ...)`.
  A storage-write `false` is counted/`ESP_LOGW`'d, never thrown.
- **Rationale**: Centralizes the category+detail format (host-tested against `MockDataStorage`), keeps
  producers terse, and contains write failures (FR-014). Pure-enough for host tests (the IDF-free parts).

## Open risks

- **R1 — SNTP reachability at HIL**: greenhouse network/DNS must resolve the pool; failure is non-fatal by
  design (FR-001). Verify at HIL.
- **R2 — Watchdog false positive**: if a legitimately slow operation exceeds the timeout, tune
  `WS_TASK_WDT_TIMEOUT_S`. Chosen default leaves margin over the 5 s sensor cadence.
- **R3 — Binary size**: SNTP + task-wdt are small IDF built-ins; confirm the app still fits 1.5 MiB.
- **R4 — Clock step & getEvents ordering**: `getEvents` assumes monotonic epochs; a pre-sync→synced jump
  can interleave ordering. Acceptable (entries are insertion-ordered on disk); documented in the edge case.
