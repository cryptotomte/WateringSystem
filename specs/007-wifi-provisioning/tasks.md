---

description: "Task list for WiFi Provisioning & Station Management (PR-07)"
---

# Tasks: WiFi Provisioning & Station Management

**Input**: Design documents from `specs/007-wifi-provisioning/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: INCLUDED. Host-testability is a non-negotiable constitution principle (II) and the CI host
suite is the merge gate. Pure-logic tasks are written test-first (test task precedes implementation task).
Hardware classes (`EspWifiDriver`, `ProvisioningPortal`) are excluded from the linux build and verified by
HIL, not host tests.

**Organization**: Grouped by user story (US1 provisioning = MVP, US2 station+reconnect, US3 recovery).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no incomplete dependencies)
- **[Story]**: US1 / US2 / US3 (setup, foundational, polish have no story label)
- All paths are under the repo root; firmware lives in `firmware/`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Scaffold the new `network` component and Kconfig; wire the host test suite.

- [ ] T001 Create the `network` component skeleton: `firmware/components/network/CMakeLists.txt` with the
  `if(${IDF_TARGET} STREQUAL "linux")` guard (pure sources — `WifiManager.cpp` — on host; pure + hardware
  sources on target), `REQUIRES interfaces board`, IDF deps (`esp_wifi esp_netif esp_event
  esp_http_server`) in `PRIV_REQUIRES` on the target branch only; add `include/` dir.
- [ ] T002 Add Kconfig options in `firmware/main/Kconfig.projbuild` under `menu "WateringSystem"`
  (`WS_` prefix, mirror `WS_INA226_SHUNT_MILLIOHM` style): `WS_PROV_AP_SSID` (string, default
  `"WateringSystem-Setup"`), `WS_PROV_AP_PASSWORD` (string; documented non-secret; help text notes WPA2 +
  choose-new-at-impl), and optional int options for the reconnect constants
  (`WS_WIFI_RETRY_INTERVAL_MS`=10000, `WS_WIFI_FAILS_BEFORE_PAUSE`=5, `WS_WIFI_PAUSE_MS`=60000,
  `WS_WIFI_MONITOR_INTERVAL_MS`=5000) with `range`s.
- [ ] T003 [P] Register a WiFi host-test suite entry point: add `test_wifi.cpp` to `SRCS` and `network` to
  `REQUIRES` in `firmware/test_apps/host/main/CMakeLists.txt`; declare + call `run_wifi_tests()` in
  `firmware/test_apps/host/main/test_main.cpp` (empty suite stub compiles and links).

**Checkpoint**: `network` component and host suite compile empty on both targets + linux.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The seam + shared types + system init that BOTH provisioning and station work build on.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [ ] T004 Define `IWifiDriver` in `firmware/components/interfaces/include/interfaces/IWifiDriver.h`
  (pure, NO IDF includes) per `contracts/IWifiDriver.md`: `enum class WifiEvent`, and virtual methods
  `staConnect`, `staStop`, `apStart`, `apStop`, `pollEvent`, `rssi`. Include guard
  `WATERINGSYSTEM_INTERFACES_IWIFIDRIVER_H`.
- [ ] T005 [P] Define shared WiFi state types in
  `firmware/components/network/include/network/WifiState.h`: `enum class WifiState` (Provisioning,
  Connecting, Connected, Reconnecting, ReconnectPaused), `struct WifiConnectionSnapshot` (state, rssi,
  consecutiveFailures, disconnectCount, ipAcquired), and `struct ReconnectPolicy` (defaults = parity /
  Kconfig constants). Pure header.
- [ ] T006 [P] Create `MockWifiDriver` in
  `firmware/components/network/include/network/testing/MockWifiDriver.h`: scriptable event queue
  (`queueEvent` + helpers `scriptConnectSuccess`/`scriptConnectFailure`), call counters, last-ssid/last-
  password capture, settable `rssi()`. Host-only; no IDF.
- [ ] T007 Add system network init to `firmware/main/app_main.cpp`: call `esp_netif_init()` and
  `esp_event_loop_create_default()` AFTER the existing `nvs_flash_init()` and BEFORE any WiFi construction.
  Do NOT move `pumps_force_off()` — it stays the first action. Log via `ESP_LOG*` with a tag; treat
  failures as non-fatal-but-logged consistent with existing init style.

**Checkpoint**: interface + mock + shared types available; app_main initializes netif + default event
loop. User stories can now proceed.

---

## Phase 3: User Story 1 - Provision a fresh device onto home WiFi (Priority: P1) 🎯 MVP

**Goal**: An unconfigured device exposes a reachable WPA2 SoftAP setup page at 192.168.4.1; a valid SSID +
password submission is validated, persisted to NVS, and the device restarts to apply it.

**Independent Test**: Flash empty credentials → device enters AP mode → submit valid credentials from a
browser → success response → device persists and restarts. (Host tests cover the pure validation + boot-
mode decision; the AP/HTTP path is HIL.)

### Tests for User Story 1 (write first, ensure they FAIL)

- [ ] T008 [P] [US1] Host tests for `validateWifiCredentials` in
  `firmware/test_apps/host/main/test_wifi.cpp`: SSID empty→reject, 1–32→accept, >32→reject; password
  empty→accept, 1–7→reject, ≥8→accept, >64→reject (per data-model validation rules).
- [ ] T009 [P] [US1] Host tests for `decideBootMode` in `firmware/test_apps/host/main/test_wifi.cpp`: all
  four rows of the truth table (contract `wifi-manager-states.md`).

### Implementation for User Story 1

- [ ] T010 [P] [US1] Implement pure `validateWifiCredentials(ssid, password)` in
  `firmware/components/network/include/network/WifiCredentialValidation.h` (+ `.cpp` if needed) returning a
  typed accept/reject result; enforces SSID 1–32 and password empty-or-8..64.
- [ ] T011 [P] [US1] Implement pure `decideBootMode(credentialsPresent, configButtonHeld)` and
  `enum class WifiBootMode` in `firmware/components/network/include/network/WifiBootMode.h`.
- [ ] T012 [US1] Implement `ProvisioningPortal` (target-only) in
  `firmware/components/network/include/network/ProvisioningPortal.h` + `src/ProvisioningPortal.cpp`:
  standalone `esp_http_server`; `GET /` serves a minimal self-contained English setup page (SSID +
  password form); `POST /wifi/config` parses form, calls `validateWifiCredentials`, then
  `IConfigStore::setWifiCredentials`. Never logs/echoes the password. Keep the page small (OTA-slot
  budget, research R2). Per `contracts/provisioning-portal.md`.
- [ ] T013 [US1] Implement the post-save restart: on persist success, respond 200 (success page,
  `restartRequired`) then schedule `esp_restart()` ~3 s later so the response is delivered first
  (`ProvisioningPortal.cpp`). On validation reject → 4xx no-persist-no-restart; on persist failure → 5xx,
  stay provisionable.
- [ ] T014 [US1] Wire provisioning into `app_main`: when `decideBootMode` → Provisioning, start the AP via
  the driver (`apStart` using `CONFIG_WS_PROV_AP_SSID` / `CONFIG_WS_PROV_AP_PASSWORD`, WPA2) and start
  `ProvisioningPortal`; do not start STA connect in this mode.

**Checkpoint**: Fresh/empty-credential device serves a reachable setup page and persists+restarts on valid
submit. Pure validation + boot-mode logic green in CI. MVP demoable (HIL).

---

## Phase 4: User Story 2 - Stay connected and survive outages without affecting watering (Priority: P2)

**Goal**: A configured device connects in STA mode, reconnects on the parity fixed-interval schedule
(10 s / +60 s after 5 / 5 s monitor), exposes its connection state, and never lets WiFi activity touch
watering.

**Independent Test**: Connect the device; drop the AP (with a watering-relevant task running on the rig)
→ watering cadence unaffected → restore AP → device reconnects on schedule. Host tests cover the schedule
and the isolation property deterministically.

### Tests for User Story 2 (write first, ensure they FAIL)

- [ ] T015 [P] [US2] Host tests for the reconnect schedule in `firmware/test_apps/host/main/test_wifi.cpp`
  using `MockWifiDriver` + `FakeTimeProvider` (contract `wifi-manager-states.md` §Timing): connect happy
  path; retry only at 10 s (assert none at 9999 ms); pause 60 s after 5 consecutive failures with no
  attempts during the pause and failures reset after; monitor evaluates at 5 s; AP-mode suspends
  monitoring.
- [ ] T016 [P] [US2] Host test for FR-014 isolation in `firmware/test_apps/host/main/test_wifi.cpp`:
  `tick()` returns promptly with a silent/hung driver (never blocks); assert `WifiManager`'s constructor
  takes only `IWifiDriver`/`IConfigStore`/`ITimeProvider`/`ReconnectPolicy` (no watering type) — document
  the dependency set.

### Implementation for User Story 2

- [ ] T017 [US2] Implement the pure `WifiManager` state machine in
  `firmware/components/network/include/network/WifiManager.h` + `src/WifiManager.cpp`: constructor injects
  `IWifiDriver&`, `IConfigStore&`, `ITimeProvider&`, `ReconnectPolicy`; `begin(WifiBootMode)`, `tick()`
  (drains `pollEvent()`, advances timers via `nowMs()`), `snapshot()`. Implements all transitions in
  `data-model.md` (Connecting→Connected on GotIp; retry cadence; pause after 5; monitor; no boot loop).
- [ ] T018 [US2] Implement `EspWifiDriver` (target-only) in
  `firmware/components/network/include/network/EspWifiDriver.h` + `src/EspWifiDriver.cpp`: create STA + AP
  netifs, register `esp_event` handlers translating `WIFI_EVENT_*`/`IP_EVENT_STA_GOT_IP` into a
  thread-safe `WifiEvent` queue drained by `pollEvent()`; implement `staConnect/staStop/apStart/apStop/
  rssi` non-blocking per `contracts/IWifiDriver.md`. No timing, no business logic.
- [ ] T019 [US2] Implement the WiFi task in `firmware/main/wifi_task.h` + `firmware/main/wifi_task.cpp`
  mirroring `main/sensor_task.cpp`: `xTaskCreate` with its own stack/priority, `[[noreturn]]`, manager
  injected via `void* arg`, fixed-cadence `tick()` loop, non-fatal creation failure. Separate task from
  the pump/level 10 Hz loop (FR-014).
- [ ] T020 [US2] Wire station mode into `app_main`: when `decideBootMode` → Station, construct
  `EspWifiDriver` + `WifiManager`, `begin(Station)`, and `wifi_task_start(manager)`. Keep everything after
  `pumps_force_off()` and the existing sensor/console setup; ensure no shared mutex with watering.
- [ ] T021 [US2] Expose connection state: implement `snapshot()` consumers hook — make the manager
  snapshot readable by the diag console (add/extend a `wifi` status line reading the snapshot without
  echoing credentials). Drive `BOARD_PIN_STATUS_LED` connect-attempt toggle (~500 ms) from the wifi task
  (parity §7/§9). Single-acquisition snapshot (research D9).

**Checkpoint**: Configured device connects and reconnects on the parity schedule; state exposed; schedule
+ isolation green in CI.

---

## Phase 5: User Story 3 - Recover a device with wrong or changed credentials (Priority: P3)

**Goal**: Config-button-held-at-boot forces provisioning (clearing stored credentials); a wrong password
never causes a boot loop and the device stays provisionable.

**Independent Test**: Hold `BOARD_PIN_BTN_CONFIG` (GPIO18) at boot on a configured device → credentials
cleared → provisioning mode. Separately, provision a wrong password → device keeps retrying, stays
provisionable, no reboot loop.

### Tests for User Story 3 (write first, ensure they FAIL)

- [ ] T022 [P] [US3] Host test "no boot loop under permanent failure" in
  `firmware/test_apps/host/main/test_wifi.cpp`: script an infinite `ConnectFailed` stream across many
  rounds; assert the machine only cycles Reconnecting↔ReconnectPaused, never requests a restart, bounded
  state (FR-013). (Extends the US2 machine; validated here as the US3 acceptance.)
- [ ] T023 [P] [US3] Host test for the emergency-reset decision in
  `firmware/test_apps/host/main/test_wifi.cpp`: `decideBootMode(credentialsPresent=true, buttonHeld=true)`
  → Provisioning (already in T009's table; here assert the paired "clear credentials" intent is invoked in
  the boot flow via a small testable helper if one is extracted).

### Implementation for User Story 3

- [ ] T024 [US3] Read the config button at boot in `app_main`: sample `BOARD_PIN_BTN_CONFIG` (GPIO18) with
  the required hold semantics (parity: held during startup) and feed `configButtonHeld` into
  `decideBootMode`. Keep the GPIO read minimal (no new abstraction required).
- [ ] T025 [US3] Emergency reset flow in `app_main`: when the button forces provisioning on a configured
  device, call `IConfigStore::clearWifiCredentials()` before entering AP mode (per `data-model.md` boot
  rule).
- [ ] T026 [US3] Config-button-hold LED feedback: blink `BOARD_PIN_STATUS_LED` ~100 ms during the button
  hold window (parity §7/§9). HIL-verified.

**Checkpoint**: All three stories independently functional; recovery paths validated.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T027 Reference the FR9 SoftAP-portal decision from `firmware/CLAUDE.md` (acceptance-criteria [CI]
  requirement), pointing at `specs/007-wifi-provisioning/`. (Root/agent CLAUDE.md is edited by the
  implementer as an explicit task, not via the spec-kit agent-context hook.)
- [ ] T028 Run `idf.py size` on both targets; confirm the app (incl. portal + WiFi stack) fits the 1.5 MiB
  OTA slot; record the margin in the PR description / note if tight (research R2).
- [ ] T029 [P] Confirm `dependencies.lock` and both `esp-modbus` pins (main/ + components/sensors/) are
  unchanged by this PR (no new managed components introduced).
- [ ] T030 Author the HIL checklist file for Paul (rev1 rig) from `quickstart.md` §HIL: fresh-device
  provisioning, AP power-cycle isolation, config-button-at-boot, wrong-password no-boot-loop, console/
  portal credential equivalence, LED patterns, brownout watch (QUIRK 4). Note deferral path if hardware
  is unavailable.
- [ ] T031 Run the full host suite on the linux target; confirm 0 failures and both board targets build
  green (quickstart.md CI section).

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies.
- **Foundational (Phase 2)**: after Setup — BLOCKS all stories (interface, mock, types, netif/event-loop).
- **US1 (Phase 3)**: after Foundational. MVP.
- **US2 (Phase 4)**: after Foundational. Shares `app_main` wiring with US1 (T014/T020 touch the same
  file — sequence them, not parallel).
- **US3 (Phase 5)**: after US2's state machine exists (T022 extends it) and after US1's provisioning entry
  (T024/T025 route into the provisioning path). Sequence after US1+US2.
- **Polish (Phase 6)**: after all desired stories.

### Story independence & coupling notes

- US1 and US2 both edit `firmware/main/app_main.cpp` (boot-mode branch) — treat those tasks as sequential.
- US3's "no boot loop" (T022) is a property of the US2 `WifiManager`; it is listed under US3 because it is
  US3's acceptance criterion, but it needs T017 done first.
- Pure-logic tasks (T008–T011, T015–T017, T022–T023) are host-tested; hardware tasks (T012–T014, T018,
  T024–T026) are HIL-only.

### Parallel opportunities

- T005 / T006 (Foundational) are parallel.
- Within US1: T008/T009 (tests) parallel; T010/T011 (pure impl) parallel.
- Within US2: T015/T016 (tests) parallel; then T017 before T018–T021.
- T029 parallel with other polish.

---

## Parallel Example: User Story 1

```bash
# Tests first (parallel):
Task: "Host tests for validateWifiCredentials in firmware/test_apps/host/main/test_wifi.cpp"   # T008
Task: "Host tests for decideBootMode in firmware/test_apps/host/main/test_wifi.cpp"            # T009
# Then pure implementations (parallel):
Task: "Implement validateWifiCredentials (WifiCredentialValidation.h)"                          # T010
Task: "Implement decideBootMode (WifiBootMode.h)"                                               # T011
```

---

## Implementation Strategy

### MVP first (US1)

1. Phase 1 Setup → 2. Phase 2 Foundational → 3. Phase 3 US1 → **STOP & VALIDATE** provisioning path
   (host: validation + boot mode; HIL: reachable setup page persists+restarts).

### Incremental delivery

- Setup + Foundational → foundation ready.
- + US1 → provisioning MVP (device is configurable from a browser).
- + US2 → device actually joins the LAN and survives outages without touching watering.
- + US3 → field recovery (button reset, no boot loop).

### Notes

- Commit after each task or logical group (handover: "design missions to die cheaply — commit often").
- Verify host tests fail before implementing the pure logic they cover.
- Builds run via Docker on an rsync'd `/tmp/ws007-firmware` copy (Docker can't mount OneDrive); host tests
  on the linux preview target.
- Never modify frozen legacy paths (`src/`, `include/`, `data/`, `test/`, `platformio.ini`).
