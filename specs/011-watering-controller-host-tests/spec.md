# Feature Specification: Watering Controller (host-tested application logic)

**Feature Branch**: `011-watering-controller-host-tests`

**Created**: 2026-07-05

**Status**: Draft

**Input**: PR-11 (`docs/prd/PR-11-watering-controller-host-tests.md`) — port `WateringController` (all
watering logic, scheduling, reservoir state machine, fail-safe) to pure interface-based C++ with **100 % of
watering logic + safety conditions under host tests in CI**. Behavior ground truth:
`docs/parity-checklist.md` §1–§3. Depends on PR-02..05 (interfaces + mocks + drivers), PR-06 (config/
storage), PR-08 (time source + watchdog). Deliberate divergences from the frozen Arduino code are called out
below (soak gate, manual cap, mode semantics).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Safe, pulsed automatic watering (Priority: P1)

In automatic mode the system waters the plant when the soil is dry, in **bursts with an enforced soak
pause** so the water can absorb, stops when the soil reaches the high threshold, and — the safety-critical
part — **immediately stops and refuses to water on invalid, stale, or missing sensor data**. Every one of
these decisions is provable in CI without hardware.

**Why this priority**: This is the heart of the product and the master-PRD success criterion ("watering
logic and safety conditions 100 % host-tested"). The fail-safe behavior is non-negotiable (Constitution I);
the pulsed soak gate is the deliberate correctness improvement over the legacy "water immediately" bug.

**Independent Test**: Drive the pure controller over mock sensors/pump + a fake clock: assert it starts at
≤ low threshold, respects the soak pause, stops at ≥ high threshold, and emergency-stops on
unavailable/stale/invalid soil data — all as host tests.

**Acceptance Scenarios**:

1. **Given** automatic mode, pump stopped, valid moisture ≤ the low threshold, **When** the controller
   evaluates, **Then** it starts a watering burst for the configured duration.
2. **Given** a burst just finished, **When** the soil still reads dry but the soak pause has not elapsed,
   **Then** the controller does NOT start another burst (enforced soak/absorption pause).
3. **Given** the soak pause has elapsed and moisture is still ≤ low, **When** the controller evaluates,
   **Then** it starts the next burst; this repeats until moisture reaches the high threshold.
4. **Given** the pump is running and moisture rises to ≥ the high threshold, **When** the controller
   evaluates, **Then** it stops the pump.
5. **Given** automatic mode with the pump running, **When** the soil sensor becomes unavailable, or its
   data is stale beyond the staleness window, or the moisture reading is outside the valid range, **Then**
   the controller emergency-stops the pump and takes no watering decision (fail-safe, FR4).
6. **Given** a pending soak pause, **When** a fail-safe stop condition arises, **Then** the safety stop is
   applied immediately and is NEVER delayed by the soak gate.
7. **Given** the soil sensor has not yet produced a first successful reading, **When** the controller
   evaluates, **Then** it does not act on the placeholder values (gates on the read result, not the value).

---

### User Story 2 - Reservoir auto-fill state machine (Priority: P2)

On a board with a local refill pump the system keeps the reservoir topped up: it starts filling when the
water is low, stops when the high mark is reached, aborts at a hard maximum fill time as a safety net, and
does nothing on a physically impossible sensor combination. The whole state machine is host-tested even
though only one board wires the pump.

**Why this priority**: The second safety-critical control domain (FR5). Board-independent logic gated by a
capability flag, so it is fully host-tested regardless of which board runs it.

**Independent Test**: Drive the pure reservoir logic over two mock level sensors + a mock pump + fake
clock; assert every row of the level truth table, the high-mark stop, the max-fill-time abort, and the
"sensor invalid → do not act" and "physically impossible → do not act" rows.

**Acceptance Scenarios**:

1. **Given** auto level control and the pump not running, **When** both marks read dry (low water), **Then**
   the fill pump starts.
2. **Given** the fill pump running, **When** the high mark reads wet, **Then** the pump stops immediately.
3. **Given** the fill pump running, **When** the high mark never trips, **Then** the pump is aborted at the
   hard maximum fill time.
4. **Given** an implausible combination (low mark dry while high mark wet), **When** evaluated, **Then** no
   action is taken.
5. **Given** either level sensor is not-yet-valid/invalid, **When** evaluated, **Then** no action is taken
   (invalid is never treated as "water absent").
6. **Given** the reservoir feature is disabled, **When** evaluated, **Then** the pump is forced off and all
   reservoir control is skipped.

---

### User Story 3 - Manual override, logging, and on-target integration (Priority: P3)

An operator can run a pump manually as an explicit override that bypasses the automatic safety stops; the
system logs important events and sensor history with correct timestamps (and behaves sanely before the
clock is set); and the controller runs on the device, driven by the mode flag, replacing the interim
direct-drive path — including a periodic soil read so live soil data and automatic decisions actually work.

**Why this priority**: Completes the feature on-target: manual control, observability, and the wiring that
makes the host-tested logic run in the real system. Lower risk than the core logic but required for a
usable, releasable controller.

**Independent Test**: Manual run continues despite a sensor failure (host); an automatic run stops on the
same failure (host); data-log entries carry epoch timestamps and are gated on time-set (host); on the rig,
flipping the mode drives automatic watering and pulling the RS485 cable stops the pump within the staleness
window (HIL).

**Acceptance Scenarios**:

1. **Given** a manual run (explicit operator override), **When** a sensor fails or reports stale/invalid
   data, **Then** the pump keeps running (manual bypasses the automatic fail-safe stops).
2. **Given** any mode, **When** stop is commanded, **Then** the pump stops and the override is cleared.
3. **Given** the data-log interval elapses, **When** the controller logs, **Then** it records the
   environmental + soil readings (NPK only when valid/non-negative) with an epoch timestamp; **when** the
   clock is not yet set, it does not fabricate a valid timestamp.
4. **Given** the device running, **When** the mode flag is automatic, **Then** the controller performs
   automatic watering; **when** manual, automatic watering is suspended (operator override only).
5. **Given** the controller task, **When** it runs, **Then** it is registered with the hardware watchdog and
   never blocks or is blocked by network/HTTP activity (watering isolation).
6. **Given** the device in automatic mode, **When** soil is polled periodically, **Then** fresh soil data
   feeds both the automatic decision and the status/sensors reporting (soil is no longer perpetually
   not-valid).

---

### Edge Cases

- **Pump start rejected** (e.g. already running, or duration out of range): the controller handles the
  false return without crashing or double-starting.
- **Clock never set (offline)**: automatic watering still runs (it depends on soil, not wall-clock);
  data-log timestamps reflect the not-set state rather than a bogus 1970 value.
- **Config changed at runtime** (thresholds/duration/soak/enabled): the next evaluation uses the new values;
  an in-flight burst is not retroactively invalidated except by the normal stop/fail-safe rules.
- **Manual → automatic transition while a manual run is active**: defined behavior (the run continues or is
  reconciled per the mode rules; no undefined double-control).
- **Soak pause active when the operator starts a manual run**: manual is unaffected by the gate.
- **Both moisture thresholds equal / low ≥ high (misconfiguration)**: the controller behaves deterministically
  (no oscillation/undefined state).
- **Reservoir max-fill abort then still low**: after an aborted fill the controller does not immediately
  re-slam the pump on (a safe re-evaluation, not a tight retry loop).
- **Concurrent access**: the controller task and other readers (console, HTTP status) reach shared sensors
  through the locked wrappers; a cross-call read is consistent (snapshot), with no torn reads or bus races.

## Requirements *(mandatory)*

### Functional Requirements

**Automatic watering (FR1)**

- **FR-001**: In automatic mode the system MUST start a watering burst (configured duration) when watering
  is enabled, the pump is not running, and the latest valid moisture is at or below the low threshold.
- **FR-002**: The system MUST stop watering when moisture reaches or exceeds the high threshold.
- **FR-003**: The system MUST enforce a soak/absorption pause (the minimum-watering-interval): after a
  burst, automatic mode MUST NOT start another burst until the pause has elapsed, **even if the soil still
  reads dry**. Burst duration and pause length are both runtime-configurable. (Deliberate divergence — the
  frozen Arduino code stores the interval but never enforces it.)
- **FR-004**: The system MUST gate every automatic decision on a successful sensor read result, never on the
  placeholder values a sensor holds before its first successful read.

**Fail-safe (FR4 — Constitution I)**

- **FR-005**: In automatic mode with the pump running, the system MUST emergency-stop the pump when the soil
  sensor is unavailable, when its data is stale beyond the staleness window, or when the moisture reading is
  outside the valid range; and MUST take no watering decision in those conditions.
- **FR-006**: A fail-safe stop MUST take effect immediately and MUST NOT be delayed or suppressed by the
  soak-pause gate or any scheduling logic.
- **FR-007**: Manual operation MUST be exempt from the automatic fail-safe stops (explicit operator
  override): a manual run continues despite sensor failure.

**Manual / mode (QUIRK 1)**

- **FR-008**: A manual run MUST be an explicit override; runs started by the automatic controller count as
  automatic (subject to all fail-safe stops), runs started by the operator count as manual (bypass). (The
  frozen code inverts this — target = the intended semantics.)
- **FR-009**: A manual run MUST be capped at the hard maximum runtime (300 s). (Deliberate divergence — the
  frozen code allows uncapped indefinite manual runs.)
- **FR-010**: A stop command MUST always stop the pump regardless of mode and clear the override state.

**Reservoir (FR5)**

- **FR-011**: On a board with a local refill pump, the system MUST run the reservoir level state machine:
  start filling when the water is low (both marks dry), stop when the high mark is wet, and abort filling at
  a hard maximum fill time (300 s).
- **FR-012**: The reservoir logic MUST take no action on a physically impossible sensor combination (low
  mark dry while high mark wet) and MUST take no action when either level sensor is not-yet-valid/invalid
  (invalid is never treated as "water absent").
- **FR-012a**: After a fill ends by the max-runtime abort (not by reaching the high mark), the system MUST
  wait a cooldown before starting another automatic fill, even if the water still reads low — preventing an
  endless refill cycle on a stuck high sensor or empty source (resolved 2026-07-05; deliberate divergence
  from parity). Manual fill is unaffected.
- **FR-013**: When the reservoir feature is disabled (or absent on the board), the reservoir pump MUST be
  forced off and its control logic skipped. The state-machine logic MUST be host-tested regardless of board
  (the capability flag gates the wiring, not the logic).

**Logging, time, integration**

- **FR-014**: The system MUST log the environmental + soil readings to persistent storage at the data-log
  cadence (NPK only when valid/non-negative), timestamped with epoch time, and MUST gate timestamp validity
  on the clock being set. It MUST log the safety-relevant events (pump start/stop with cause, fail-safe
  activations) without double-logging with the existing event observer.
- **FR-015**: The controller MUST initialize and offer manual operation even when one or both sensors fail
  to initialize (graceful degradation), as long as the pump and storage are available.
- **FR-016**: The controller MUST run on the device driven by the mode flag (automatic vs. manual override),
  replacing the interim direct-drive path; a periodic soil read MUST provide fresh soil data to both the
  automatic decision and the status reporting.
- **FR-017**: The controller MUST run on its own watchdog-registered task (or the watchdog-fed watering
  loop) and MUST never block, or be blocked by, network/HTTP activity — watering safety is independent of
  connectivity.
- **FR-018**: All watering-logic and safety-condition branches MUST be covered by host tests running in CI;
  the build MUST fail on any test failure. Both board targets MUST still build with the controller
  integrated.

### Key Entities *(include if data involved)*

- **Watering configuration**: low/high moisture thresholds, burst duration, soak-pause length, enabled flag,
  sensor-read + data-log intervals — from the typed config store, validated + persisted.
- **Controller state**: current mode (automatic / manual override), last-burst time (for the soak gate),
  last-valid-sensor time (for staleness), pump running state.
- **Sensor snapshot**: a consistent (single locked read) view of a sensor's validity + values, used by the
  controller and other readers to avoid torn cross-call reads.
- **Reservoir state**: composed from the two level marks (valid + wet/dry) into the fill decision + the
  fill-timer.
- **Logged records**: sensor-history entries (metric, epoch, value) and events (pump start/stop, fail-safe)
  with cause.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100 % of the controller's watering-decision and safety-condition branches are covered by host
  tests that run in CI and gate the build (the master-PRD success criterion).
- **SC-002**: In automatic mode, dry soil is watered in bursts with the enforced soak pause between them, and
  watering stops at the high threshold — demonstrated deterministically without hardware.
- **SC-003**: A sensor failure / stale / invalid reading in automatic mode stops the pump within the
  staleness window and blocks further automatic watering; a manual run is unaffected — all host-verified;
  the staleness stop also confirmed on the rig by pulling the RS485 cable.
- **SC-004**: Every row of the reservoir truth table (including the implausible and invalid-sensor rows), the
  high-mark stop, and the max-fill abort are host-verified; on the rig, fill starts on low, stops on high,
  and aborts at the cap.
- **SC-005**: A manual run is capped at 300 s and is never delayed or stopped by the automatic safety/soak
  logic.
- **SC-006**: Sensor history + events are logged at the configured cadence with correct epoch timestamps,
  and the device behaves sanely (no bogus 1970 timestamps treated as valid) before the clock is set.
- **SC-007**: Both board targets build with the controller integrated; the API mode flag drives automatic
  watering on-target, and live soil data is available (no longer perpetually not-valid).

## Assumptions

- **Soak gate is pre-decided (not re-opened)**: enforcing the minimum-watering-interval as a soak pause is
  the resolved project decision (Paul, 2026-06-10). Default burst duration 20 s, default soak/interval
  300 s, thresholds 30 %/55 % (the config-store defaults); the good field values are unknown and tuned
  empirically later — the spec fixes the *behavior*, not the tuned numbers.
- **"Manual" = the mode flag, not a pump flag**: the new pump interface has no manual-mode flag; manual is
  expressed via the watering-enabled/mode state (auto when enabled, operator override otherwise). Fail-safe
  guards are re-expressed against that state.
- **Staleness window = 30 s**, **reservoir max fill = 300 s**, **data-log cadence = 5 min default**,
  **implausible = high-wet without low-wet** — parity constants (`docs/parity-checklist.md` §1–§3).
- **Enabling helpers (implementation, not new behavior)**: consistent-snapshot helpers on the locked sensor
  wrappers (the booked `TODO(PR-11)` on env/level/power + one for soil), coherent
  `scriptSuccessfulRead/scriptFailedRead` helpers on the soil mock, and routing the diagnostic `rs485test`
  through a locked path — all needed so the periodic soil reader and the controller do not create torn reads
  or an RS485 bus race. These are enablers of FR-004/FR-016/edge-cases, decided at plan time.
- **Consumers do not branch on mock-only error codes**: the controller reacts to read success/failure +
  availability, never to specific numeric error codes that exist only in the mock (real esp-modbus collapses
  non-timeout errors), per the booked review guidance.
- **Event-logging coordination**: the existing `SystemObserver` already logs pump start/stop transitions;
  the controller adds fail-safe events and coordinates so pump transitions are not double-logged.

### Dependencies

- **PR-02..05** — the sensor/pump interfaces + mocks + drivers the controller consumes.
- **PR-06** — the typed config store (thresholds/duration/soak/enabled) + data storage (history + events).
- **PR-08** — the injectable wall clock (data-log timestamps + time-not-set) and the task watchdog the
  controller task registers with.
- **PR-09** — the API mode flag (`wateringEnabled`) the controller reads; the controller replaces PR-09's
  interim direct-drive path and makes soil/power `valid` by adding the periodic read.

### Out of Scope

- Running the parity checklist on the rig (PR-12).
- New control features beyond parity + the resolved soak gate.
- INA226-based pump protections (separate PRD).
- The multi-zone / central-reservoir refill-request design (future PRD) — rev2 level sensors feed status
  only here.
