# HIL Checklist: Watering Controller (011) — rev1 bench rig

**Purpose**: hardware-in-the-loop smoke of PR-11 at Checkpoint 3 (Paul, bench rig). The merge-gating
deliverable is the 100 % host-tested watering logic (289/0 in CI); this HIL is a confidence smoke that the
wired-up controller behaves on real hardware.
**Rig**: ESP32 devkit (rev1) provisioned with valid home-WiFi credentials (station mode, so `/api/v1/` binds
and the flag can be driven remotely); BME280 on I2C, RS485 soil sensor, plant + reservoir pumps, and the two
XKC-Y26 level sensors as wired for the previous features. A laptop/phone on the same LAN with `curl`, plus a
serial console (`ws>`, 115200) for the RS485 pull and event-log checks.
**Build**: rev1 target (`sdkconfig.board.rev1_devkit`), flash per `firmware/CLAUDE.md`. Confirm the serial
log shows `watering task started (... ms cadence)` and `sensor task started` after boot.
**Reference**: acceptance criteria `docs/prd/PR-11-*.md`; spec FR-001..FR-017 (esp. FR-003 soak, FR-005/006
fail-safe, FR-012a cooldown, FR-014 logging, FR-017 isolation); `quickstart.md` §HIL; `data-model.md`
decision tables; parity `docs/parity-checklist.md`.
**Setup**: note the device IP (console `wifi` / router / `GET /api/v1/status`). Export it, e.g.
`export DEV=192.168.1.50`, and prefix curls with `http://$DEV/api/v1/...`. Set short thresholds/intervals via
`POST /api/v1/config` (or the `config` console command) so a full cycle is observable in minutes:
a low soak (`minWateringIntervalS`), a short burst (`wateringDurationS`), a low sensor interval
(`sensorReadIntervalMs`), and low/high moisture thresholds bracketing the current soil reading.

**Note on cadence**: the controller ticks at `sensorReadIntervalMs` (floored at 1 s), so a decision may lag a
config/soil change by up to one interval — allow for that when timing the observations below.

## A. Live soil reader (US3, quickstart §HIL #5)

- [ ] A1. `curl http://$DEV/api/v1/sensors` now shows `soil.valid:true` with fresh, plausible values
      (moisture/temp/pH/EC and NPK when the sensor reports them) — the periodic reader is running (this is
      the PR-09 limitation being lifted)
- [ ] A2. The soil values refresh over time (poll twice ~2 intervals apart while touching the probe / moving
      it between dry and wet media — the reading tracks)
- [ ] A3. `soil.valid` returns to `false` (last-good placeholders) after the RS485 pull in D1, then recovers
      to `true` once reconnected

## B. Automatic pulsed watering + soak (US1, quickstart §HIL #1, FR-002/003)

- [ ] B1. In automatic mode (`GET /api/v1/status` → `mode:automatic`; set via `POST /api/v1/config`
      `{"wateringEnabled":true}`) with the soil below the low threshold (dry probe), the plant pump starts a
      burst of ~`wateringDurationS`
- [ ] B2. After the burst ends the pump does NOT immediately restart even while the soil still reads dry —
      it waits out the soak pause (`minWateringIntervalS`) measured from the burst END, then re-waters
- [ ] B3. When the soil rises to ≥ the high threshold (wet the probe), a running burst stops and no new burst
      starts
- [ ] B4. Change `wateringDurationS` / thresholds via `POST /api/v1/config` mid-run → the next tick/burst
      picks up the new values (runtime-tunable, no reflash)

## C. Fail-safe precedence (US1, quickstart §HIL #2, FR-005/006)

- [ ] C1. With the plant pump mid-burst in automatic mode, pull the RS485 soil cable → the pump stops within
      the staleness window (~30 s), and `/api/v1/sensors` shows `soil.valid:false`
- [ ] C2. A `reset=`/`failsafe` entry appears in `GET /api/v1/events` (or console `storage events`) with a
      detail like `soil-stale` / `soil-unavailable`
- [ ] C3. **Soak does not delay a safety stop**: trigger the pull DURING an active soak pause (pump just
      stopped, waiting to re-water) — confirm the fail-safe still fires (no watering resumes) rather than
      waiting for the soak to elapse
- [ ] C4. Reconnect the cable → after a fresh valid read the controller resumes normal automatic behavior
      (no reboot occurred — check uptime in `/status`)

## D. Manual override (US3, quickstart §HIL #3, FR-007..010)

- [ ] D1. With the RS485 cable pulled (soil failing), a manual run still executes:
      `POST /api/v1/pumps/plant {"action":"run","durationS":15}` → the plant pump runs despite the sensor
      failure (manual bypasses fail-safe)
- [ ] D2. A manual `durationS` above 300 is capped at 300 s; the pump never runs past the hard cap
- [ ] D3. `POST {"action":"stop"}` stops the manual run and clears the override; switching back to automatic
      (`wateringEnabled:true`) with a valid dry sensor then resumes automatic bursts
- [ ] D4. In manual mode (`wateringEnabled:false`) the controller takes NO automatic action (no bursts start
      on their own)

## E. Reservoir auto-fill (US2, quickstart §HIL #4, FR-012/012a)

- [ ] E1. With both level marks dry (low-dry + high-dry) in automatic mode, the reservoir fill pump starts
- [ ] E2. When the high mark goes wet, the fill pump stops immediately
- [ ] E3. **Max-fill abort**: with the high mark forced to stay dry (disconnected / empty source), the fill
      pump aborts at the 300 s cap rather than running forever
- [ ] E4. **Post-abort cooldown**: after the E3 abort, no new automatic fill starts for ~`kReservoirRefill
      CooldownMs` (~60 s) even though low still reads dry; after the cooldown a fill re-attempts. A normal
      high-wet stop (E2) does NOT impose the cooldown; a manual fill
      (`POST /api/v1/pumps/reservoir {"action":"run",...}`) starts immediately regardless of cooldown
- [ ] E5. A stuck/settling level mark (`not_yet_valid`) never triggers a fill (invalid ≠ dry)

## F. Data logging (US3, FR-014)

- [ ] F1. After the first SNTP sync (`/status` → time `synced:true`), `GET /api/v1/history?metric=env_
      temperature&range=1h` returns a growing series at roughly `dataLogIntervalMs` cadence
- [ ] F2. Soil metrics log too (`metric=soil_moisture`); `soil_humidity` is intentionally absent (identical
      to `soil_moisture`); NPK metrics appear only when the sensor reports them (≥ 0)
- [ ] F3. Before the first sync (power-cycle, no network) no bogus 1970-epoch rows are written (query shows
      nothing logged until the clock is set)

## G. Isolation (FR-017, quickstart §HIL #6)

- [ ] G1. During an active automatic watering cycle, drop WiFi (power off the AP / `wifi-clear` is too
      destructive — just move out of range or disable the AP) → the plant/reservoir behavior is UNAFFECTED
      (bursts, stops, soak continue on schedule)
- [ ] G2. Flood the API with requests (a `curl` loop against `/api/v1/sensors`) while a burst runs → watering
      timing is unaffected; the 10 Hz pump-timing stays crisp (pump stops at the burst duration, not late)

## H. RS485 race fix (T016)

- [ ] H1. While the periodic reader is active (watering task running), issue `ws> rs485test` repeatedly on
      the console → it returns clean probe results + statistics with no bus corruption, garbled frames, or
      lock-ups, and `/api/v1/sensors` soil readings stay coherent throughout (shared-mutex serialization)

## Definition of done

- [ ] HIL smoke executed on the rev1 rig at Checkpoint 3, OR deferred with rationale if no rig time is
      available (record in the deferred-HIL register alongside the earlier features' deferrals).
- [ ] Any anomaly filed and triaged; a fail-safe or isolation miss (Section C or G) blocks merge, the rest
      are logged for follow-up.

**Deferral note**: like PR-04/07/08/09, if the rig is unavailable this checklist is deferred (not skipped).
The merge is gated by the host suite (289/0) + both board builds; the HIL is the on-hardware confidence pass
Paul runs when the bench is free.
