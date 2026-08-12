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

// FROZEN rev2 pin values (feature 012, FR-002). Every number below comes from
// the SYNC 1 map in hardware/rev2/design-notes/02-mcu.md §2.2 (frozen
// 2026-08-12): the board exists, so changing one here changes nothing in
// copper. Pinning them makes an accidental edit a build failure rather than a
// profile that silently disagrees with the PCB.
static_assert(BOARD_PIN_I2C_SDA == 21 && BOARD_PIN_I2C_SCL == 22,
              "rev2 board contract: I2C pins per the frozen SYNC 1 map");
static_assert(BOARD_PIN_RS485_TX == 16 && BOARD_PIN_RS485_RX == 17,
              "rev2 board contract: RS485 UART pins per the frozen SYNC 1 map");
static_assert(BOARD_RS485_UART_PORT == 2,
              "rev2 board contract: Modbus RTU on UART2 (parity: legacy "
              "Serial2, docs/parity-checklist.md §5)");
static_assert(BOARD_PIN_MAIN_PUMP == 26,
              "rev2 board contract: plant pump pin per the frozen SYNC 1 map");
static_assert(BOARD_PIN_LEVEL_LOW == 32 && BOARD_PIN_LEVEL_HIGH == 33,
              "rev2 board contract: level pins per the frozen SYNC 1 map");
static_assert(BOARD_PIN_STATUS_LED == 2,
              "rev2 board contract: status LED pin per the frozen SYNC 1 map");

// rev2 is the single-pump node: capability flag 0 AND the pin REMOVED
// (flag=0 ⇒ pin undefined — the compile-error enforcement this feature's
// US2 rests on; same pattern as BOARD_PIN_RS485_DE).
static_assert(BOARD_HAS_RESERVOIR_PUMP == 0,
              "rev2 board contract: single-pump node (master PRD FR4)");
#ifdef BOARD_PIN_RESERVOIR_PUMP
#error "rev2 board contract: BOARD_PIN_RESERVOIR_PUMP must NOT be defined \
(unguarded references must fail the build)"
#endif

// rev2 carries the pump INA226 at 0x40 (A0 = A1 = GND). The board-profile
// address map also lists 0x41 (solar INA226 — populated on this node, see
// 01-power.md §1.5; its driver lands in PR-14) and 0x77 (BME280).
// BOARD_INA226_ADDR names the PUMP monitor specifically.
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

// Buttons: the frozen rev2 board has NO manual or config button — only the
// BOOT/RESET switches (IO0/EN) and the status LED (feature 012, FR-001).
// Both pin macros are therefore removed, so the boot path's button block
// cannot compile on this board (the same enforcement pattern as the
// reservoir pump above). This matters beyond tidiness: the pin the old
// profile used for BTN_CONFIG is IO18 = EXP_SCK, an expansion-header signal
// the boot path must never read.
static_assert(BOARD_HAS_BTN_MANUAL == 0,
              "rev2 board contract: no manual button on the frozen rev2 board");
#ifdef BOARD_PIN_BTN_MANUAL
#error "rev2 board contract: BOARD_PIN_BTN_MANUAL must NOT be defined \
(unguarded references must fail the build)"
#endif
static_assert(BOARD_HAS_BTN_CONFIG == 0,
              "rev2 board contract: no config button on the frozen rev2 board");
#ifdef BOARD_PIN_BTN_CONFIG
#error "rev2 board contract: BOARD_PIN_BTN_CONFIG must NOT be defined \
(unguarded references must fail the build)"
#endif

// Power / rail signals the frozen rev2 board provides (feature 012,
// FR-005). Consumers (ADC calibration, PG monitoring, rail sequencing) are
// PR-14 scope; the profile only has to state the facts.
static_assert(BOARD_HAS_VBAT_SENSE == 1,
              "rev2 board contract: battery voltage sense present");
#ifndef BOARD_PIN_VBAT_SENSE
#error "rev2 board contract: BOARD_PIN_VBAT_SENSE must be defined"
#endif
static_assert(BOARD_PIN_VBAT_SENSE == 34,
              "rev2 board contract: VBAT_SENSE on IO34 (ADC1_CH6, input-only)");

static_assert(BOARD_HAS_PWR_PG == 1,
              "rev2 board contract: buck power-good present");
#ifndef BOARD_PIN_PWR_PG
#error "rev2 board contract: BOARD_PIN_PWR_PG must be defined"
#endif
static_assert(BOARD_PIN_PWR_PG == 35,
              "rev2 board contract: PWR_PG on IO35 (input-only, ext. pull-up)");

static_assert(BOARD_HAS_SENS_PWR_EN == 1,
              "rev2 board contract: switched sensor rail present");
#ifndef BOARD_PIN_SENS_PWR_EN
#error "rev2 board contract: BOARD_PIN_SENS_PWR_EN must be defined"
#endif
static_assert(BOARD_PIN_SENS_PWR_EN == 25,
              "rev2 board contract: SENS_PWR_EN on IO25 (output, rail OFF "
              "by hardware default)");

// Expansion reservation (feature 012, FR-004). J7 carries VSPI
// SCK=IO18, MISO=IO19, MOSI=IO23 plus CS=IO4 and IRQ=IO27
// (08-expansion.md §8.3); core firmware must not
// claim any of them on rev2. board.h enforces this too — this TU is the
// belt to that header's braces: the contract survives even if the header
// check is ever removed. NOTE: this is a rev2-ONLY invariant; rev1
// legitimately uses IO18 (config button) and IO27 (reservoir pump).
#define WS_REV2_NOT_EXPANSION(pin) \
    ((pin) != 18 && (pin) != 19 && (pin) != 23 && (pin) != 4 && (pin) != 27)

static_assert(WS_REV2_NOT_EXPANSION(BOARD_PIN_I2C_SDA) &&
                  WS_REV2_NOT_EXPANSION(BOARD_PIN_I2C_SCL),
              "rev2 board contract: I2C pins must stay off the expansion set");
static_assert(WS_REV2_NOT_EXPANSION(BOARD_PIN_RS485_TX) &&
                  WS_REV2_NOT_EXPANSION(BOARD_PIN_RS485_RX),
              "rev2 board contract: RS485 pins must stay off the expansion set");
static_assert(WS_REV2_NOT_EXPANSION(BOARD_PIN_MAIN_PUMP),
              "rev2 board contract: pump pin must stay off the expansion set");
static_assert(WS_REV2_NOT_EXPANSION(BOARD_PIN_LEVEL_LOW) &&
                  WS_REV2_NOT_EXPANSION(BOARD_PIN_LEVEL_HIGH),
              "rev2 board contract: level pins must stay off the expansion set");
static_assert(WS_REV2_NOT_EXPANSION(BOARD_PIN_STATUS_LED),
              "rev2 board contract: status LED must stay off the expansion set");
static_assert(WS_REV2_NOT_EXPANSION(BOARD_PIN_VBAT_SENSE) &&
                  WS_REV2_NOT_EXPANSION(BOARD_PIN_PWR_PG) &&
                  WS_REV2_NOT_EXPANSION(BOARD_PIN_SENS_PWR_EN),
              "rev2 board contract: power/rail pins must stay off the "
              "expansion set");

#undef WS_REV2_NOT_EXPANSION
