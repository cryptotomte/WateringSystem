# Research: rev2 board profile aligned with frozen hardware

**Date**: 2026-08-12 · **Feature**: 012-rev2-pin-map

## R1 — What is actually divergent on origin/main (survey result)

**Decision basis for the whole feature.** The initial assumption (reservoir
pump on IO27 still defined for rev2) was FALSE — fixed in feature 006 with
`BOARD_HAS_RESERVOIR_PUMP`, deliberate pin-undefine enforcement, guarded
app_main, and contract tests. Verified divergences that remain:

| # | Divergence | Evidence |
|---|---|---|
| 1 | Phantom buttons: rev2 defines `BOARD_PIN_BTN_MANUAL 5`, `BOARD_PIN_BTN_CONFIG 18`; neither exists on frozen rev2 (only BOOT/RESET); IO18 = `EXP_SCK` | `board.h` rev2 section; `02-mcu.md` §2.2 SYNC 1 map; `08-expansion.md` |
| 2 | Boot path reads the phantom button: `app_main.cpp` configures IO18 as input and polls it (with STATUS_LED feedback) to enter provisioning | `app_main.cpp:148-217` |
| 3 | Missing signals: `VBAT_SENSE` (IO34), `PWR_PG` (IO35), `SENS_PWR_EN` (IO25) undefined | grep over `board.h`; `01-power.md` §1.0a; `rev2-firmware-notes.md` FW-1/FW-6 |
| 4 | 11 stale `TODO(SYNC1)` markers + "provisionally mirror rev 1" header | grep count |
| 5 | Stale comment: I²C map calls 0x41 "solar footprint, DNP"; solar group is populated per 2026-08-12 decision | `board.h` rev2 INA226 block; `01-power.md` §1.5 |

All present rev2 pin VALUES verified correct against the frozen contract
(I2C 21/22, RS485 16/17, pump 26, level 32/33, LED 2). The fix is
subtractive/additive, not corrective, for existing values.

## R2 — Mechanism for board-conditional peripherals

**Decision**: per-peripheral capability flags (`BOARD_HAS_*`) with the
deliberate-undefine enforcement pattern.

**Rationale**: the codebase already settled this twice — `BOARD_HAS_RS485_DE`
(feature 004) and `BOARD_HAS_RESERVOIR_PUMP` (feature 006), both with the
"flag 0 ⇒ pin macro undefined ⇒ unguarded reference is a compile error"
pattern, sanity-checked in the header's consistency section and pinned by the
contract-test TUs. Introducing `BOARD_PUMP_COUNT` (the alternative floated at
spec time) would add a second parallel mechanism for the same job.

**Alternatives considered**: `BOARD_PUMP_COUNT` — rejected: pumps already use
the HAS-flag pattern on origin/main; a count adds nothing the flag doesn't
give and invites index-loop code over pins that don't form an array.

**New flags this feature adds**: `BOARD_HAS_BTN_MANUAL`, `BOARD_HAS_BTN_CONFIG`
(rev1 = 1, rev2 = 0); `BOARD_HAS_VBAT_SENSE`, `BOARD_HAS_PWR_PG`,
`BOARD_HAS_SENS_PWR_EN` (rev1 = 0, rev2 = 1). Flags are defined 0/1 on BOTH
boards (never absent) so `#if` works uniformly; pin macros exist only where
flag = 1.

## R3 — Consumers that must become conditional

Grep survey of `BOARD_PIN_BTN_*` / `BOARD_PIN_STATUS_LED` outside board.h:

- `app_main.cpp:148-217` — config-button provisioning entry (IO18 input cfg,
  poll loop, LED feedback). Must be `#if BOARD_HAS_BTN_CONFIG`. The
  credentials-absent provisioning path is separate and stays unconditional.
- `wifi_task.cpp/h` — STATUS_LED only; LED exists on both boards; no change.
- `BOARD_PIN_BTN_MANUAL` — **zero consumers** outside board.h. Removing the
  rev2 definition breaks nothing.

## R4 — Where the contract is enforced

- `firmware/test_apps/host/main/test_board_contract_rev{1,2}.cpp`: each TU
  defines `CONFIG_BOARD_REVx 1` and includes the REAL `board/board.h`,
  pinning values/absences with `static_assert` / `#ifdef` + `#error`. The
  natural home for every new assertion (buttons, new signals, expansion
  disjointness).
- `board.h` sanity section: compile-time distinctness and flag/pin
  consistency checks; gains (a) flag↔pin consistency for the five new flags,
  (b) a rev2-only expansion-reservation check asserting no defined core pin
  equals 18/19/23/4/27.
- CI already builds both targets + host suite (constitution III).

## R5 — Expansion-set reservation as compile-time property (SC-002)

**Decision**: express "boot-claimed ⊆ profile ∧ profile ∩ expansion = ∅" in
the header's sanity section: on rev2, `#if` chains erroring if any defined
`BOARD_PIN_*` equals an expansion GPIO. Boot code derives every claimed pin
from `BOARD_PIN_*` macros (it already does), so header-level disjointness +
guarded button path together give the SC-002 property without runtime cost.

**Alternative considered**: runtime assertion table — rejected: adds code to
the safety-critical boot path for a property fully decidable at compile time.

## R6 — Provisioning UX on buttonless rev2

**Decision**: accept credentials-absent as the only rev2 provisioning
trigger (spec assumption, flagged to Paul at CP2). BOOT-button (IO0) reuse
after boot is feasible ESP32 practice but is new function on a frozen-scope
fix — deferred to PR-14+.
