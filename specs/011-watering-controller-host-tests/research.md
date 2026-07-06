# Phase 0 Research: Watering Controller

Grounded in the verified codebase map (origin/main, PR-01..09 merged), the legacy behavior
(`src/WateringController.cpp`, `src/main.cpp` — frozen reference), `docs/prd/PR-11-*.md`, and
`docs/parity-checklist.md` §1–§3. No open `NEEDS CLARIFICATION`.

## D1 — pure `control` component (WateringController + ReservoirController)

- **Decision**: New `control` component with two pure classes over interfaces + injected clock; 100 %
  host-tested. `ReservoirController` is a separate pure state machine so it is tested independently of the
  board that wires the pump.
- **Rationale**: master-PRD success criterion (100 % host-tested watering logic); mirrors the established
  pure-logic pattern (WifiManager, DebouncedLevelSensor). No IDF includes → linux-buildable.
- **Alternatives**: one monolithic controller incl. reservoir — rejected (reservoir logic is board-gated;
  separating it keeps each state machine independently testable).

## D2 — soak gate: enforce min-interval, measured from burst END

- **Decision**: Automatic mode enforces the minimum-watering-interval as a soak pause: after a burst ENDS,
  no new automatic burst starts until `soakPause` has elapsed, even if soil still reads ≤ low. Burst
  duration + soak length are runtime config (`getWateringDurationS`/`getMinWateringIntervalS`, defaults
  20 s / 300 s). Loop: burst → soak pause → re-evaluate → repeat until moisture ≥ high.
- **Rationale**: resolved by Paul 2026-06-10 — deliberate divergence (legacy comment `WateringController.cpp:303`
  "water immediately", never enforced). Pause from burst END models absorption (water pools if poured
  continuously while the sensor lags). Values tuned empirically later — the behavior is fixed here.
- **Alternatives**: measure from burst START (legacy `lastWateringTime = millis()` at start) — rejected: if
  the pause < burst duration there is no effective soak; from-end is the intended absorption semantics.

## D3 — fail-safe precedence: safety stop is unconditional, never gated by soak

- **Decision**: On every automatic evaluation, check fail-safe FIRST: soil unavailable, OR data stale
  (> 30 000 ms since last valid read, or never), OR moisture outside 0–100 → emergency-stop the pump and
  take no watering decision. This runs regardless of the soak pause or any scheduling state. Manual mode
  (operator override) is exempt (FR-007).
- **Rationale**: Constitution I; parity §2 (`WateringController.cpp:229-301`). FR-006 "a safety stop must
  not be delayed by the gate" — host-tested with a pending soak pause + a fail-safe condition.
- **Alternatives**: evaluating the gate before safety — rejected (would delay a safety stop).

## D4 — "manual" is the mode/enabled flag, not a pump flag

- **Decision**: The new `IWaterPump` has no `isManualMode()`. Express mode via the controller's own state
  driven by `IConfigStore::getWateringEnabled()` (+ who started a run): automatic = enabled and controller-
  started (subject to fail-safe); manual = an operator-started run (bypass), and automatic evaluation is
  suspended while `wateringEnabled==false`. A stop always clears the override (FR-010).
- **Rationale**: research §5 — legacy guarded fail-safes on `isManualMode()` which no longer exists; QUIRK 1
  requires auto-started = automatic (the legacy inverts by flagging every timed run manual). Manual runs
  are capped at 300 s by the pump's own `runFor` cap (deliberate divergence; legacy allowed uncapped).
- **Alternatives**: add `isManualMode()` back to the pump — rejected (mode is controller/config policy, not
  pump state; keeps the pump a dumb, safe actuator).

## D5 — reservoir truth table (compose two ILevelSensor; add the invalid row)

- **Decision**: `ReservoirController` composes the two marks (each: valid + wet/dry). Auto control, only
  when enabled and pump not running:

  | low mark | high mark | action |
  |---|---|---|
  | invalid OR high invalid | (either invalid) | **do not act** (never treat invalid as dry) |
  | wet | wet | full → ensure stopped |
  | wet | dry | sufficient → no action |
  | dry | dry | **start fill** |
  | dry | wet | physically implausible → **no action** |

  While filling (manual or auto): stop when high mark reads wet; abort at 300 s max fill (the pump's own
  cap). Feature disabled/absent → force pump off, skip logic.
- **Rationale**: research §5 legacy truth table (`src/main.cpp:533-550`) + the `ILevelSensor` "invalid =
  do-not-act" contract (the fifth row the legacy lacks). 300 s cap = the pump's `runFor(cap)`/max-runtime.
- **Alternatives**: numeric level — N/A (marks are boolean). Aggregating in the sensor layer — rejected
  (`ILevelSensor` never aggregates; the controller composes).

## D6 — snapshot helpers on the locked wrappers (resolve TODO(PR-11)); soil-mock coherence

- **Decision**: Add a consistent-snapshot helper (one locked call returning validity + values) to
  `LockedSoilSensor`, `LockedEnvironmentalSensor`, `LockedLevelSensor` (the booked TODOs; soil has the gap
  but no stub). The controller/periodic-reader use these to avoid the documented read()-then-getters
  two-lock cross-call race. Add coherent `scriptSuccessfulRead/scriptFailedRead` to `MockSoilSensor`
  (mirroring `MockEnvironmentalSensor`), so tests can't script incoherent soil state.
- **Rationale**: research §1/§2 — the periodic soil reader + controller become cross-task readers; the
  cross-call gap becomes a real torn read without a snapshot helper.
- **Alternatives**: leave the gap — rejected (torn reads once the reader lands).

## D7 — periodic soil reader; controller task; API mode handoff

- **Decision**: Add a periodic soil read (a small `soil_reader_task`, or extend `sensor_task` to also read
  soil) at the configured sensor interval, refreshing the locked soil snapshot — this also makes PR-09
  `/sensors` soil `valid`. The `WateringController` runs on its own watchdog-registered task (mirroring
  `sensor_task`: `watchdog_subscribe_current_task()` + `watchdog_feed()` each cycle), reading
  `getWateringEnabled()` to decide automatic vs. suspended — no direct ApiServer↔controller call path.
- **Rationale**: research §3/§4 — no periodic soil reader exists; the API only flips the config flag.
  Watchdog policy (PR-08): watering-critical tasks subscribe.
- **Alternatives**: fold the controller into the existing 10 Hz main loop — acceptable, but a dedicated
  task keeps cadence/責任 clean and mirrors sensor_task; decide at implementation (either is watchdog-fed).

## D8 — fix the rs485test race; coordinate event logging

- **Decision**: Route the diag-console `rs485test` through a locked path (via the sensor or a locked
  client) so it no longer drives the raw `EspModbusClient` beneath `LockedSoilSensor`'s mutex once the
  periodic reader runs. The controller logs fail-safe events (`EventLogger::logFailsafe`, "producer is
  PR-11"); pump start/stop transitions stay owned by `SystemObserver` to avoid double-logging.
- **Rationale**: research §2/§6 — the rs485test raw path becomes a real RS485 bus race with a concurrent
  reader; `SystemObserver` already edge-logs pump transitions.

## D9 — data logging + time-not-set

- **Decision**: Log env + soil to `IDataStorage` every `dataLogInterval` (default 5 min) with
  `IWallClock::nowEpoch()`; NPK only when ≥ 0; gate a "valid timestamp" on `isTimeSet()` (pre-SNTP → do not
  present a bogus 1970 as valid). Automatic watering itself does NOT depend on wall-clock (soil-driven), so
  it runs offline.
- **Rationale**: parity §1 (`WateringController.cpp:332-363`) + PR-08 time-not-set contract.

## Open risks

- **R1** — soak/threshold values are placeholders (tuned empirically on the rig); the spec fixes behavior,
  not the numbers (config-driven).
- **R2** — controller-task vs main-loop placement + soil-reader-task vs sensor_task-extension are the two
  structural choices left to implementation; both must stay watchdog-fed and isolated from watering-blocking.
- **R3** — coordinating fail-safe event logging with `SystemObserver`'s pump-transition logging (avoid
  double-log); verify at review/HIL.
