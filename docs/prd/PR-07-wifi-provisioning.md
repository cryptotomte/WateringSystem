# PR-07: wifi-provisioning

> Phase 2 — infrastructure

## Goal

WiFi station management with robust reconnection, plus first-boot provisioning,
implementing the FR9 baseline decision (custom SoftAP portal).

## Scope

- **FR9 decision (RESOLVED by Paul, 2026-06-10): custom SoftAP portal** — port the
  existing concept (AP at 192.168.4.1 with config page). Rationale: feature parity,
  works from any phone browser, no dependency on Espressif's provisioning apps.
  IDF's `wifi_provisioning` component is the documented fallback only if the portal
  proves fragile during implementation (re-escalate to Paul in that case).
  - **AP password policy:** moved from a source `#define` to a Kconfig option
    (e.g. `CONFIG_WS_PROV_AP_PASSWORD`), treated as a documented non-secret (the
    repo and release binaries are public; the password only shields the short
    unconfigured-device window). Keep WPA2 rather than an open AP so the real
    home-WiFi credentials are not sent in cleartext during setup. Choose a NEW
    password at implementation — the legacy one is exposed in public git history
    and must not be reused.
- `WifiManager` on `esp_wifi`/`esp_event`:
  - STA mode with credentials from NVS (PR-06).
  - Reconnection strategy. Actual Arduino behavior (parity baseline) is **not**
    exponential backoff: fixed 10 s reconnect interval, plus a 60 s pause after 5
    failed attempts (`docs/parity-checklist.md` §7; the "exponential backoff" claim
    in older docs is a myth from a code comment). If exponential backoff is chosen
    instead, mark it as a deliberate change. Either way reconnection never blocks
    or affects watering tasks — WiFi loss must not influence watering safety
    (master PRD reliability requirement).
  - Connection state exposed for status reporting/LED.
- Provisioning entry conditions (parity): unconfigured device boots into provisioning
  mode; config-button-held-at-boot forces emergency provisioning mode.
- Credential storage in NVS; system restart after (re)configuration, as today.
- mDNS or fixed-hostname niceties only if free with the chosen component (do not
  gold-plate).

## Out of scope

- HTTP API (PR-09) — the portal uses a minimal standalone
  `esp_http_server` instance, with the real API arriving in PR-09. SNTP (PR-08).
  Frontend (PR-10).

## Functional requirements covered

- FR9 (provisioning, incl. the open-question decision); reliability NFR on WiFi
  reconnection.

## Dependencies

- PR-06 (NVS credential storage).

## Acceptance criteria

- [CI] Both targets build; provisioning choice documented in `specs/NNN-*/` and
  referenced from `firmware/CLAUDE.md`.
- [HIL] Fresh device (no credentials) enters provisioning mode; phone can provision
  real credentials; device joins the LAN.
- [HIL] AP power-cycle test: rig reconnects automatically with backoff; watering
  tasks (sensor poll, pump control loop) keep running throughout the outage.
- [HIL] Config-button-at-boot forces provisioning mode on an already-configured
  device.
- [HIL] Wrong-password case: device keeps retrying with backoff and remains
  provisionable, no boot loop.

## Estimated size

L
