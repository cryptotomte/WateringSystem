/**
 * @file board.h
 * @brief Board abstraction: pin mapping and feature flags per board revision.
 *
 * The board revision is selected at configure time via Kconfig
 * (menu "WateringSystem" -> "Board revision"), typically through the
 * sdkconfig.board.* overlay files:
 *
 *   idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.rev2" build
 *
 * All application and driver code must take pin numbers and polarity from
 * this header — never hard-code GPIO numbers elsewhere.
 */

#ifndef WATERINGSYSTEM_BOARD_BOARD_H
#define WATERINGSYSTEM_BOARD_BOARD_H

#include "sdkconfig.h"

#if CONFIG_BOARD_REV1_DEVKIT

/* ------------------------------------------------------------------------
 * Rev 1 — ESP32 devkit + breakout board (TXS0108E level shifter + SP3485)
 * Pins follow the running Arduino firmware (src/main.cpp), the
 * field-verified source of truth. Note: docs/hardware.md lists RS485
 * TX/RX swapped — see docs/parity-checklist.md QUIRK 6.
 * ------------------------------------------------------------------------ */

#define BOARD_NAME                      "rev1_devkit"

/* I2C bus (BME280) */
#define BOARD_PIN_I2C_SDA               21
#define BOARD_PIN_I2C_SCL               22

/* RS485 (SP3485 via TXS0108E, manual direction control).
 * TX=16/RX=17 per src/main.cpp PIN_RS485_TX/PIN_RS485_RX (source of truth;
 * docs/hardware.md has them swapped — docs/parity-checklist.md QUIRK 6). */
#define BOARD_PIN_RS485_TX              16
#define BOARD_PIN_RS485_RX              17
#define BOARD_HAS_RS485_DE              1
#define BOARD_PIN_RS485_DE              25
/* Modbus RTU runs on UART2 at 9600 baud 8N1 (parity: legacy Serial2,
 * docs/parity-checklist.md §5). */
#define BOARD_RS485_UART_PORT           2

/* Pumps (MOSFET gates, active high).
 * Rev 1 has both pumps (plant + reservoir fill) — two-pump topology. */
#define BOARD_PIN_MAIN_PUMP             26
#define BOARD_HAS_RESERVOIR_PUMP        1
#define BOARD_PIN_RESERVOIR_PUMP        27

/* Reservoir level sensors (XKC-Y26), low/high mark on 32/33 with internal
 * pull-ups (parity: src/main.cpp:37-38, 231-233; docs/parity-checklist.md
 * line 95).
 * Rev 1: sensor OUT routed non-inverting through TXS0108E. XKC-Y26 OUT is
 * active HIGH (water present = HIGH), so the GPIO is active HIGH (FW-5).
 * Debounce: stability window before a reported state change (deliberate
 * divergence from legacy's bare reads — docs/parity-checklist.md §6); a
 * hardware property of the sensor class, hence a board macro, not Kconfig.
 * Settle: 0 ms — the rev1 sensor rail is permanently on (FW-3 applies to
 * rev2's switched rail only); debounce warm-up subsumes settle here. */
#define BOARD_PIN_LEVEL_LOW             32
#define BOARD_PIN_LEVEL_HIGH            33
#define BOARD_LEVEL_ACTIVE_LOW          0
#define BOARD_LEVEL_DEBOUNCE_MS         300
#define BOARD_LEVEL_SETTLE_MS           0

/* Pump current monitoring: none on rev1 (BOARD_INA226_ADDR is deliberately
 * NOT defined — unguarded references fail the build, RS485-DE pattern). */
#define BOARD_HAS_INA226                0

/* Status LED */
#define BOARD_PIN_STATUS_LED            2

/* Buttons (manual watering trigger, WiFi config/AP mode) */
#define BOARD_PIN_BTN_MANUAL            5
#define BOARD_PIN_BTN_CONFIG            18

#elif CONFIG_BOARD_REV2

/* ------------------------------------------------------------------------
 * Rev 2 — custom PCB (THVD1426 auto-direction RS485, INA226, CP2102N).
 * GPIO numbers provisionally mirror rev 1 until the rev 2 pin map is frozen.
 * ------------------------------------------------------------------------ */

#define BOARD_NAME                      "rev2"

/* I2C bus (BME280, INA226) */
#define BOARD_PIN_I2C_SDA               21  // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1
#define BOARD_PIN_I2C_SCL               22  // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1

/* RS485 (THVD1426 with automatic direction control — no DE pin).
 * UART pins provisionally mirror rev 1 (TX=16/RX=17 per src/main.cpp). */
#define BOARD_PIN_RS485_TX              16  // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1
#define BOARD_PIN_RS485_RX              17  // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1
#define BOARD_HAS_RS485_DE              0
/* BOARD_PIN_RS485_DE is deliberately NOT defined when BOARD_HAS_RS485_DE
 * is 0: any reference that is not guarded by #if BOARD_HAS_RS485_DE becomes
 * a compile error instead of undefined behavior (e.g. 1ULL << -1) or a
 * silently dropped ESP_ERR_INVALID_ARG at runtime. */
/* Modbus RTU runs on UART2 at 9600 baud 8N1 (parity: legacy Serial2,
 * docs/parity-checklist.md §5). The UART number is a parity fact, not part
 * of the provisional rev2 pin map — no TODO(SYNC1). */
#define BOARD_RS485_UART_PORT           2

/* Pumps (MOSFET gates, active high).
 * Rev 2 is a SINGLE-PUMP node (master PRD FR4, final decision 2026-06-10):
 * only the plant pump exists. BOARD_PIN_RESERVOIR_PUMP is deliberately NOT
 * defined when BOARD_HAS_RESERVOIR_PUMP is 0: any reference that is not
 * guarded by #if BOARD_HAS_RESERVOIR_PUMP becomes a compile error instead
 * of driving a phantom GPIO (same enforcement pattern as
 * BOARD_PIN_RS485_DE above). */
#define BOARD_PIN_MAIN_PUMP             26  // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1
#define BOARD_HAS_RESERVOIR_PUMP        0

/* Reservoir level sensors (XKC-Y26), low/high mark with internal pull-ups
 * (redundant-but-harmless on top of the external 10 kΩ — rev2 design notes
 * §6.3, research.md R4).
 * Rev 2: sensor OUT routed through a 2N7002 inverter, so the GPIO is
 * active LOW (water present = LOW) — FW-5. See PRD FR5.
 * Settle: the XKC-Y26 needs ≥500 ms after its rail powers on before the
 * output is trustworthy (FW-3); rail control itself arrives in PR-14 —
 * this feature arms the gate once at boot. */
#define BOARD_PIN_LEVEL_LOW             32  // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1
#define BOARD_PIN_LEVEL_HIGH            33  // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1
#define BOARD_LEVEL_ACTIVE_LOW          1
#define BOARD_LEVEL_DEBOUNCE_MS         300
#define BOARD_LEVEL_SETTLE_MS           500

/* Pump current monitoring (INA226 on the shared I2C bus).
 * Rev 2 I2C address map (design notes §5.2.2):
 *   0x40  INA226 pump monitor (A0 = A1 = GND)
 *   0x41  reserved — solar-input INA226 footprint, DNP
 *   0x76 / 0x77  BME280
 * The ALERT pin is not connected — no Mask/Enable/Alert register use. */
#define BOARD_HAS_INA226                1
#define BOARD_INA226_ADDR               0x40

/* Status LED */
#define BOARD_PIN_STATUS_LED            2   // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1

/* Buttons (manual watering trigger, WiFi config/AP mode) */
#define BOARD_PIN_BTN_MANUAL            5   // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1
#define BOARD_PIN_BTN_CONFIG            18  // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1

#else
#error "No board selected: enable CONFIG_BOARD_REV1_DEVKIT or CONFIG_BOARD_REV2"
#endif

/* ------------------------------------------------------------------------
 * Compile-time board sanity checks (preprocessor — the values are macros).
 * A wrong or inconsistent pin table must fail the build, not the rig.
 * ------------------------------------------------------------------------ */

/* Pin distinctness within each function group. Checks that reference the
 * reservoir pump pin are guarded: on single-pump boards the pin does not
 * exist (BOARD_HAS_RESERVOIR_PUMP == 0), and an unguarded reference must
 * stay a compile error — never be papered over here. */
#if BOARD_HAS_RESERVOIR_PUMP
#if BOARD_PIN_MAIN_PUMP == BOARD_PIN_RESERVOIR_PUMP
#error "Board sanity: BOARD_PIN_MAIN_PUMP and BOARD_PIN_RESERVOIR_PUMP must differ"
#endif
#endif
#if BOARD_PIN_LEVEL_LOW == BOARD_PIN_LEVEL_HIGH
#error "Board sanity: BOARD_PIN_LEVEL_LOW and BOARD_PIN_LEVEL_HIGH must differ"
#endif
#if BOARD_PIN_BTN_MANUAL == BOARD_PIN_BTN_CONFIG
#error "Board sanity: BOARD_PIN_BTN_MANUAL and BOARD_PIN_BTN_CONFIG must differ"
#endif

/* Pumps must not share a pin with the level sensors */
#if (BOARD_PIN_MAIN_PUMP == BOARD_PIN_LEVEL_LOW) || \
    (BOARD_PIN_MAIN_PUMP == BOARD_PIN_LEVEL_HIGH)
#error "Board sanity: pump pins collide with level sensor pins"
#endif
#if BOARD_HAS_RESERVOIR_PUMP
#if (BOARD_PIN_RESERVOIR_PUMP == BOARD_PIN_LEVEL_LOW) || \
    (BOARD_PIN_RESERVOIR_PUMP == BOARD_PIN_LEVEL_HIGH)
#error "Board sanity: pump pins collide with level sensor pins"
#endif
#endif

/* Level sensors must not share a pin with the I2C bus or the RS485 UART */
#if (BOARD_PIN_LEVEL_LOW == BOARD_PIN_I2C_SDA) || \
    (BOARD_PIN_LEVEL_LOW == BOARD_PIN_I2C_SCL) || \
    (BOARD_PIN_LEVEL_HIGH == BOARD_PIN_I2C_SDA) || \
    (BOARD_PIN_LEVEL_HIGH == BOARD_PIN_I2C_SCL)
#error "Board sanity: level sensor pins collide with I2C pins"
#endif
#if (BOARD_PIN_LEVEL_LOW == BOARD_PIN_RS485_TX) || \
    (BOARD_PIN_LEVEL_LOW == BOARD_PIN_RS485_RX) || \
    (BOARD_PIN_LEVEL_HIGH == BOARD_PIN_RS485_TX) || \
    (BOARD_PIN_LEVEL_HIGH == BOARD_PIN_RS485_RX)
#error "Board sanity: level sensor pins collide with RS485 UART pins"
#endif
#if BOARD_HAS_RS485_DE
#if (BOARD_PIN_LEVEL_LOW == BOARD_PIN_RS485_DE) || \
    (BOARD_PIN_LEVEL_HIGH == BOARD_PIN_RS485_DE)
#error "Board sanity: level sensor pins collide with the RS485 DE pin"
#endif
#endif

/* Feature flag consistency: BOARD_HAS_RS485_DE == 1 iff the DE pin exists */
#if BOARD_HAS_RS485_DE && !defined(BOARD_PIN_RS485_DE)
#error "Board sanity: BOARD_HAS_RS485_DE is 1 but BOARD_PIN_RS485_DE is not defined"
#endif
#if !BOARD_HAS_RS485_DE && defined(BOARD_PIN_RS485_DE)
#error "Board sanity: BOARD_PIN_RS485_DE is defined but BOARD_HAS_RS485_DE is 0"
#endif

/* Feature flag consistency: BOARD_HAS_RESERVOIR_PUMP == 1 iff the pump pin
 * exists (single-pump decision, master PRD FR4 — same pattern as RS485 DE) */
#if BOARD_HAS_RESERVOIR_PUMP && !defined(BOARD_PIN_RESERVOIR_PUMP)
#error "Board sanity: BOARD_HAS_RESERVOIR_PUMP is 1 but BOARD_PIN_RESERVOIR_PUMP is not defined"
#endif
#if !BOARD_HAS_RESERVOIR_PUMP && defined(BOARD_PIN_RESERVOIR_PUMP)
#error "Board sanity: BOARD_PIN_RESERVOIR_PUMP is defined but BOARD_HAS_RESERVOIR_PUMP is 0"
#endif

/* Feature flag consistency: BOARD_HAS_INA226 == 1 iff the address exists */
#if BOARD_HAS_INA226 && !defined(BOARD_INA226_ADDR)
#error "Board sanity: BOARD_HAS_INA226 is 1 but BOARD_INA226_ADDR is not defined"
#endif
#if !BOARD_HAS_INA226 && defined(BOARD_INA226_ADDR)
#error "Board sanity: BOARD_INA226_ADDR is defined but BOARD_HAS_INA226 is 0"
#endif

#endif /* WATERINGSYSTEM_BOARD_BOARD_H */
