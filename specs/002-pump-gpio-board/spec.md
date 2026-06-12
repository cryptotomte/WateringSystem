# Feature Specification: Pump Actuator Layer and Board Abstraction

**Feature Branch**: `002-pump-gpio-board`

**Created**: 2026-06-10

**Status**: Draft

**Input**: User description: "Pump actuator layer and board abstraction for ESP-IDF firmware, per docs/prd/PR-02-pump-gpio-board.md (authoritative mini-PRD; ground truth for behavior is docs/parity-checklist.md)."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Safe pump control on the bench rig (Priority: P1)

Paul connects the rev1 devkit rig and powers it on. Both pumps stay off through boot
and any reset. From the serial console he can start and stop each pump individually,
start a timed run that stops by itself, and trust that no pump can ever run longer
than its maximum runtime regardless of what command started it.

**Why this priority**: Pump control is the safety-critical core of the system —
everything later (watering logic, web control) builds on a pump layer that is
provably safe. This is also the first hardware-in-the-loop deliverable of phase 1.

**Independent Test**: Flash the rig, observe pump outputs at boot (off), issue serial
start/stop/timed commands, verify self-stop and the hard runtime cap with a stopwatch
or scope.

**Acceptance Scenarios**:

1. **Given** a freshly powered or reset device, **When** boot completes, **Then** both
   pump outputs are off and remain off until an explicit start command.
2. **Given** an idle pump, **When** the operator issues a timed start (e.g. 10 s),
   **Then** the pump runs and stops by itself when the time elapses.
3. **Given** a running pump, **When** the operator issues stop, **Then** the pump
   stops immediately.
4. **Given** a running pump (any start mode), **When** accumulated run time reaches
   the maximum runtime (300 s), **Then** the pump is stopped automatically and the
   stop is reported.

---

### User Story 2 - One firmware, two boards (Priority: P2)

An AI developer (or Paul) builds the firmware for either board revision. Selecting
the board at build time is the only step needed: pin mappings and feature flags
(RS485 direction pin, level-sensor polarity, current monitoring) follow automatically
from a single source of truth, and a wrong or missing board selection fails the build
rather than producing a mislabeled binary.

**Why this priority**: The board abstraction is the contract every later driver PR
(PR-03..PR-05) builds against; getting it wrong propagates into every phase-1 PR.

**Independent Test**: Build both board variants from clean checkout; verify each
binary used the correct pin table and feature flags via the build-time check and the
boot banner.

**Acceptance Scenarios**:

1. **Given** a clean checkout, **When** building with the rev1 board selected,
   **Then** the build embeds the rev1 pin table (verified at compile time) and
   reports the board name at boot.
2. **Given** a clean checkout, **When** building with the rev2 board selected,
   **Then** the build embeds the provisional rev2 table and rev2 feature flags
   (no RS485 direction pin, inverted level sensors, current monitoring present).
3. **Given** no board selected (configuration error), **When** building, **Then**
   the build fails with an explicit error.

---

### User Story 3 - Pump behavior testable without hardware (Priority: P3)

An AI developer changes pump or (later) watering logic and runs the host test suite
in CI. Runtime enforcement, timed-run behavior and run-time tracking are verified
against a simulated clock — no devkit needed, failures block the merge.

**Why this priority**: Host-testability is a constitution principle (II) and the
foundation PR-11's 100 % logic coverage will build on; the mock pump created here is
that foundation.

**Independent Test**: Run the host test suite on a machine with no ESP32 attached;
tests for max-runtime, self-stop and runtime tracking pass deterministically.

**Acceptance Scenarios**:

1. **Given** the host test suite, **When** a timed run is simulated past its
   duration, **Then** the pump model reports stopped and total run time equals the
   configured duration.
2. **Given** a simulated run reaching the maximum runtime, **When** time advances
   further, **Then** the pump model is stopped and the overrun is observable.
3. **Given** CI on a clean checkout, **When** the host tests run, **Then** they
   complete without any hardware or device-specific environment.

---

### Edge Cases

- Start command to an already-running pump: must not restart the runtime clock or
  extend a timed run; the call is rejected or ignored with a clear result.
- Timed start with duration 0: no indefinite runs exist in the new firmware — the
  request is rejected (deliberate change; see Assumptions).
- Timed start with duration above the maximum runtime: rejected with a clear error
  (consistent with duration-0 rejection; explicit rejection over silent clamping).
- Stop command to an already-stopped pump: harmless no-op, reported as success.
- Two pumps commanded simultaneously: each enforces its own runtime independently;
  stopping one never affects the other.
- Reset while a pump is running (watchdog, panic, power blip): pump output returns
  to off as part of boot (covered by the existing boot fail-safe; this feature must
  not weaken it).
- Time source anomalies in run-time tracking (wrap-around of the tick counter) must
  not cause a pump to run past its cap or stop prematurely.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The board abstraction MUST be the single source of truth for pin
  mappings and board feature flags for both board revisions; application code MUST
  obtain pins/flags only from it.
- **FR-002**: The rev1 pin table MUST match the running Arduino firmware as recorded
  in `docs/parity-checklist.md` (I2C SDA 21 / SCL 22, RS485 TX 16 / RX 17 / DE 25,
  plant pump 26, reservoir pump 27, reservoir level low 32 / high 33, status LED 2,
  manual button 5, config button 18). `docs/hardware.md` MUST NOT be used as pin
  source (known swapped TX/RX, checklist QUIRK 6).
- **FR-003**: The rev2 table MUST carry provisional pin numbers marked `TODO(SYNC1)`
  and the feature flags: no RS485 direction pin (auto-direction transceiver),
  level sensors active low (inverter), current monitoring present.
- **FR-004**: A build with no board (or a contradictory board) selection MUST fail
  at compile time. The selected board MUST be verifiable per build (compile-time
  check and boot banner).
- **FR-005**: Pump control MUST be exposed through hardware-independent interfaces
  (ported `IActuator`/`IWaterPump`, pure C++, no Arduino or hardware-SDK types)
  that host tests can use without any hardware headers.
- **FR-006**: The GPIO pump driver MUST drive the MOSFET gate active HIGH and MUST
  set the output to off as its first action when constructed/initialized (before
  any other use), preserving the boot fail-safe.
- **FR-007**: The driver MUST support: start (timed), stop, running-state query,
  and run-time reporting (current run start, accumulated run time).
- **FR-008**: Maximum runtime MUST be enforced in the driver for every run mode:
  reservoir pump 300 s (parity with Arduino `RESERVOIR_PUMP_MAX_RUNTIME`), plant
  pump 300 s for manual runs (**deliberate behavior change** — the Arduino firmware
  allows uncapped/indefinite manual runs; see `docs/parity-checklist.md` §4 and
  `docs/prd/PR-02`). Indefinite runs are not supported.
- **FR-009**: Reaching the maximum runtime MUST stop the pump autonomously (no
  cooperation from callers required) and the event MUST be observable (log + state).
- **FR-010**: Two pump instances (plant, reservoir) MUST be wired in the application
  behind the interfaces, each with its own configuration and independent state.
- **FR-011**: A serial diagnostic MUST allow starting (timed) and stopping each pump
  and querying pump status on the rig, for HIL verification.
- **FR-012**: A mock pump implementation MUST exist for host tests, driven by an
  injectable/simulated time source, sufficient to test max-runtime enforcement,
  self-stop and runtime tracking deterministically in CI.

### Key Entities

- **Board profile**: pin mapping + feature flags for one board revision; exactly one
  active per build; compile-time selected.
- **Pump (actuator)**: identity (plant/reservoir), output state, run mode (timed),
  configured duration, maximum runtime, run-time statistics.
- **Time source**: monotonic time used for timed runs and enforcement; injectable so
  hosts can simulate it.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Both board variants build green in CI from a clean checkout, and each
  build proves it selected the intended board (automated check, no human inspection).
- **SC-002**: On the rig, pump outputs are off at boot and after reset in 100 % of
  observed boots (HIL checklist).
- **SC-003**: A timed pump run self-stops within 1 s of its configured duration, and
  no pump ever exceeds its 300 s cap in any test.
- **SC-004**: Host test suite covering runtime enforcement and tracking runs in CI
  with zero hardware dependencies and passes deterministically (no flaky reruns).
- **SC-005**: Operator can start/stop/query each pump over the serial console using
  documented commands on the first attempt (HIL checklist).

## Assumptions

- Indefinite pump runs are intentionally removed: every run is timed and capped at
  300 s. This resolves the Arduino "duration 0 = indefinite manual run" semantics by
  rejection of duration 0 (the operator simply issues a new timed run if needed).
  Recorded as a deliberate behavior change in `docs/parity-checklist.md` §4 / PR-02.
- The plant pump's automatic-mode watering duration remains configuration-capped at
  1–300 s (parity); the driver-level cap is a second, independent safety net.
- Rev2 provisional pins reuse the rev1 numbers where applicable until SYNC 1; only
  the feature flags differ today. Final rev2 numbers land at SYNC 1 (gates PR-14,
  not this PR).
- The mode-flag semantics quirk (QUIRK 1 — Arduino flags auto runs as manual) is
  resolved at the controller level in PR-11; this PR's driver exposes neutral
  timed-run semantics and does not encode manual/automatic policy.
- The status LED and button pins enter the board table now (they are board facts)
  but their behaviors are implemented in later PRs.
- Serial diagnostic is a temporary rig tool; the full diagnostic command set (FR12
  of the master PRD) arrives with later phases.
