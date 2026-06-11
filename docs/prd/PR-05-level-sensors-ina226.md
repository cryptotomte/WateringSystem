# PR-05: level-sensors-ina226

> Phase 1 — drivers

## Goal

Implement the reservoir level-sensor inputs with board-configured polarity (including
the FR5 bench verification that settles the rev1 polarity question), the
`BOARD_HAS_RESERVOIR_PUMP` capability flag (single-pump rev2 node, FR4 decision
2026-06-10), and an INA226 driver delivering raw voltage/current/power for the
watering-pump channel on rev2.

## Scope

### Level sensors (XKC-Y26, GPIO 32 low / GPIO 33 high)

- `LevelSensor` abstraction returning logical *water present / not present*; raw GPIO
  polarity selected by `BOARD_LEVEL_SENSOR_ACTIVE_LOW` from the board component.
- Polarity facts (master PRD FR5, from the 2026-04-12 fix branch, verified against the
  KiCad schematic): XKC-Y26 OUT is **active HIGH** (water = HIGH). Rev1 routes OUT
  directly via TXS0108E (non-inverting) ⇒ GPIO active HIGH. Rev2 routes via a 2N7002
  inverter (with 10 kΩ pull-up to 12 V on OUT) ⇒ GPIO active LOW.
- **Polarity status of the Arduino code:** the code on this branch already reads the
  level sensors **active HIGH** (`src/main.cpp:504-506`; the 2026-04-12 fix is
  merged). Master PRD FR5's "Arduino reads active LOW" describes the pre-fix state.
  This PR implements board-configured polarity (rev1 active HIGH, rev2 active LOW
  via 2N7002 inverter) and includes a mandatory **bench verification task**: measure
  actual GPIO levels on the rig with the sensor wet/dry, record the result in
  `docs/parity-checklist.md` (FR5 note), and only then freeze the rev1 polarity flag.
  See `docs/parity-checklist.md` §3 for the verified facts.
- Debounce/glitch filtering appropriate for slow level signals.
- Mock level sensor for host tests (drives the reservoir state machine tests in PR-11).

### Board capability flag: local reservoir pump (decision 2026-06-10)

- Introduce `BOARD_HAS_RESERVOIR_PUMP` in the board component: **1 on rev1**
  (bench rig and today's greenhouse unit have a local refill pump on GPIO 27),
  **0 on rev2** (single-pump node; refill is the future central reservoir unit's
  job — see `docs/feature-ideas.md` and master PRD FR4). Make the reservoir pump
  instance and its console command conditional on the flag (adjusts the PR-02
  wiring in `app_main`/`diag_console`). Level sensors exist on BOTH boards.

### INA226 (rev2 only, raw measurements only)

- `Ina226Sensor` component on the shared `i2c_master` bus (from PR-03): **one
  device** for the single watering-pump channel (rev2 has one pump channel per
  FR4 decision); default A0/A1 strapping (0x40) — no collision with BME280 at
  0x76/0x77; high-side shunt on the 12 V rail per rev2 BOM.
- Configure shunt calibration from Kconfig (shunt value per BOM), expose bus voltage,
  current, power as plain readings for logging/API.
- Compiled out on `BOARD_REV1_DEVKIT` (`BOARD_HAS_INA226`); graceful absent-device
  handling on rev2.

## Out of scope

- Reservoir state machine using the levels (PR-11). Any protection/alarm logic on
  INA226 data — dry-run/blockage detection is explicitly a follow-up PRD (master PRD
  "Ingår EJ"). On-hardware INA226 validation (PR-14, needs rev2 boards).

## Functional requirements covered

- FR5 (level sensors with board-configured polarity + bench verification);
  FR6 (partially: driver + raw readings; API/logging exposure completes in PR-09).

## Dependencies

- PR-02 (board component flags/pins); PR-03 (shared I2C bus) for the INA226 part.

## Acceptance criteria

- [CI] Both targets build: rev1 without INA226 and active-HIGH levels, rev2 with
  INA226 and active-LOW levels.
- [CI] Host test: polarity mapping for both board configs; debounce behavior.
- [HIL] Bench verification protocol executed on the rig (sensor wet/dry vs GPIO level),
  result documented in the parity checklist; logical readings correct in both states.
- [HIL-deferred to PR-14] INA226 readings on real rev2 hardware.

## Estimated size

M
