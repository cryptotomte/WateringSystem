# PR-11: watering-controller-host-tests

> Phase 4 — application logic

## Goal

Port `WateringController` (all watering logic, scheduling, reservoir state machine,
fail-safe behavior) to pure interface-based C++ and put **100 % of watering logic and
safety conditions under host tests running in CI** on the IDF linux target.

## Scope

- `WateringController` port from `src/WateringController.cpp`, consuming only
  interfaces (`IEnvironmentalSensor`, `ISoilSensor`, `IWaterPump`, `IDataStorage`,
  level sensors, injectable clock) — zero hardware includes, fully host-buildable.
- Behavior parity per `docs/parity-checklist.md` (the phase 0 ground truth), notably:
  - Moisture-threshold watering with configurable duration. **Minimum watering
    interval: the Arduino code stores and persists the value but NEVER enforces it**
    (deliberate "NO minimum interval — if it's dry, water immediately!" comment,
    `WateringController.cpp:303`). Whether the ESP-IDF port starts enforcing it is
    an **OPEN DECISION for Paul** (`docs/parity-checklist.md` §1 marks it
    `[DECISION for Paul]`) — this PR must not silently resolve it; the decision is
    recorded in the spec before implementation.
  - Manual/automatic mode; manual operation bypasses sensor safety checks (parity).
    Manual runs capped at 300 s — **DELIBERATE behavior change, not parity**: the
    Arduino firmware runs manual/indefinite starts (legacy `/control` `start` with
    duration 0) uncapped (`docs/parity-checklist.md` §4).
  - **Fail-safe:** in automatic mode, invalid or stale soil data (Arduino staleness
    window: 30 s) stops the plant pump and blocks automatic watering (FR4).
  - Reservoir state machine on low/high level sensors: fill on low, stop on high,
    hard 300 s max fill runtime, sensor-implausibility handling (high without low).
  - Data logging cadence (Arduino default 5 min) into `IDataStorage`.
  - Time-not-set behavior (pre-SNTP) defined and tested.
- **Host test suite in CI** (IDF linux target + mocks from PR-02..06):
  every watering decision branch and every safety condition covered; CI gate fails
  the build on test failure. This is the master PRD success criterion "watering
  logic and safety conditions 100 % host-tested".
- On-target wiring: controller task replaces the direct pump/config plumbing from
  PR-09's interim mode; API mode switch now drives the controller.

## Out of scope

- Running the parity checklist on the rig (PR-12). New control features beyond
  parity. INA226-based protections (separate PRD).

## Functional requirements covered

- FR1 (controller port with preserved behavior); FR4 (completed: fail-safe stop on
  invalid sensor data in auto mode); FR5 (reservoir control logic); testability NFR.

## Dependencies

- PR-02..PR-05 (interfaces + mocks + drivers), PR-06 (config/storage), PR-08
  (time source, watchdog registration for the controller task).

## Acceptance criteria

- [CI] Host test suite green in CI on every push; coverage of the controller's
  decision/safety branches demonstrated (coverage report or branch checklist in PR).
- [CI] Both board targets still build with the controller task integrated.
- [HIL] Smoke on rig: automatic mode waters when moisture below threshold; pulling
  the RS485 cable in auto mode stops the pump within the staleness window.
- [HIL] Reservoir fill starts on low, stops on high, and aborts at 300 s if the high
  sensor never triggers.

## Estimated size

L
