# Feature Specification: Frontend served from littlefs (010)

**PR:** PR-10 (`docs/prd/PR-10-frontend-littlefs-assets.md`) — Phase 3 web, completes the phase-3 exit criteria.
**Branch:** `010-frontend-littlefs-assets` (worktree, off origin/main @ b6d60bb).
**Depends on:** PR-09 (frozen `/api/v1/` contract), PR-06 (littlefs mount). Both merged.

## Overview

Serve the existing web frontend from the device's littlefs partition, gzip-compressed, over the same
`esp_http_server` that already exposes `/api/v1/`. The frontend is adapted **minimally** to the frozen
`/api/v1/` contract and acts as the end-to-end test client that proves the API works from a real browser on
the LAN. No redesign, no framework adoption — full frontend modernization is a separate PRD ("Ingår EJ").

## Clarifications

### Session 2026-07-06

- Q: The legacy `data/` frontend pulls Tailwind + Chart.js from public CDNs — how to handle offline/weak-WiFi
  greenhouse clients? → A: **Vendor them locally** into the gzipped littlefs image so the dashboard works
  with no internet at the client. Chart.js + `chartjs-adapter-date-fns` shipped as pinned static files; a
  pre-built/purged Tailwind CSS shipped as a committed vendored asset (the ESP-IDF build only gzips it — no
  node/Tailwind toolchain added to CI, keeping the build reproducible, Constitution III). Regeneration of the
  purged Tailwind CSS is a documented, manual step, not a CI dependency.
- Q: v1 has no reservoir enable/disable or auto-level-control endpoint (legacy `/reservoir` verbs) — what does
  the reservoir UI become? → A: reduce it to **manual start/stop + level display** (no contract change);
  auto-fill is owned by `ReservoirController`/config. Hidden entirely on single-pump (rev2) boards.
- Q: `data/wifi_setup.html` targets `/api/wifi/*` which do not exist in v1 (provisioning is the SoftAP portal)
  → A: **exclude it** from the served asset set.
- Q: `data/` is frozen legacy — where do the adapted copies live? → A: a **new source location under
  `firmware/`** (never edit `data/`); only truly-unchanged assets are copied verbatim.

## User Scenarios & Testing

### User Story 1 — Load the dashboard from a browser on the LAN (Priority: P1) 🎯 MVP

An operator on the greenhouse LAN opens the device IP in a browser (laptop or phone) and sees the dashboard
served from the device itself: live environmental readings, pump state, system status.

**Why P1:** proves assets serve from littlefs with correct gzip/content-type and that the frozen API renders
end to end — the core deliverable and the phase-3 exit gate.

**Independent test:** flash the rev1 rig, browse to `http://<ip>/`, confirm the page loads (HTTP 200,
`Content-Encoding: gzip`, correct MIME) and populates live sensor/status values from `/api/v1/`.

**Acceptance:**
1. `GET /` returns `index.html` (gzipped) with `Content-Type: text/html` + `Content-Encoding: gzip`; the page renders.
2. `script.js`, `styles.css`, favicon load with correct content types and gzip encoding.
3. The dashboard shows live environmental values (temp/humidity/pressure) from `GET /api/v1/sensors`, refreshing on the existing poll cadence.
4. Soil section tolerates `valid:false`/null (until PR-11's reader lands) without breaking the page.
5. System status (mode, wifi ip, uptime, storage, firmware) renders from `GET /api/v1/status` (+ `/config`, `/pumps` as needed).
6. Loads on a phone browser.

### User Story 2 — Control pumps and mode from the UI (Priority: P2)

The operator starts/stops the plant pump, runs a timed watering, and switches automatic/manual mode from the UI.

**Independent test:** from the browser, start a timed plant run and stop it; toggle mode; observe the device react.

**Acceptance:**
1. Timed plant run via `POST /api/v1/pumps/plant {action:"run",durationS:n}` (1..300); pump runs; UI reflects running state from `GET /api/v1/pumps`.
2. Stop via `POST /api/v1/pumps/plant {action:"stop"}`; idempotent.
3. Mode switch automatic/manual via `POST /api/v1/config {wateringEnabled:bool}`; UI reflects `status.mode`.
4. Reservoir UI (rev1 only) offers manual start/stop + shows the two level marks; it is hidden when `GET /api/v1/pumps` has no `reservoir` entry (rev2).
5. A 409 (start on a running pump) or 4xx (bad input) surfaces a readable error in the UI, no crash.

### User Story 3 — Configuration persists across reboot (Priority: P2)

The operator edits thresholds/durations/intervals in the config page; the values persist across a device reboot.

**Independent test:** set config in the UI, reboot the device, reload the page — the new values are shown.

**Acceptance:**
1. Config page loads current values from `GET /api/v1/config`.
2. Saving posts `POST /api/v1/config` (JSON) with the v1 field names/units (`wateringDurationS`, `minWateringIntervalS` in seconds); out-of-range values are rejected with a readable 4xx message.
3. After reboot the saved values are still shown (persisted via `IConfigStore`/NVS).

### Edge cases

- Unknown non-API path (`GET /nope`) → a 404 (small HTML or the JSON envelope); unmatched `/api/v1/*` still returns the JSON 404 envelope.
- Path traversal (`GET /../secret`) → rejected, never serves outside the asset root.
- A missing asset file on the volume → 404, no crash/hang.
- History chart requested for `soil_*` while soil is `valid:false` → empty series handled gracefully.
- Client without `Accept-Encoding: gzip` → out of scope to fully support (all real browsers send it); document the assumption.
- rev2 (single-pump): reservoir UI absent; no other functional difference.

## Requirements

### Functional

- **FR-001** The device MUST serve the frontend static assets (`index.html`, `script.js`, `styles.css`, favicon) from the littlefs `storage` partition over the existing `esp_http_server`, without a second server instance/port.
- **FR-002** Served assets MUST be stored gzip-compressed and returned with `Content-Encoding: gzip` and a correct `Content-Type` per extension (`text/html`, `application/javascript`, `text/css`, `image/x-icon`).
- **FR-003** `GET /` MUST return the dashboard (`index.html`). Exact `/api/v1/*` routes MUST continue to match first; the static handler serves other GET paths; unmatched `/api/v1/*` MUST still return the JSON 404 envelope.
- **FR-004** Asset requests MUST reject path traversal and only serve files under the asset root; a missing file returns 404 without crashing or hanging the server.
- **FR-005** The gzipped assets MUST be produced at BUILD time (not committed pre-gzipped) and packed into the littlefs image via the standard `idf.py` flow; `data/` MUST NOT be modified (frozen legacy) — adapted files live in a new `firmware/` source location, unchanged files may be copied verbatim.
- **FR-006** The adapted JS MUST call the frozen `/api/v1/` endpoints with the correct methods, JSON bodies, and field names/units, consuming the frozen response shapes: sensors, status, config (get/set), pumps (enumerate/run/stop), history (`metric`+`range`), events. It MUST NOT require any change to `docs/api/openapi.yaml`.
- **FR-007** The dashboard MUST provide the parity UI functions: live sensor readings, pump manual control + mode switch, configuration page, system status, and a history chart. Plus a placeholder OTA upload page wired up later in PR-13.
- **FR-008** The reservoir UI MUST be reduced to manual start/stop + level display (no enable/disable or auto-level control — those endpoints do not exist in v1) and MUST be hidden on boards without a reservoir pump (rev2, `GET /api/v1/pumps` lacks `reservoir`).
- **FR-009** The UI MUST tolerate `valid:false`/null sensor sections (soil until PR-11; power on rev1) without breaking, showing a placeholder rather than erroring.
- **FR-010** `data/wifi_setup.html` MUST be excluded from the served set (its `/api/wifi/*` endpoints do not exist in v1).
- **FR-011** The dashboard's third-party assets MUST be vendored locally (no client-side internet dependency): Chart.js + `chartjs-adapter-date-fns` as pinned static files and a pre-built/purged Tailwind CSS as a committed vendored asset, all served gzipped from littlefs. The adapted `index.html` MUST reference these local paths instead of the CDN URLs. The purged Tailwind CSS is regenerated by a documented manual step (not a CI build dependency); the ESP-IDF build only gzips the committed files.
- **FR-012** The static file server MUST run on the httpd task only, share no mutex with the watering loop, and MUST NOT be watchdog-subscribed (FR-015 isolation) — same lifecycle as the API (started lazily on the first WiFi `Connected`).

### Non-functional / constraints

- Both board targets (rev1, rev2) MUST build; the littlefs image with gzipped assets MUST build in CI for both and report a size comfortably within the 960 KiB `storage` partition.
- Assets MUST coexist with runtime-generated history/event files on the same volume (they are not seed-collision).
- English only; frozen legacy untouched; frozen `/api/v1/` contract unchanged.

### Key entities

- **Asset** — a static file (path, content-type, gzipped bytes) served from `/storage/<name>.gz`.
- **Asset route** — the mapping from a request path to an asset file (`/` → `index.html`).

## Success Criteria

- **SC-001** [CI] The littlefs image including gzipped assets builds for rev1 and rev2; the reported image size is well within 960 KiB.
- **SC-002** [HIL] A LAN browser loads the dashboard from the rig; live sensor values update.
- **SC-003** [HIL] Pump start/stop + mode switch work from the UI and the device reacts.
- **SC-004** [HIL] Configuration changed in the UI persists across a reboot.
- **SC-005** [HIL] The page loads on a phone browser.
- **SC-006** Both boards build; the frozen `/api/v1/` contract and `data/` are unchanged.

## Assumptions

- All target browsers send `Accept-Encoding: gzip` (universally true); non-gzip clients are not a supported case.
- The adapted `script.js`/`index.html` become the source of truth for the frontend under `firmware/`; `data/` remains the historical Arduino reference only.
- Reservoir auto-fill remains controller/config-owned (PR-11); the UI does not attempt to drive it.

## Out of scope

- New UI features, visual refresh, framework adoption (separate PRD).
- Any `/api/v1/` contract change (frozen; a genuine contract bug needs an explicit PR-09 amendment).
- OTA execution (PR-13) — only a placeholder upload page is wired here.

## Open questions

- ~~O1: CDN vs vendored vs drop for Tailwind + Chart.js~~ — **RESOLVED (clarify 2026-07-06): vendor locally**
  (see FR-011). No open questions remain.
