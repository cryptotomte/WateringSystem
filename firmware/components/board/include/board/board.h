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

/* Pumps (MOSFET gates, active high) */
#define BOARD_PIN_MAIN_PUMP             26
#define BOARD_PIN_RESERVOIR_PUMP        27

/* Reservoir level sensors (XKC-Y26).
 * Rev 1: sensor OUT routed non-inverting through TXS0108E.
 * XKC-Y26 OUT is active HIGH (water present = HIGH), so GPIO is active HIGH. */
#define BOARD_PIN_LEVEL_LOW             32
#define BOARD_PIN_LEVEL_HIGH            33
#define BOARD_LEVEL_SENSOR_ACTIVE_LOW   0

/* Pump current monitoring */
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

/* Pumps (MOSFET gates, active high) */
#define BOARD_PIN_MAIN_PUMP             26  // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1
#define BOARD_PIN_RESERVOIR_PUMP        27  // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1

/* Reservoir level sensors (XKC-Y26).
 * Rev 2: sensor OUT routed through a 2N7002 inverter, so GPIO is active LOW
 * (water present = LOW). See PRD FR5. */
#define BOARD_PIN_LEVEL_LOW             32  // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1
#define BOARD_PIN_LEVEL_HIGH            33  // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1
#define BOARD_LEVEL_SENSOR_ACTIVE_LOW   1

/* Pump current monitoring (one INA226 per pump on the I2C bus) */
#define BOARD_HAS_INA226                1   // TODO(SYNC1): final rev2 pin map frozen at hardware sync 1

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

/* Pin distinctness within each function group */
#if BOARD_PIN_MAIN_PUMP == BOARD_PIN_RESERVOIR_PUMP
#error "Board sanity: BOARD_PIN_MAIN_PUMP and BOARD_PIN_RESERVOIR_PUMP must differ"
#endif
#if BOARD_PIN_LEVEL_LOW == BOARD_PIN_LEVEL_HIGH
#error "Board sanity: BOARD_PIN_LEVEL_LOW and BOARD_PIN_LEVEL_HIGH must differ"
#endif
#if BOARD_PIN_BTN_MANUAL == BOARD_PIN_BTN_CONFIG
#error "Board sanity: BOARD_PIN_BTN_MANUAL and BOARD_PIN_BTN_CONFIG must differ"
#endif

/* Pumps must not share a pin with the level sensors */
#if (BOARD_PIN_MAIN_PUMP == BOARD_PIN_LEVEL_LOW) || \
    (BOARD_PIN_MAIN_PUMP == BOARD_PIN_LEVEL_HIGH) || \
    (BOARD_PIN_RESERVOIR_PUMP == BOARD_PIN_LEVEL_LOW) || \
    (BOARD_PIN_RESERVOIR_PUMP == BOARD_PIN_LEVEL_HIGH)
#error "Board sanity: pump pins collide with level sensor pins"
#endif

/* Feature flag consistency: BOARD_HAS_RS485_DE == 1 iff the DE pin exists */
#if BOARD_HAS_RS485_DE && !defined(BOARD_PIN_RS485_DE)
#error "Board sanity: BOARD_HAS_RS485_DE is 1 but BOARD_PIN_RS485_DE is not defined"
#endif
#if !BOARD_HAS_RS485_DE && defined(BOARD_PIN_RS485_DE)
#error "Board sanity: BOARD_PIN_RS485_DE is defined but BOARD_HAS_RS485_DE is 0"
#endif

#endif /* WATERINGSYSTEM_BOARD_BOARD_H */
