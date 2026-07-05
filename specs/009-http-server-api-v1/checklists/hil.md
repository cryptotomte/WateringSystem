# HIL Checklist: HTTP REST/JSON API v1 (009) — rev1 bench rig

**Purpose**: hardware-in-the-loop verification of PR-09 at Checkpoint 3 (Paul, bench rig)
**Rig**: ESP32 devkit provisioned with valid home-WiFi credentials (station mode) so the API socket binds on the STA interface; BME280 on I2C, RS485 soil sensor, plant + reservoir pumps, and the two XKC-Y26 level sensors as wired for the previous features. A laptop/phone on the same LAN with `curl` (or a browser). If still unprovisioned, see PR-07 §A.
**Build**: rev1 target (`sdkconfig.board.rev1_devkit`), flash per `firmware/CLAUDE.md`. Confirm the serial log shows `API server started (/api/v1/)` after the `wifi=Connected` event.
**Reference**: acceptance criteria `docs/prd/PR-09-*.md`; spec FR-004/FR-015; `quickstart.md` §HIL; contract `docs/api/openapi.yaml`; parity `docs/parity-checklist.md`
**Setup**: note the device IP (console `wifi` / router / `GET /api/v1/status`). Export it, e.g. `export DEV=192.168.1.50`, and prefix each curl with `http://$DEV/api/v1/...`.

**Known limitation (not a bug):** `soil` and (rev2) `power` report `valid:false`
with `null` placeholders — no periodic reader lands until PR-11. Verify the
`valid` flag and shape, NOT the numeric values.

## A. Status (US1, quickstart §HIL #1)

- [ ] A1. `curl http://$DEV/api/v1/status` returns `{ "success": true, ... }`
      with `mode` (`automatic`/`manual`), `wifi` (ip/rssi/ssid/connected),
      `time`, `uptimeMs`, `resetReason`, `firmware`, `storage`
- [ ] A2. The wifi block carries NO password field (grep the body — `password`
      must not appear anywhere)
- [ ] A3. On rev1 `power` is JSON `null`; `time` reads `synced:false` before the
      first SNTP sync and the correct local time after

## B. Sensors — non-blocking (US1, quickstart §HIL #2, FR-015 / QUIRK 5)

- [ ] B1. `curl http://$DEV/api/v1/sensors` returns quickly with fresh
      `environmental` (`valid:true`) and `level` (both marks) blocks
- [ ] B2. `soil.valid` is `false` with `null` placeholder values (PR-11
      limitation), and NPK keys are absent unless the sensor reported them
- [ ] B3. Start a long console read (`ws> rs485test` or `ws> soil`) or a
      `selftest` and, WHILE it runs, hit `/api/v1/sensors` — it still returns
      fast and does NOT stall on the bus (cached getters only)

## C. Pumps (US2, quickstart §HIL #3)

- [ ] C1. `GET /api/v1/pumps` lists the board's pumps (rev1: plant + reservoir)
- [ ] C2. `curl -X POST -d '{"action":"run","durationS":15}' http://$DEV/api/v1/pumps/plant`
      → pump runs; the response reports `running:true`
- [ ] C3. The pump auto-stops at the requested duration (and never past the
      300 s hard cap); a later `GET /api/v1/pumps` shows `running:false`
- [ ] C4. `POST {"action":"stop"}` stops a running pump; stopping an
      already-stopped pump is a success no-op
- [ ] C5. `POST {"action":"run",...}` on an already-running pump returns **409**
      with an error body and does NOT restart the pump's clock
- [ ] C6. `POST /api/v1/pumps/bogus` (unknown name) returns **404** JSON error

## D. Config (US2, quickstart §HIL #4)

- [ ] D1. `GET /api/v1/config` returns the current settable fields; NO wifi
      password present
- [ ] D2. `POST` a valid change (e.g. `{"moistureThresholdLow":35}`) returns the
      new config; **reboot** and confirm it persisted
- [ ] D3. `POST` an out-of-range value (e.g. `{"wateringDurationS":9999}`)
      returns **400** naming the field, and changes NOTHING (re-GET to confirm)
- [ ] D4. `POST` a mixed valid+invalid body → all-or-nothing: 400 and no field
      applied

## E. History / events (US3, quickstart §HIL #5)

- [ ] E1. `GET '/api/v1/history?metric=env_temperature&range=24h'` returns
      aligned `timestamps[]`/`values[]` with matching `count` and an echoed
      `metric`/`start`/`end`
- [ ] E2. A metric/window with no data returns **200** with empty arrays and
      `count:0` (NOT an error)
- [ ] E3. `GET '/api/v1/history'` with no `metric` returns **400**; an unknown
      `range` (e.g. `range=99y`) returns **400**
- [ ] E4. `GET /api/v1/events` lists recent events newest-first with `epoch`,
      `category`/`categoryName`, `detail`; `?count=5` caps the list

## F. Self-test / OTA stub (US3, quickstart §HIL #6)

- [ ] F1. `curl -X POST http://$DEV/api/v1/selftest` returns
      `{ overall, checks:[{name,ok,detail}] }` with an `environmental` and a
      `soil` check (the soil check exercises the RS485 round-trip)
- [ ] F2. Unplug the soil sensor and re-run `selftest` → its check reports
      `ok:false` with a `read failed, error N` detail; `overall:false`
- [ ] F3. `curl -X POST http://$DEV/api/v1/ota` returns **501** with
      `{ "success": false, "error": "OTA not implemented" }`

## G. Robustness (US3, quickstart §HIL #7, FR-015)

- [ ] G1. `POST` malformed JSON to `/api/v1/config` or a pump route → **400**
      JSON error envelope (never a crash / HTML)
- [ ] G2. `GET /api/v1/nope` (unknown `/api/...` route) → **404** JSON error
      envelope, not the default HTML page
- [ ] G3. Start an active pump run (`POST run durationS=30`), then open a
      stalled/slow or flooding client against the API (e.g. `nc`/`curl --limit-rate`,
      or several parallel requests) → the pump timed run + self-stop and the 5 s
      sensor cadence are UNAFFECTED and no watchdog reset occurs (the server is
      not WDT-subscribed)

## H. rev2 power (US1, quickstart §HIL #8)

- [ ] H1. **rev2 only**: `GET /api/v1/power` returns
      `{ valid, busVoltage, current, power }` (last-good until PR-11)
- [ ] H2. On rev1, `GET /api/v1/power` returns the not-available shape
      (`available:false`, `power:null`) — board-capability driven

## Deferral path

If the rig or a LAN client is unavailable, defer the affected items with a
written rationale and add them to the deferred-HIL register (mirroring the
handover's deferred-HIL pattern, e.g. PR-04's `hil-004`). The host-test suite
(`test_api_serialize` / `test_api_requests` / `test_api_routes`) already covers
the serializers, parsers/validators and the route-table contract
deterministically; the items above are the on-device confirmations — real HTTP
round-trips, the non-blocking guarantee under bus contention, persistence across
reboot, and the stalled-client watering-isolation measurement. **Note the
soil/power staleness limitation (PR-11):** those blocks are shape-verified only,
not value-verified, until the periodic readers land.

**Sign-off**: date + result per item as a PR comment (pattern from PR #7).
