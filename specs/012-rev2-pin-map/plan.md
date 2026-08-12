# Implementation Plan: rev2 board profile aligned with frozen hardware

**Branch**: `fix/rev2-pin-map` | **Date**: 2026-08-12 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/012-rev2-pin-map/spec.md`

## Summary

Close the gap between the frozen rev2 hardware (schematic freeze 2026-08-12)
and the firmware's rev2 board profile: remove the phantom button pins (one of
which sits on the expansion header's SPI clock and is polled at boot), add the
three missing frozen signals behind capability flags, and retire the stale
SYNC1 markers — using the established `BOARD_HAS_*` + deliberate-undefine
enforcement pattern, extended contract tests, and zero behavioral change on
rev1.

## Technical Context

**Language/Version**: C/C++ (C17 / C++17), ESP-IDF v6.0.1 (pinned Docker image)

**Primary Dependencies**: ESP-IDF GPIO/Kconfig only — no new components

**Storage**: N/A

**Testing**: host test app (`firmware/test_apps/host`, CMake + pinned
toolchain in CI), compile-time contract TUs per board target

**Target Platform**: ESP32 (rev1 devkit / rev2 custom PCB via
`CONFIG_BOARD_REV1_DEVKIT` / `CONFIG_BOARD_REV2`)

**Project Type**: embedded firmware, single repo

**Performance Goals**: N/A (compile-time facts; no runtime additions)

**Constraints**: pumps-OFF-at-boot invariant untouchable (constitution I);
rev1 behavior byte-equivalent; no hardware available for verification —
everything must be provable by build + host tests

**Scale/Scope**: 1 header, 1 boot file, 2 contract-test TUs, docs citations.
No new files except tests if split is cleaner.

## Constitution Check

*GATE: evaluated pre-Phase 0 and re-checked post-design — PASS on both.*

| Principle | Verdict | Note |
|---|---|---|
| I Safety First | PASS | Boot pump-OFF logic untouched on both boards; button-path guard removes a spurious-provisioning risk. No fail-safe weakened. |
| II Host-Testability | PASS | All new facts are compile-time; contract TUs extended; no hardware needed. |
| III Reproducible Builds | PASS | No dependency changes; CI already builds both targets. |
| IV Frozen Legacy | PASS | Arduino tree untouched. |
| V Checkpoint-Gated Workflow | PASS | CP1 passed silently; this plan stops at CP2 before implementation. |
| VI (remaining principles) | PASS | No scope beyond profile alignment. |

## Project Structure

### Documentation (this feature)

```text
specs/012-rev2-pin-map/
├── spec.md
├── plan.md              # this file
├── research.md          # R1–R6: survey, mechanism decision, consumers
├── data-model.md        # capability-flag/pin matrix for both boards
├── quickstart.md        # build/test validation commands
├── contracts/
│   └── board-profile-contract.md
├── checklists/requirements.md
└── tasks.md             # /speckit-tasks output (next step)
```

### Source Code (repository root)

```text
firmware/
├── components/board/include/board/board.h    # the profile — main edit
├── main/app_main.cpp                         # guard config-button block
└── test_apps/host/main/
    ├── test_board_contract_rev1.cpp          # extend: buttons present, new flags 0
    └── test_board_contract_rev2.cpp          # extend: buttons absent, new pins, expansion disjointness
```

**Structure Decision**: existing layout; no new components. The board profile
stays a single header per the project rule "pin tables live in
`firmware/components/board/` only".

## Design

### D1 — board.h rev2 section (per research R1/R2)

1. Remove `BOARD_PIN_BTN_MANUAL` / `BOARD_PIN_BTN_CONFIG` from rev2; add
   `BOARD_HAS_BTN_MANUAL` / `BOARD_HAS_BTN_CONFIG` **to both sections**
   (rev1 = 1 with existing pins 5/18 unchanged; rev2 = 0, pins undefined).
2. Add to rev2 (with `BOARD_HAS_* 1`), absent from rev1 (`BOARD_HAS_* 0`):
   - `BOARD_PIN_VBAT_SENSE 34` — input-only, ADC1 (FW-1: nonlinearity note
     stays in rev2-firmware-notes; the profile carries pin + ADC1-only fact)
   - `BOARD_PIN_PWR_PG 35` — input-only, open-drain + external pull-up, no
     internal pulls on IO34–39 (FW-6)
   - `BOARD_PIN_SENS_PWR_EN 25` — output, sensor rail OFF default
3. Replace all 11 `TODO(SYNC1)` markers and the "provisionally mirror rev 1"
   header with citations: `02-mcu.md §2.2 SYNC 1 map (frozen 2026-08-12)`.
4. Update the I²C address-map comment: 0x41 = solar INA226, populated on this
   node (support lands PR-14). Address constant for it NOT added — PR-14 owns
   the driver surface.
5. Sanity section: flag↔pin consistency checks for the five new flags (same
   pattern as reservoir pump), plus rev2-only expansion-reservation check —
   compile error if any defined `BOARD_PIN_*` ∈ {18, 19, 23, 4, 27}.

### D2 — app_main.cpp (per research R3)

Wrap the config-button provisioning block (`gpio_config` of BTN_CONFIG, the
poll loop, its LED feedback) in `#if BOARD_HAS_BTN_CONFIG`. The
credentials-absent provisioning path is unconditional and untouched. Pump
boot-safety code untouched.

### D3 — contract tests (per research R4)

- rev1 TU: `BOARD_HAS_BTN_CONFIG == 1`, `BOARD_PIN_BTN_CONFIG == 18`,
  `BOARD_HAS_BTN_MANUAL == 1`, pin 5; new signal flags all 0 and pins
  undefined (`#ifdef` + `#error`).
- rev2 TU: button flags 0, pins undefined; `BOARD_PIN_VBAT_SENSE == 34`,
  `BOARD_PIN_PWR_PG == 35`, `BOARD_PIN_SENS_PWR_EN == 25`, flags 1;
  static_assert that every defined core pin ∉ expansion set (belt to the
  header's braces — catches accidental future edits even if the header check
  is removed).
- Grep-style truth checks (zero `TODO(SYNC1)`) are a CI/test-script concern:
  add a trivial host test or CI step ONLY if cheap; otherwise SC-004 is
  verified at review. (Tasks phase decides; do not over-engineer.)

### D4 — docs

`firmware/CLAUDE.md` board section: note the five new capability flags and
the frozen-map citation convention, if that file documents the profile (check
at implementation; keep diff minimal).

## Complexity Tracking

No constitution violations. No new mechanisms — the feature deliberately
reuses the existing enforcement pattern (research R2 rejected the
`BOARD_PUMP_COUNT` alternative as a second parallel mechanism).
