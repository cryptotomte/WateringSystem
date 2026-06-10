# PR-12: parity-validation

> Phase 4 — application logic (completes phase 4 exit criteria)

## Goal

Execute the full `docs/parity-checklist.md` against the rig (HIL) and close every gap,
so the ESP-IDF firmware is demonstrably feature-par with Arduino v2.3.

## Scope

- Dry-run pass by the implementer: walk the parity checklist against the merged
  firmware (PR-02..PR-11), mark items expected-pass / needs-fix / needs-rig.
- Fix all gaps found — bug fixes and small behavior corrections only; anything
  larger goes back to the owning PR's follow-up.
- Produce the **HIL checklist run-sheet** (concrete step-by-step protocol per
  checklist item: preconditions, action, expected observation) that Paul executes on
  the rig at checkpoint 3. Categories from the phase 0 checklist:
  - Watering logic: thresholds, min interval, duration, manual/auto.
  - Sensor validation + fail-safe pump stop (incl. stale-data window).
  - Reservoir state machine incl. 300 s max fill and FR5 polarity note
    (level-sensor polarity = *correct* behavior, explicitly not bug-for-bug parity;
    bench measurement from PR-05 is the reference).
  - All web routes (real route list extracted from Arduino code → `/api/v1/`
    equivalents mapping table).
  - Serial diagnostic commands (`rs485test`/`test`, `soil`/`sensor`, `help` parity).
  - WiFi reconnection and provisioning/AP behavior; NTP/local time.
  - Storage: config persistence, history logging, documented format divergences
    (clean start on rev2 — no data migration, per master PRD).
- Update `docs/parity-checklist.md` with ticked boxes, measured values, and dated
  sign-off; deviations (intentional, e.g. FR5 polarity fix) recorded with rationale.

## Out of scope

- New functionality. Performance work beyond parity. Rev2-specific behavior (PR-14).

## Functional requirements covered

- Verification pass over FR1–FR5, FR8–FR10, FR12–FR13 (master PRD success criterion:
  parity checklist 100 % ticked, including all fail-safe behaviors).

## Dependencies

- PR-09, PR-10, PR-11 (everything user-visible must be merged). Rig availability.

## Acceptance criteria

- [CI] Full pipeline (build both targets + host test suite) green on the final commit.
- [HIL] Parity checklist 100 % ticked on the rig by Paul, every fail-safe item
  demonstrated live (sensor unplug, reservoir overrun, watchdog, WiFi loss).
- Review: ticked checklist + deviation log approved — this is the phase 4 gate and
  the green light for phase 5 (OTA) field-readiness claims.

## Estimated size

M
