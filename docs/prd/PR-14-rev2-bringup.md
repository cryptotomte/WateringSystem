# PR-14: rev2-bringup

> Phase 6 — rev2 hardware. **Gated by SYNC 2 (boards delivered).**

## Goal

Bring up and validate the `BOARD_REV2` firmware on real rev2 hardware, then deploy to
the greenhouse and retire the Arduino unit after a two-week unattended soak.

## Scope

- Finalize the `BOARD_REV2` table in the board component against the as-built rev2
  board (pin numbers frozen at SYNC 1; correct any as-built deviations here).
- **Bring-up checklist (HIL on rev2 bench):**
  - Initial flash via **CP2102N** USB-C — first and last cable flash of the unit.
  - **THVD1426 auto-direction RS485 @ 9600 baud:** soil sensor reads with no DE pin;
    verify driver-release/idle behavior against datasheet timing (tHOLD vs 104 µs bit
    time at 9600 baud — open question from rev2 BOM) with scope on A/B; confirm bias
    resistors (R4 pull-up A, R5 pull-down B) give a defined idle state.
  - **INA226 (single device — rev2 has one pump channel, FR4 decision
    2026-06-10):** responds at its address (default 0x40, no collision with
    BME280 0x76/0x77); plausible 12 V bus voltage; current readings against a
    bench meter while the watering pump runs (high-side shunt R1).
  - **Single-pump node:** no reservoir pump output exists; verify the firmware
    (BOARD_HAS_RESERVOIR_PUMP=0) exposes exactly one pump in console/API and the
    reservoir level sensors report status without driving any refill logic.
    Greenhouse reservoir is refilled manually until the central reservoir unit
    exists (multi-zone, future PRD).
  - **Inverted level sensors:** XKC-Y26 through 2N7002 inverter ⇒ GPIO active LOW;
    wet/dry bench measurement confirms `BOARD_LEVEL_SENSOR_ACTIVE_LOW` mapping
    (counterpart of the PR-05 rev1 verification).
  - **JTAG smoke test:** attach debugger on header J6, halt/resume, read a variable —
    proves the debug path for future use.
  - BME280, pumps-off-at-boot, WiFi provisioning, web UI, watchdog reboot — rerun of
    the relevant parity run-sheet items on rev2.
- OTA proof on rev2: device installs an update from a GitHub Release (no cable).
- **Field deployment checklist** (deliverable, `docs/`): pre-deployment config,
  AP password rotation (open-question follow-up — old password is in public git
  history), physical install steps, rollback-to-Arduino-unit contingency, soak
  monitoring plan.
- Greenhouse deployment + **≥ 2 weeks unattended soak**; event log reviewed; Arduino
  unit retired afterwards (master PRD phase 6 exit criterion).

## Out of scope

- Hardware design changes (Paul's track; firmware-visible findings filed as issues).
  INA226 protection features, frontend modernization (separate PRDs). Migration of
  data from the Arduino unit (clean start, per master PRD).

## Functional requirements covered

- FR6 (INA226 validated on hardware); FR2/FR5 rev2 variants (DE-less RS485, inverted
  levels); final validation of all FRs in the field.

## Dependencies

- **SYNC 2** (rev2 boards delivered). PR-12 (parity proven), PR-13 (OTA, needed for
  cable-free operation in the field), PR-05 (drivers under test).

## Acceptance criteria

- [CI] Both targets still build; any rev2 pin-table fixes covered by the
  compile-time/host board-config checks from PR-02.
- [HIL] Entire bring-up checklist above ticked on the rev2 bench unit, with scope
  evidence for THVD1426 timing and meter evidence for INA226 current.
- [HIL] Rev2 unit OTA-updates itself from GitHub Releases.
- Field: rev2 unit runs the greenhouse **≥ 2 weeks without intervention**; event log
  shows no fail-safe surprises; Arduino unit decommissioned. **Project exit gate.**

## Estimated size

L
