// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_board_contract_rev1.cpp
 * @brief Compile-time capability contract of the REAL rev1 board header
 * (tasks.md T016, feature 006).
 *
 * The host test app defines no board Kconfig choice, so CONFIG_BOARD_* is
 * absent from its sdkconfig.h — defining the rev1 selector before
 * including board/board.h compiles the REAL rev1 profile (not a copy)
 * into this translation unit. Its sibling
 * (test_board_contract_rev2.cpp) does the same for rev2, so the host
 * build asserts the board-header contract "both ways" without either
 * device toolchain.
 *
 * Passing = compiling: every check is a static_assert or preprocessor
 * #error — there is nothing to run. The AUTHORITATIVE verification of the
 * capability gating remains the dual-target CI build (quickstart.md §2,
 * SC-001): an unguarded BOARD_PIN_RESERVOIR_PUMP reference in app code
 * only fails THERE. This TU pins the header-level contract those builds
 * rely on.
 */

#define CONFIG_BOARD_REV1_DEVKIT 1
#include "board/board.h"

// rev1 is the two-pump bench node: the capability flag is set AND the pin
// exists (flag ⇒ pin, the board.h consistency assert's positive branch).
static_assert(BOARD_HAS_RESERVOIR_PUMP == 1,
              "rev1 board contract: two-pump node (capability flag set)");
#ifndef BOARD_PIN_RESERVOIR_PUMP
#error "rev1 board contract: BOARD_PIN_RESERVOIR_PUMP must be defined"
#endif
static_assert(BOARD_PIN_RESERVOIR_PUMP == 27,
              "rev1 board contract: reservoir pump pin (parity, "
              "src/main.cpp legacy pin table)");

// rev1 has no INA226 — neither the flag nor the address (flag=0 ⇒ address
// undefined, the RS485-DE enforcement pattern).
static_assert(BOARD_HAS_INA226 == 0,
              "rev1 board contract: no INA226 on the devkit rig");
#ifdef BOARD_INA226_ADDR
#error "rev1 board contract: BOARD_INA226_ADDR must NOT be defined"
#endif

// Level sensors: rev1 polarity is active HIGH (FW-5, non-inverting
// TXS0108E path) with no settle gating (permanently powered rail).
static_assert(BOARD_PIN_LEVEL_LOW == 32 && BOARD_PIN_LEVEL_HIGH == 33,
              "rev1 board contract: level pins (parity checklist line 95)");
static_assert(BOARD_LEVEL_ACTIVE_LOW == 0,
              "rev1 board contract: active HIGH (FW-5)");
static_assert(BOARD_LEVEL_SETTLE_MS == 0,
              "rev1 board contract: no settle gating (rail always on)");
static_assert(BOARD_LEVEL_DEBOUNCE_MS == 300,
              "rev1 board contract: 300 ms debounce window");

// Buttons: the rev1 devkit rig HAS both buttons, and feature 012 must not
// change a single rev1 value (FR-002 regression guard). The manual-watering
// button sits on IO5, the WiFi-config button on IO18 — the pins the boot
// path reads today (feature 007, parity checklist §7).
//
// NOTE for future readers: feature 012 reserves IO18/19/23/4/27 for the
// rev2 expansion header J7, and rev1 legitimately uses two of them (IO18
// config button, IO27 reservoir pump). That reservation is a rev2-ONLY
// invariant (data-model.md invariant 3) — never "fix" the rev1 values to
// satisfy it.
static_assert(BOARD_HAS_BTN_MANUAL == 1,
              "rev1 board contract: manual button present on the devkit rig");
#ifndef BOARD_PIN_BTN_MANUAL
#error "rev1 board contract: BOARD_PIN_BTN_MANUAL must be defined"
#endif
static_assert(BOARD_PIN_BTN_MANUAL == 5,
              "rev1 board contract: manual button pin unchanged (FR-002)");
static_assert(BOARD_HAS_BTN_CONFIG == 1,
              "rev1 board contract: config button present on the devkit rig");
#ifndef BOARD_PIN_BTN_CONFIG
#error "rev1 board contract: BOARD_PIN_BTN_CONFIG must be defined"
#endif
static_assert(BOARD_PIN_BTN_CONFIG == 18,
              "rev1 board contract: config button pin unchanged (FR-002, "
              "feature 007 boot provisioning path)");
