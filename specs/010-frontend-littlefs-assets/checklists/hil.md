# HIL Checklist: Frontend from littlefs (010) — rev1 bench rig

**Purpose**: hardware-in-the-loop smoke of PR-10 at Checkpoint 3 (Paul, bench rig). The merge gate is the CI
(pure `ApiStatic` host tests + both board builds + the littlefs image building with the gz assets); this HIL
proves the dashboard actually loads and drives the device from a real browser.
**Rig**: rev1 devkit provisioned with valid home-WiFi (station mode, so `/api/v1/` + the assets bind on the STA
interface); BME280, RS485 soil sensor, plant + reservoir pumps, the two level sensors as wired before. A
laptop AND a phone on the same LAN with a browser.
**Build/flash**: rev1 target, `idf.py flash` (FLASH_IN_PROJECT writes the littlefs image with the assets). Serial
shows `API server started (/api/v1/)` after `wifi=Connected`.
**Setup**: note the device IP (`GET /api/v1/status`, router, or console `wifi`). Browse to `http://<ip>/`.

**Offline check is the point of the vendoring decision**: for §F, put a router between the client and the
device that has NO uplink to the internet (or disable the client's mobile data / use airplane-mode-plus-WiFi)
so the CDN would be unreachable — the dashboard must still render fully from the vendored assets.

## A. Assets load from littlefs (SC-002)

- [ ] A1. `http://<ip>/` returns the dashboard (HTTP 200); the page renders (layout, not raw HTML).
- [ ] A2. In devtools Network: `index.html`, `script.js`, `styles.css`, `vendor/tailwind.css`,
      `vendor/chart.min.js`, `vendor/chartjs-adapter-date-fns.min.js` all load 200 with
      `Content-Encoding: gzip` and a sensible `Content-Type`.
- [ ] A3. `GET http://<ip>/nope.js` (missing asset) returns a 404 with the JSON error envelope, no hang.
- [ ] A4. `GET http://<ip>/api/v1/status` still returns the JSON status (the API routes were not shadowed by
      the static catch-all).

## B. Live dashboard (SC-002)

- [ ] B1. Environmental temp/humidity/pressure show live values and refresh on the poll cadence.
- [ ] B2. Soil section shows placeholders (`--`) while soil is `valid:false` (pre-PR-11) and does NOT break the
      page or the console (no unhandled errors in devtools).
- [ ] B3. System status shows mode, wifi IP, uptime, firmware, storage (KB) from `/status` + `/config`.
- [ ] B4. The reservoir level display shows Full/OK/Low/Settling from the two marks (rev1).

## C. Pump + mode control (SC-003)

- [ ] C1. Start a timed plant run from the UI → the plant pump runs; the running indicator reflects it
      (driven by `/pumps`).
- [ ] C2. Stop from the UI → the pump stops.
- [ ] C3. Starting a run while already running surfaces the 409 "already running" message (no crash).
- [ ] C4. Toggle automatic/manual → `status.mode` follows and the indicator updates.
- [ ] C5. Reservoir manual start/stop works (rev1); the enable/disable + auto-level controls are absent/hidden.

## D. Config persistence (SC-004)

- [ ] D1. The config page loads current thresholds/durations/interval (seconds) from `/config`.
- [ ] D2. Save a changed value → accepted; an out-of-range value shows the 4xx `error` message, no crash.
- [ ] D3. Reboot the device, reload the page → the saved values are still shown (persisted via NVS).

## E. History + phone + OTA

- [ ] E1. Pick a metric + range → the chart renders from `/history?metric=&range=` (empty series → empty chart,
      not an error). Chart.js loads from the vendored file.
- [ ] E2. (SC-005) The page loads and is usable on a phone browser.
- [ ] E3. The OTA button posts `/api/v1/ota` and shows the "not available yet (PR-13)" 501 message cleanly.

## F. Offline render (the vendoring decision)

- [ ] F1. With the client on the LAN but WITHOUT internet access, reload `http://<ip>/` → the dashboard still
      renders fully (Tailwind styling + Chart.js) from the vendored assets — no blank/broken layout, no CDN
      requests in the Network tab.

## G. rev2 note (when a rev2 unit is available — deferred to PR-14)

- [ ] G1. On a single-pump (rev2) build the reservoir section is hidden (no `reservoir` in `/pumps`) and the
      rest of the dashboard is unaffected.

## Definition of done

- [ ] HIL smoke executed on the rev1 rig at Checkpoint 3, OR deferred with rationale if no rig time (record in
      the deferred-HIL register alongside the earlier features').
- [ ] The merge is gated by CI (host `ApiStatic` tests + both board builds + the littlefs image); this HIL is
      the on-hardware confidence pass. A broken page load (§A/§B) or a failed control action (§C) blocks merge;
      the rest are logged for follow-up.
