// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_board_contract_rev2.cpp
 * @brief Compile-time capability contract of the REAL rev2 board header
 * (tasks.md T016, feature 006).
 *
 * Companion of test_board_contract_rev1.cpp (mechanism documented there):
 * this TU compiles the real rev2 profile and pins the SINGLE-PUMP side of
 * the capability contract — most importantly that the reservoir pump pin
 * does not exist, so any unguarded reference anywhere is a compile error
 * (master PRD FR4 single-pump decision, spec 006 FR-006).
 */

#define CONFIG_BOARD_REV2 1
#include "board/board.h"

// rev2 is the single-pump node: capability flag 0 AND the pin REMOVED
// (flag=0 ⇒ pin undefined — the compile-error enforcement this feature's
// US2 rests on; same pattern as BOARD_PIN_RS485_DE).
static_assert(BOARD_HAS_RESERVOIR_PUMP == 0,
              "rev2 board contract: single-pump node (master PRD FR4)");
#ifdef BOARD_PIN_RESERVOIR_PUMP
#error "rev2 board contract: BOARD_PIN_RESERVOIR_PUMP must NOT be defined \
(unguarded references must fail the build)"
#endif

// rev2 carries the pump INA226 at 0x40 (A0 = A1 = GND; 0x41 reserved for
// the DNP solar footprint, 0x76/0x77 BME280 — the board-profile address
// map).
static_assert(BOARD_HAS_INA226 == 1,
              "rev2 board contract: INA226 pump monitor present");
#ifndef BOARD_INA226_ADDR
#error "rev2 board contract: BOARD_INA226_ADDR must be defined"
#endif
static_assert(BOARD_INA226_ADDR == 0x40,
              "rev2 board contract: pump INA226 at 0x40");

// Level sensors: rev2 polarity is active LOW (FW-5, 2N7002 inverter) with
// the FW-3 settle gate for the switched sensor rail.
static_assert(BOARD_LEVEL_ACTIVE_LOW == 1,
              "rev2 board contract: active LOW (FW-5, 2N7002 inverter)");
static_assert(BOARD_LEVEL_SETTLE_MS == 500,
              "rev2 board contract: 500 ms settle gate (FW-3)");
static_assert(BOARD_LEVEL_DEBOUNCE_MS == 300,
              "rev2 board contract: 300 ms debounce window");

// No RS485 direction pin either (THVD1426 auto-direction) — the pattern
// the reservoir-pump enforcement was cloned from stays intact.
static_assert(BOARD_HAS_RS485_DE == 0,
              "rev2 board contract: auto-direction RS485, no DE pin");
#ifdef BOARD_PIN_RS485_DE
#error "rev2 board contract: BOARD_PIN_RS485_DE must NOT be defined"
#endif
