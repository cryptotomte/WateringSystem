# PR-07: wifi-provisioning

> Phase 2 — infrastructure

## Goal

WiFi station management with robust reconnection, plus first-boot provisioning — this
PR contains the FR9 decision spike (custom SoftAP portal vs IDF `wifi_provisioning`
component) and documents the choice.

## Scope

- **FR9 decision spike (documented deliverable):** evaluate (a) porting the existing
  SoftAP portal concept (AP at 192.168.4.1 with config page) vs (b) IDF's
  `wifi_provisioning` component (SoftAP scheme). Criteria per master PRD: robustness
  and simplicity. Write the decision + rationale into the spec; implement the winner.
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

- HTTP API (PR-09) — if the spike picks a custom portal, it uses a minimal standalone
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
