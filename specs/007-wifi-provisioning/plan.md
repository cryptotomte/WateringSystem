# Implementation Plan: WiFi Provisioning & Station Management

**Branch**: `007-wifi-provisioning` | **Date**: 2026-07-04 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/007-wifi-provisioning/spec.md`; PR brief
`docs/prd/PR-07-wifi-provisioning.md`; parity contract `docs/parity-checklist.md` §7.

## Summary

Add WiFi station management with parity-baseline reconnection and first-boot SoftAP provisioning to
the ESP-IDF firmware, without ever letting network activity touch watering. The design keeps all timing
and state-machine logic in a **pure `WifiManager`** driven by `ITimeProvider` and an injected
**`IWifiDriver`** seam, so the reconnection schedule (10 s retry, +60 s pause after 5 fails, 5 s monitor)
and the boot-mode decision are host-tested against a mock driver + `FakeTimeProvider`. The IDF surface
(`esp_wifi`/`esp_netif`/`esp_event` and the standalone provisioning `esp_http_server`) lives in thin
hardware classes excluded from the linux build. WiFi runs on its own FreeRTOS task, structurally
isolated from the 10 Hz pump/level loop. Credentials reuse the existing `IConfigStore` WiFi API from
PR-06 (empty SSID = unconfigured); no new credential store is introduced.

## Technical Context

**Language/Version**: C++17 on native ESP-IDF v6.0.1 (Docker `espressif/idf:v6.0.1`); no Arduino layers.

**Primary Dependencies**: `esp_wifi`, `esp_netif`, `esp_event`, `esp_http_server`, `nvs_flash` (already
initialized), FreeRTOS. Pure layer depends only on project interfaces (`IWifiDriver`, `IConfigStore`,
`ITimeProvider`) — no IDF headers.

**Storage**: Reuses `IConfigStore` (PR-06) WiFi API — `getWifiSsid()/getWifiPassword()/
setWifiCredentials()/clearWifiCredentials()`; NVS namespace `wscfg`, keys `wifi_ssid`/`wifi_pass`;
credential values never logged. Unconfigured = empty SSID string.

**Testing**: Unity host suite on the IDF linux preview target (`test_apps/host/`); new `run_wifi_tests()`.
Hardware paths (`EspWifiDriver`, `ProvisioningPortal`) are excluded from the linux build and verified by
HIL on the rev1 devkit rig.

**Target Platform**: ESP32-WROOM-32E, board targets `BOARD_REV1_DEVKIT` and `BOARD_REV2` (rev2 LED/button
pins are `TODO(SYNC1)` provisional).

**Project Type**: Embedded firmware component + `main/` wiring; single-repo, component-based.

**Performance Goals**: Reconnection cadence exactly per parity (10 s / +60 s after 5 / 5 s monitor).
Watering loop cadence (10 Hz poll, pump timing) suffers zero measurable deviation during WiFi events.

**Constraints**: Provisioning portal binary must fit the 1.5 MiB OTA app slot; NVS partition is 16 KiB;
brownout detector stays enabled (parity QUIRK 4 watch-item); AP uses WPA2 with a Kconfig-sourced,
documented non-secret password (brand-new, legacy never reused).

**Scale/Scope**: One WiFi manager, one provisioning portal with a single setup page and one POST handler,
one new FreeRTOS task, one new `network` component + one interface + one mock. Minimal standalone portal —
the full JSON/status API and `/api/wifi/scan` are PR-09.

## Constitution Check

*GATE: evaluated before Phase 0 and re-checked after Phase 1 design. Result: PASS (no violations).*

- **I. Safety First (NON-NEGOTIABLE)** — PASS. WiFi runs on a dedicated task that shares **no mutex or
  state with the pump/level watering path**; `pumps_force_off()` remains the first action in `app_main`.
  The pure `WifiManager` depends only on `IWifiDriver`/`IConfigStore`/`ITimeProvider` — it is
  *structurally incapable* of blocking or influencing watering. Host tests assert this dependency set and
  that a stuck/failing driver never stalls the manager's tick contract (FR-014). **Scoping note:** the
  watering *controller* (pump control loop) lands in PR-11; at PR-07 the isolation is verified
  structurally (dependency set + separate task) plus HIL on the rig, and full watering-cycle-during-outage
  is a PR-07 HIL item and re-validated in PR-12. This scoping is recorded here deliberately so it does not
  surface as a Checkpoint-3 finding.
- **II. Host-Testability** — PASS. All logic (reconnect state machine, boot-mode decision, credential
  validation) is pure and host-tested; hardware access is behind the new `IWifiDriver` interface in the
  `interfaces` component, mirroring `IModbusClient`/`II2cBus`. IDF-touching classes contain no business
  logic and are excluded from the linux build via the `if(${IDF_TARGET} STREQUAL "linux")` CMake guard.
- **III. Reproducible Builds** — PASS. Both board targets build in the pinned container; the new
  `network` component adds only IDF-provided dependencies (no new managed components), so
  `dependencies.lock` and the two `esp-modbus` pins are untouched.
- **IV. Frozen Legacy** — PASS. No change under `src/`, `include/`, `data/`, `test/`, `platformio.ini`.
- **V. Checkpoint-Gated AI Workflow** — PASS. Plan authored by the orchestrator; implementation will be
  delegated to the `implementer` subagent after CP2 approval.
- **VI. English Outward** — PASS. All artifacts, code, and the setup page are in English.

**Additional-constraints note (documented, compliant — not a deviation):** the AP password in
`CONFIG_WS_PROV_AP_PASSWORD` is an intentional *documented non-secret*, not a secret in the
"no secrets in the repo" sense. It only shields the brief unconfigured-device window; WPA2 is kept so the
operator's real home-WiFi credentials are never sent in cleartext during setup, and those real
credentials live only in NVS (constitution-compliant). A brand-new password is chosen at implementation.

## Project Structure

### Documentation (this feature)

```text
specs/007-wifi-provisioning/
├── plan.md              # This file
├── research.md          # Phase 0 — decisions & rationale
├── data-model.md        # Phase 1 — entities + WifiManager state machine
├── quickstart.md        # Phase 1 — host-test + HIL validation guide
├── contracts/
│   ├── IWifiDriver.md            # driver seam contract
│   ├── wifi-manager-states.md    # state machine + reconnect timing contract
│   └── provisioning-portal.md    # HTTP setup-page + POST contract
├── checklists/
│   └── requirements.md  # spec quality checklist (from /speckit-specify)
└── tasks.md             # Phase 2 — /speckit-tasks output (NOT created here)
```

### Source Code (repository root)

```text
firmware/
├── components/
│   ├── interfaces/include/interfaces/
│   │   ├── IWifiDriver.h              # NEW — pure driver seam (STA/AP control + event poll), no IDF
│   │   └── (IConfigStore.h, ITimeProvider.h — reused unchanged)
│   ├── network/                       # NEW component
│   │   ├── CMakeLists.txt             # linux-guarded: pure sources on host, +hardware on target
│   │   ├── Kconfig.projbuild? (see note)   # or options live in main/Kconfig.projbuild (chosen: main/)
│   │   ├── include/network/
│   │   │   ├── WifiManager.h          # pure state machine (STA connect + reconnect schedule)
│   │   │   ├── WifiBootMode.h         # pure decideBootMode(credsPresent, buttonHeld)
│   │   │   ├── WifiCredentialValidation.h  # pure validate(ssid,password)
│   │   │   ├── EspWifiDriver.h        # thin IWifiDriver impl (target-only)
│   │   │   ├── ProvisioningPortal.h   # standalone esp_http_server (target-only)
│   │   │   └── testing/MockWifiDriver.h     # host mock (scriptable events)
│   │   └── src/
│   │       ├── WifiManager.cpp        # pure (host + target)
│   │       ├── EspWifiDriver.cpp      # target-only (esp_wifi/esp_netif/esp_event)
│   │       └── ProvisioningPortal.cpp # target-only (esp_http_server + setup page asset)
│   └── board/include/board/board.h    # reused: BOARD_PIN_STATUS_LED, BOARD_PIN_BTN_CONFIG
├── main/
│   ├── app_main.cpp                   # EDIT — add esp_netif_init + esp_event_loop_create_default,
│   │                                  #        construct driver+manager, start wifi_task
│   ├── wifi_task.cpp / .h             # NEW — task wrapper (mirrors sensor_task.cpp idiom)
│   └── Kconfig.projbuild              # EDIT — add CONFIG_WS_PROV_* options
└── test_apps/host/main/
    ├── test_wifi.cpp                  # NEW — Unity suite: reconnect schedule, boot mode, validation
    ├── test_main.cpp                  # EDIT — declare + call run_wifi_tests()
    └── CMakeLists.txt                 # EDIT — add test_wifi.cpp to SRCS, `network` to REQUIRES
```

**Structure Decision**: A new `network` component holds the WiFi logic and its hardware drivers,
following the established `sensors`/`actuators`/`storage` component shape (pure sources compiled on both
targets; `esp_wifi`/`esp_netif`/`esp_event`/`esp_http_server` sources compiled only when
`IDF_TARGET != linux`, with those IDF deps in `PRIV_REQUIRES`). `IWifiDriver` goes in the existing
`interfaces` component so the pure `WifiManager` never sees an IDF header, exactly as `IModbusClient`
and `II2cBus` do. Kconfig options are added to `main/Kconfig.projbuild` (the existing `menu
"WateringSystem"`) to match how `CONFIG_WS_*` options are declared today, rather than adding a component
`Kconfig`. The task wrapper lives in `main/wifi_task.cpp` mirroring `main/sensor_task.cpp`.

## Complexity Tracking

> No constitution violations. Table intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| —         | —          | —                                    |
