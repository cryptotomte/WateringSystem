# Feature Specification: rev2 board profile aligned with frozen hardware

**Feature Branch**: `fix/rev2-pin-map`

**Created**: 2026-08-12

**Status**: Draft (rewritten same day after codebase survey — see Revision note)

**Input**: User description: "Align the rev2 board profile in firmware/components/board/include/board/board.h with the frozen rev2 hardware design. Close the stale SYNC1 markers, remove phantom peripherals, add the missing frozen signals, keep the pumps-OFF-at-boot invariant intact on both boards."

## Revision note

The first draft of this spec assumed the rev2 profile still defined two pump
channels with a reservoir pump on IO27. A survey of `origin/main` (the branch
this feature builds on) showed that fix already landed during phases 1–3:
`BOARD_HAS_RESERVOIR_PUMP == 0` on rev2, the pin macro deliberately undefined,
boot safety properly guarded, host contract tests in place
(`test_board_contract_rev1/rev2.cpp`). The earlier observation came from a
stale working tree. This spec covers what actually remains divergent.

## Context

The rev2 hardware design was frozen 2026-08-12 (all 8 schematic sheets drawn,
ERC clean, components ordered). Surveying the current rev2 board profile
against the frozen design leaves three real divergence classes:

1. **A boot-active phantom peripheral (the serious one).** The profile defines
   `BOARD_PIN_BTN_CONFIG = 18` and `BOARD_PIN_BTN_MANUAL = 5`, and the boot
   sequence configures IO18 as an input and reads it to decide whether to
   enter WiFi provisioning mode. On frozen rev2 hardware **neither button
   exists** (the board has only BOOT/RESET switches on IO0/EN and a status
   LED on IO2), and **IO18 is `EXP_SCK`** — the expansion header's SPI clock
   (J7, reserved set IO18/19/23/4/27). Consequences on real rev2 hardware:
   whatever an attached expansion device does with SCK can be misread as
   "operator holds the config button" and drop the node into provisioning
   mode at boot; and core firmware claims a pin the design reserves for
   expansion.
2. **Missing frozen signals.** Three signals the frozen design provides have
   no definition: battery-voltage sense (IO34, ADC1, input-only), buck
   power-good (IO35, input-only, open-drain with external pull-up), and
   sensor-rail enable (IO25, output, rail OFF by default).
3. **Stale annotations.** Eleven "TODO(SYNC1)" markers and a section header
   saying the pin map "provisionally mirrors rev 1" — but SYNC 1 (the pin-map
   freeze) has happened and every present value matches the frozen contract.
   Also one stale hardware comment: the I²C address-map note calls the solar
   INA226 (0x41) "footprint, DNP", but the populate decision of 2026-06-20
   (gate cleared 2026-08-10, `01-power.md` §1.5) made it a populated device
   on this node.

Already correct on `origin/main` — to be regression-guarded, not re-done:
single-pump profile with compile-error enforcement, level-sensor polarity and
timing constants (active LOW, 300/500 ms), pump INA226 at 0x40, status LED on
IO2 (matches the frozen design), and the existing board contract host tests.

Sources of truth: `hardware/rev2/design-notes/02-mcu.md` §2.2 (SYNC 1 GPIO
map) and `00-architecture.md` §0.5 pin contract (both on the
`docs/single-pump-node` branch), `docs/rev2-firmware-notes.md` FW-1..FW-6,
`docs/parity-checklist.md` for rev1.

### Frozen rev2 pin facts (the contract this feature encodes)

| Signal | GPIO | Direction / note |
|---|---|---|
| I2C SDA / SCL | IO21 / IO22 | bidirectional; 3 devices: BME280 0x77, INA226 0x40 (pump), INA226 0x41 (solar, populated) |
| RS485 TX / RX | IO16 / IO17 | auto-direction transceiver, no DE pin |
| Pump enable (single pump) | IO26 | active-HIGH output *(already correct)* |
| Reservoir level LOW / HIGH | IO32 / IO33 | inputs, active LOW *(already correct)* |
| Status LED | IO2 | output *(already correct)* |
| Sensor-rail enable | IO25 | output; switched 12 V sensor domain, OFF by default — **missing today** |
| Battery voltage sense | IO34 | input-only, ADC1; ADC2 unusable with WiFi — **missing today** |
| Buck power-good | IO35 | input-only, open-drain externally pulled up; IO34–39 have no internal pulls — **missing today** |
| Manual / config buttons | — | **do not exist on rev2** (BOOT/RESET only) — profile must say so |
| Expansion (reserved, untouchable) | IO18/19/23/4/27 | J7: VSPI SCK=IO18, MISO=IO19, MOSI=IO23, CS=IO4, IRQ=IO27 (`08-expansion.md` §8.3); core firmware must not claim any of them |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - rev2 boot touches only pins that exist on rev2 (Priority: P1)

Paul boots the firmware on rev2 hardware (module prototype or final PCB) with
an expansion device attached to J7. The node never claims an expansion pin and
never mistakes expansion-bus activity for a pressed configuration button.

**Why this priority**: This is the correctness- and hardware-protecting fix.
Today's boot path reads `EXP_SCK` as if it were a button — a spurious
provisioning entry at best, interference with attached expansion hardware at
worst, on a project whose failure history is exactly "pins used for something
the hardware didn't intend".

**Independent Test**: Host contract tests for the rev2 target assert the
button macros are absent (unguarded reference fails to compile) and that the
boot path's claimed-pin set is disjoint from the expansion set.

**Acceptance Scenarios**:

1. **Given** the rev2 board target, **When** the firmware boots, **Then** no
   GPIO in the expansion set IO18/19/23/4/27 is configured (input or output)
   or read by core firmware.
2. **Given** the rev2 board target with no stored WiFi credentials, **When**
   the firmware boots, **Then** provisioning mode is entered via the
   credentials-absent path exactly as before — the button path simply does
   not exist on this board.
3. **Given** the rev1 board target, **When** the firmware boots, **Then** the
   config-button check on IO18, the manual button on IO5, and both pump
   outputs behave exactly as today — rev1 has this hardware and must not
   change.

---

### User Story 2 - the profile states every frozen rev2 signal (Priority: P2)

A developer (human or agent) implementing the rev2 bring-up drivers (PR-14)
reads the board profile and finds every signal the frozen hardware provides —
battery sense, power-good, sensor-rail enable — with direction and polarity
constraints, without opening the schematic.

**Why this priority**: The board profile is the single place pin facts live
(project rule: pin tables are not duplicated elsewhere). Missing entries force
the next developer back to the schematic and invite guessed constants.

**Independent Test**: rev2 contract test asserts the three new definitions
exist with the frozen values; rev1 contract test asserts referencing them
unguarded fails to compile.

**Acceptance Scenarios**:

1. **Given** the rev2 board target, **When** a driver references battery
   sense, power-good, or sensor-rail enable, **Then** the definitions exist
   with the frozen GPIO numbers (IO34, IO35, IO25) and capability flags.
2. **Given** the rev1 board target, **When** the same references are made
   unguarded, **Then** compilation fails (rev1 lacks all three) — the
   established enforcement pattern.

---

### User Story 3 - annotations tell the truth (Priority: P3)

A reader of the rev2 profile sees no stale "TODO(SYNC1)" markers, no
"provisionally mirrors rev 1" language, and no "solar INA226 is DNP" claim;
each section instead cites the frozen design source.

**Why this priority**: Stale TODOs actively invite "helpful" future edits to
values that are frozen, and the DNP claim contradicts a purchasing decision
already made.

**Independent Test**: Text search over the board component returns zero
occurrences of "TODO(SYNC1)" and "provisionally"; the I²C address-map comment
describes 0x41 as populated on this node.

**Acceptance Scenarios**:

1. **Given** the merged feature, **When** searching the board component for
   SYNC1 markers or provisional wording, **Then** none remain in the rev2
   section and each pin group cites its design-note source.

---

### Edge Cases

- Watchdog/panic/OTA reset (not cold boot): the boot path runs the same
  claiming logic — the expansion-untouched and single-pump guarantees hold on
  every reset type.
- rev2 provisioning re-entry on a deployed node: with the button path absent,
  re-provisioning relies on the credentials-absent path and whatever
  API/reset mechanisms PR-07 provides. If a physical re-provisioning trigger
  is wanted on rev2, the BOOT button (IO0) is free after boot — that is a
  future feature (PR-14 or later), explicitly out of scope here.
- A future board with different button complement must be expressible via the
  capability flags without touching application logic.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The rev2 board profile MUST NOT define manual- or config-button
  pins; button presence MUST be a capability flag, and an unguarded reference
  to an absent button pin MUST fail compilation (established enforcement
  pattern).
- **FR-002**: The rev1 board profile MUST remain behaviorally identical: all
  existing rev1 pin values, both buttons, both pumps, unchanged.
- **FR-003**: The boot provisioning-entry logic MUST compile the button path
  only on boards that have a config button; on buttonless boards the
  credentials-absent trigger (existing PR-07 behavior) is the sole entry and
  MUST be preserved unchanged.
- **FR-004**: On rev2, core firmware MUST NOT configure, drive, or read any
  GPIO in the expansion set (IO18, IO19, IO23, IO4, IO27).
- **FR-005**: The rev2 profile MUST define battery-voltage sense (IO34,
  ADC1, input-only), buck power-good (IO35, input-only, externally pulled
  up), and sensor-rail enable (IO25, output, rail OFF default), each behind a
  capability flag with the compile-error enforcement pattern on boards that
  lack the signal.
- **FR-006**: All "TODO(SYNC1)" markers and provisional wording MUST be
  removed from the board component; each rev2 pin group MUST cite its frozen
  design source; the solar INA226 comment MUST reflect the populate decision
  (0x41 populated on this node; firmware support for reading it is PR-14
  scope).
- **FR-007**: Host contract tests MUST be extended to cover: button absence
  on rev2 / presence on rev1, the three new rev2 signals, and the
  expansion-set-disjointness of boot-claimed pins; existing pump-invariant
  tests MUST remain green unchanged.
- **FR-008**: Both board targets MUST build green in CI from a clean
  checkout; the host test suite MUST pass.

### Key Entities

- **Board profile**: per-target pin definitions and capability flags; the
  single source of pin truth in firmware.
- **Boot-claimed pin set**: every GPIO the boot path configures or reads on a
  target; must be derivable from the profile and disjoint from the expansion
  set on rev2.
- **Expansion pin set**: IO18/19/23/4/27 — reserved for J7, unclaimed by core
  firmware on rev2.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Host contract tests pass asserting: rev2 defines no button
  pins, defines the three new signals with frozen values, and rev1 values are
  unchanged.
- **SC-002**: The rev2 boot path's claimed-pin set is provably disjoint from
  IO18/19/23/4/27 (compile-time assertion or host test).
- **SC-003**: CI builds both board targets green on the feature branch; host
  suite passes.
- **SC-004**: Zero "TODO(SYNC1)" and zero "provisionally" occurrences remain
  in the board component.
- **SC-005**: The rev2 profile covers all rows of the frozen pin facts table
  (present signals defined, absent peripherals capability-flagged off).

## Assumptions

- The frozen pin facts table is correct and final, extracted from
  `02-mcu.md` §2.2 SYNC 1 map and the §0.5 pin contract after the 2026-08-12
  freeze. Future pin changes go schematic-first and re-open the profile
  deliberately.
- Consuming the new signal definitions (ADC reads, PG monitoring, rail
  switching, solar INA226 support) is PR-14 driver scope; this feature only
  makes the facts available and safe.
- Losing the physical config-button provisioning trigger on rev2 is accepted:
  the hardware has no such button, and the credentials-absent path covers
  first provisioning. A BOOT-button-based trigger is a possible future
  feature, out of scope.
- The legacy Arduino tree is frozen and untouched.
- No hardware is required for verification; host tests + CI builds suffice.
  Bench verification lands with PR-14 bring-up.
