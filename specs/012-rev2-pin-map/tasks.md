# Tasks: rev2 board profile aligned with frozen hardware

**Input**: Design documents from `specs/012-rev2-pin-map/`
**Prerequisites**: plan.md (D1–D4), research.md (R1–R6), data-model.md (capability matrix), contracts/board-profile-contract.md

**Tests**: Included — the contract tests ARE the feature's verification
(constitution II; spec FR-007). Ordering is test-first: contract assertions
are written before the header edits that make them pass; a red step is a
build failure, which is the intended TDD signal for compile-time contracts.

**Organization**: By user story, in spec priority order. All stories touch
`board.h` and the contract TUs, so tasks run sequentially (same files — no
[P] markers except the truly independent docs task).

## Phase 1: Setup

- [x] T001 Record the green baseline: run the host test suite and both board-target builds per `specs/012-rev2-pin-map/quickstart.md` §1–2 on the unmodified branch; note results in the task log. A pre-existing red here is a STOP — this feature must start from green.

## Phase 2: Foundational

*(No blocking infrastructure — single-header feature. Proceed to stories.)*

## Phase 3: User Story 1 — rev2 boot touches only pins that exist (P1)

**Goal**: remove the phantom buttons (BTN_CONFIG on EXP_SCK/IO18 polled at
boot), guard the boot provisioning button path, reserve the expansion set.

**Independent test**: host build fails if rev2 defines a button pin or any
core pin collides with the expansion set; app_main compiles for rev2 without
the button block and for rev1 with it, unchanged.

- [x] T002 [US1] Extend `firmware/test_apps/host/main/test_board_contract_rev2.cpp`: assert `BOARD_HAS_BTN_MANUAL == 0` and `BOARD_HAS_BTN_CONFIG == 0`; `#ifdef BOARD_PIN_BTN_MANUAL/`BOARD_PIN_BTN_CONFIG` → `#error`; add static_asserts that every defined core `BOARD_PIN_*` (I2C, RS485, pump, levels, LED — and the US2 signals once they exist) differs from each of 18, 19, 23, 4, 27. Expect the build to go red (pins still defined).
- [x] T003 [US1] Extend `firmware/test_apps/host/main/test_board_contract_rev1.cpp`: assert `BOARD_HAS_BTN_MANUAL == 1`, `BOARD_PIN_BTN_MANUAL == 5`, `BOARD_HAS_BTN_CONFIG == 1`, `BOARD_PIN_BTN_CONFIG == 18` — pins rev1 genuinely has, values frozen (FR-002 regression guard). Document in the TU comment that the expansion reservation is rev2-only (rev1 legitimately uses 18/27 — data-model invariant 3).
- [x] T004 [US1] Edit `firmware/components/board/include/board/board.h`: add `BOARD_HAS_BTN_MANUAL 1` / `BOARD_HAS_BTN_CONFIG 1` to the rev1 section above the existing pins; in the rev2 section set both flags 0 and DELETE both pin defines (deliberate-undefine pattern, comment mirroring the RS485_DE/reservoir-pump wording).
- [x] T005 [US1] Edit `firmware/components/board/include/board/board.h` sanity section: add flag↔pin consistency `#error` pairs for both button flags (pattern of lines 211–217); add the rev2-only expansion-reservation check (`#if`-chain erroring if any defined core pin ∈ {18,19,23,4,27}), placed inside a `CONFIG_BOARD_REV2` conditional per data-model invariant 3.
- [x] T006 [US1] Edit `firmware/main/app_main.cpp`: wrap the config-button provisioning block (input `gpio_config` of `BOARD_PIN_BTN_CONFIG`, the poll loop and its STATUS_LED feedback, approx. lines 148–217) in `#if BOARD_HAS_BTN_CONFIG`; the credentials-absent provisioning path and all pump boot-safety code stay untouched. Update the block comment to state why (no button on rev2; IO18 is EXP_SCK).
- [x] T007 [US1] Validate US1: host suite green, both board targets build green (quickstart §1–2).

**Checkpoint**: US1 delivers the safety/correctness fix on its own.

## Phase 4: User Story 2 — profile states every frozen signal (P2)

**Goal**: add VBAT_SENSE (IO34), PWR_PG (IO35), SENS_PWR_EN (IO25) behind
capability flags.

**Independent test**: rev2 TU asserts the three pins/flags; rev1 TU proves
unguarded references cannot compile on rev1.

- [x] T008 [US2] Extend `firmware/test_apps/host/main/test_board_contract_rev2.cpp`: assert `BOARD_HAS_VBAT_SENSE == 1` / `BOARD_PIN_VBAT_SENSE == 34`, `BOARD_HAS_PWR_PG == 1` / `BOARD_PIN_PWR_PG == 35`, `BOARD_HAS_SENS_PWR_EN == 1` / `BOARD_PIN_SENS_PWR_EN == 25`; fold the three pins into the T002 expansion-disjointness asserts. Red until T010.
- [x] T009 [US2] Extend `firmware/test_apps/host/main/test_board_contract_rev1.cpp`: assert all three flags are `== 0` and pins undefined (`#ifdef` → `#error`) on rev1.
- [x] T010 [US2] Edit `firmware/components/board/include/board/board.h`: rev1 section — three flags at 0; rev2 section — three flags at 1 with pins 34/35/25 and constraint comments (IO34 input-only ADC1, ADC2 dead with WiFi, FW-1 pointer; IO35 input-only, open-drain + external pull-up, no internal pulls IO34–39, FW-6; IO25 output, sensor rail OFF default, FW-3 settle owned by PR-14). Add the three flag↔pin sanity pairs; confirm the pin-distinctness checks still pass (25 = RS485_DE on rev1 vs SENS_PWR_EN on rev2 — never same target).
- [x] T011 [US2] Validate US2: host suite green, both board targets build green.

**Checkpoint**: PR-14 driver work now has every frozen fact available.

## Phase 5: User Story 3 — annotations tell the truth (P3)

**Goal**: zero stale markers; citations in; solar comment corrected.

**Independent test**: quickstart §3 grep returns nothing.

- [ ] T012 [US3] Edit `firmware/components/board/include/board/board.h`: delete all 11 `TODO(SYNC1)` markers and the "GPIO numbers provisionally mirror rev 1…" header sentence; each rev2 pin group cites `hardware/rev2/design-notes/02-mcu.md §2.2 SYNC 1 map (frozen 2026-08-12)` once, group-level not per-line; update the I²C address-map comment: `0x41 solar INA226 — populated on this node (decision 2026-08-12; driver support PR-14)`, BME280 = 0x77.
- [ ] T013 [US3] Validate US3: `grep -rn "TODO(SYNC1)\|provisionally" firmware/components/board/` returns nothing (quickstart §3); host suite + both targets still green.

## Phase 6: Polish & cross-cutting

- [ ] T014 [P] Check `firmware/CLAUDE.md` board-profile documentation (plan D4): if it enumerates capability flags or the pin-table convention, add the five new flags and the frozen-map citation convention; if it doesn't document the profile, change nothing.
- [ ] T015 Negative proof (quickstart §4): temporarily add an unguarded `BOARD_PIN_BTN_CONFIG` use to the rev2 TU, confirm the build FAILS, revert. Record in the implementation notes.
- [ ] T016 Full quickstart pass (§1–3) as the feature's final gate; assemble the implementer's test checklist for CP3 (host-test PR — CI suite is the verifier; no HIL needed, note the deferred bench verification lands in PR-14).

## Dependencies & execution order

- T001 → everything (green baseline gate).
- US1 (T002–T007) → US2 (T008–T011): same files, and T008 folds new pins
  into T002's disjointness list. US2 → US3 (T012–T013): comment edits land
  last so citations describe the final content. Polish last; T014 is [P]
  (different file, no dependency).
- MVP scope: **US1 alone** — the safety/correctness fix stands on its own if
  implementation must stop early.

## Implementation strategy

Test-first per story: write the contract assertions (red = build failure),
make them green with the header/app edit, validate, move on. Every task's
"green" is machine-checkable (build result or grep), so the implementer can
self-verify without judgment calls. Commit granularity: one commit per story
phase (see CLAUDE.md one-branch-at-a-time rule; commits stay on
fix/rev2-pin-map in the worktree).
