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

/* Buttons (manual watering trigger, WiFi config/AP mode).
 * Rev 1 has both physical buttons; the config button drives the boot
 * provisioning-force path (feature 007, docs/parity-checklist.md §7). */
#define BOARD_HAS_BTN_MANUAL            1
#define BOARD_PIN_BTN_MANUAL            5
#define BOARD_HAS_BTN_CONFIG            1
#define BOARD_PIN_BTN_CONFIG            18

/* Power/rail monitoring: none on rev1. The devkit rig runs off USB with a
 * permanently powered sensor rail, so battery sense, buck power-good and
 * sensor-rail enable do not exist here. The pin macros are deliberately NOT
 * defined (RS485-DE pattern): unguarded references fail the build. */
#define BOARD_HAS_VBAT_SENSE            0
#define BOARD_HAS_PWR_PG                0
#define BOARD_HAS_SENS_PWR_EN           0

#elif CONFIG_BOARD_REV2

/* ------------------------------------------------------------------------
 * Rev 2 — custom PCB (THVD1426 auto-direction RS485, INA226, CP2102N).
 * Every GPIO number below is FROZEN: it comes from the SYNC 1 map in
 * hardware/rev2/design-notes/02-mcu.md §2.2 (frozen 2026-08-12, all sheets
 * drawn, ERC clean, components ordered). Changing one here changes nothing
 * on the board — pin changes go schematic-first and re-open this profile
 * deliberately.
 *
 * MAINTENANCE SENTINEL: adding ANY new BOARD_PIN_* or BOARD_HAS_* to this
 * section requires hand edits elsewhere, because the preprocessor cannot
 * enumerate macros — nothing detects an omission:
 *   (a) add a new pin to the expansion-reservation check at the bottom of
 *       this header (BOARD_PIN_IS_EXPANSION list),
 *   (b) add a new pin to the frozen-value and expansion-set asserts in
 *       firmware/test_apps/host/main/test_board_contract_rev2.cpp, and
 *   (c) add a new BOARD_HAS_* flag to the capability-flag definedness check
 *       at the top of the sanity section below.
 * A macro left out of those lists is silently unguarded.
 * ------------------------------------------------------------------------ */

#define BOARD_NAME                      "rev2"

/* I2C bus (BME280, INA226 x2) — 02-mcu.md §2.2 SYNC 1 map
 * (frozen 2026-08-12). */
#define BOARD_PIN_I2C_SDA               21
#define BOARD_PIN_I2C_SCL               22

/* RS485 (THVD1426 with automatic direction control — no DE pin) —
 * 02-mcu.md §2.2 SYNC 1 map (frozen 2026-08-12). */
#define BOARD_PIN_RS485_TX              16
#define BOARD_PIN_RS485_RX              17
#define BOARD_HAS_RS485_DE              0
/* BOARD_PIN_RS485_DE is deliberately NOT defined when BOARD_HAS_RS485_DE
 * is 0: any reference that is not guarded by #if BOARD_HAS_RS485_DE becomes
 * a compile error instead of undefined behavior (e.g. 1ULL << -1) or a
 * silently dropped ESP_ERR_INVALID_ARG at runtime. */
/* Modbus RTU runs on UART2 at 9600 baud 8N1 (parity: legacy Serial2,
 * docs/parity-checklist.md §5) — a parity fact, not a pin-map fact. */
#define BOARD_RS485_UART_PORT           2

/* Pumps (MOSFET gates, active high).
 * Rev 2 is a SINGLE-PUMP node (master PRD FR4, final decision 2026-06-10):
 * only the plant pump exists. BOARD_PIN_RESERVOIR_PUMP is deliberately NOT
 * defined when BOARD_HAS_RESERVOIR_PUMP is 0: any reference that is not
 * guarded by #if BOARD_HAS_RESERVOIR_PUMP becomes a compile error instead
 * of driving a phantom GPIO (same enforcement pattern as
 * BOARD_PIN_RS485_DE above).
 * Pin: 02-mcu.md §2.2 SYNC 1 map (frozen 2026-08-12). */
#define BOARD_PIN_MAIN_PUMP             26
#define BOARD_HAS_RESERVOIR_PUMP        0

/* Reservoir level sensors (XKC-Y26), low/high mark with internal pull-ups
 * (redundant-but-harmless on top of the external 10 kΩ — rev2 design notes
 * §6.3, research.md R4).
 * Rev 2: sensor OUT routed through a 2N7002 inverter, so the GPIO is
 * active LOW (water present = LOW) — FW-5. See PRD FR5.
 * Settle: the XKC-Y26 needs ≥500 ms after its rail powers on before the
 * output is trustworthy (FW-3); rail control itself arrives in PR-14 —
 * this feature arms the gate once at boot.
 * Pins: 02-mcu.md §2.2 SYNC 1 map (frozen 2026-08-12). */
#define BOARD_PIN_LEVEL_LOW             32
#define BOARD_PIN_LEVEL_HIGH            33
#define BOARD_LEVEL_ACTIVE_LOW          1
#define BOARD_LEVEL_DEBOUNCE_MS         300
#define BOARD_LEVEL_SETTLE_MS           500

/* Pump current monitoring (INA226 on the shared I2C bus).
 * Rev 2 I2C address map (07-i2c-env.md §7.3, frozen 2026-08-12):
 *   0x40  INA226 pump monitor (A0 = A1 = GND)
 *   0x41  INA226 solar/panel telemetry (A0 → VS) — populated on this node
 *         (01-power.md §1.5: decision 2026-06-20, populate gate cleared
 *         2026-08-10; driver support PR-14)
 *   0x77  BME280 (SDO → VDDIO, rev1 parity; the driver still probes
 *         0x76 first and settles on whichever answers)
 * The ALERT pin is not connected — no Mask/Enable/Alert register use.
 * BOARD_INA226_ADDR names the PUMP monitor; the solar device gets its own
 * constant when PR-14 adds the second driver instance. */
#define BOARD_HAS_INA226                1
#define BOARD_INA226_ADDR               0x40

/* Status LED — 02-mcu.md §2.2 SYNC 1 map (frozen 2026-08-12). */
#define BOARD_PIN_STATUS_LED            2

/* Power and rail monitoring — rev 2 only.
 * Pins: 02-mcu.md §2.2 SYNC 1 map (frozen 2026-08-12); electrical
 * constraints: docs/rev2-firmware-notes.md FW-1/FW-3/FW-6.
 *
 * VBAT_SENSE (IO34): battery voltage through the 470 k/100 k divider.
 * Input-only pin on ADC1 — mandatory, since ADC2 is unusable while WiFi is
 * active. Readings compress above ~2.45 V at the pin (~14 V battery): the
 * ESP32 11 dB linear range ends there, so top-of-charge accuracy is reduced
 * (FW-1). Telemetry and soft-UVLO only — no safety decision depends on it.
 *
 * PWR_PG (IO35): 3V3 buck power-good, HIGH = in regulation. Input-only and
 * open-drain with an external pull-up (R30) — configure as a plain input;
 * IO34-39 have no internal pulls at all (FW-6).
 *
 * SENS_PWR_EN (IO25): enables the switched 12 V sensor domain (Q60 gate,
 * also the THVD1426 SHDN-bar). The rail is OFF by HARDWARE default — the
 * R61 gate pull-up holds the high-side switch off while the GPIO is hi-Z,
 * so no firmware action is needed to keep it off at boot, and this feature
 * deliberately does NOT drive the pin. Rail sequencing (assert, the 500 ms
 * XKC-Y26 settle before level reads are trustworthy per FW-3, and the IO17
 * pull-up rule of FW-2 while the domain is off) is PR-14 scope.
 *
 * Consumers of all three land in PR-14; the profile only states the facts. */
#define BOARD_HAS_VBAT_SENSE            1
#define BOARD_PIN_VBAT_SENSE            34
#define BOARD_HAS_PWR_PG                1
#define BOARD_PIN_PWR_PG                35
#define BOARD_HAS_SENS_PWR_EN           1
#define BOARD_PIN_SENS_PWR_EN           25

/* Buttons: rev 2 has NONE. The frozen board carries only the BOOT (IO0) and
 * RESET (EN) switches plus the status LED — no manual-watering and no
 * WiFi-config button. BOARD_PIN_BTN_MANUAL / BOARD_PIN_BTN_CONFIG are
 * therefore deliberately NOT defined when their flags are 0: any reference
 * that is not guarded by #if BOARD_HAS_BTN_* becomes a compile error instead
 * of reading a phantom GPIO (same enforcement pattern as
 * BOARD_PIN_RS485_DE / BOARD_PIN_RESERVOIR_PUMP above).
 *
 * This is a correctness fix, not cosmetics: the pin the profile previously
 * used for BTN_CONFIG is IO18 = EXP_SCK on the frozen board — an expansion
 * header signal. Reading it at boot could mistake expansion-bus traffic for
 * "operator holds the config button". Provisioning on rev2 is entered via
 * the credentials-absent path (feature 007); a BOOT-button trigger is a
 * possible future feature. */
#define BOARD_HAS_BTN_MANUAL            0
#define BOARD_HAS_BTN_CONFIG            0

#else
#error "No board selected: enable CONFIG_BOARD_REV1_DEVKIT or CONFIG_BOARD_REV2"
#endif

/* ------------------------------------------------------------------------
 * Compile-time board sanity checks (preprocessor — the values are macros).
 * A wrong or inconsistent pin table must fail the build, not the rig.
 * ------------------------------------------------------------------------ */

/* Every capability flag must be DEFINED, not merely 0 or 1. An undefined
 * macro evaluates to 0 in #if without a diagnostic, so a flag lost in an edit
 * would silently delete the behavior it gates instead of failing the build.
 * This check comes FIRST, ahead of every flag-guarded check below: those
 * checks are themselves gated on these flags, so an undefined flag would
 * otherwise silently switch OFF the very collision checks it is supposed to
 * enable. Ordering makes that structurally impossible. */
#if !defined(BOARD_HAS_BTN_MANUAL) || !defined(BOARD_HAS_BTN_CONFIG) ||   \
    !defined(BOARD_HAS_VBAT_SENSE) || !defined(BOARD_HAS_PWR_PG) ||      \
    !defined(BOARD_HAS_SENS_PWR_EN) || !defined(BOARD_HAS_RS485_DE) ||   \
    !defined(BOARD_HAS_RESERVOIR_PUMP) || !defined(BOARD_HAS_INA226)
#error "Board sanity: every capability flag must be defined (0 or 1)"
#endif

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
/* Buttons are optional hardware (rev2 has none), so the distinctness check
 * is guarded exactly like the reservoir pump's: with both flags at 0 the
 * undefined macros would otherwise compare 0 == 0 in #if and fire a
 * spurious error. */
#if BOARD_HAS_BTN_MANUAL && BOARD_HAS_BTN_CONFIG
#if BOARD_PIN_BTN_MANUAL == BOARD_PIN_BTN_CONFIG
#error "Board sanity: BOARD_PIN_BTN_MANUAL and BOARD_PIN_BTN_CONFIG must differ"
#endif
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

/* The rev2-only power/rail pins must not collide with a core function pin or
 * with each other. Each check is guarded by its capability flag, exactly like
 * the reservoir-pump checks above: where the signal does not exist the pin
 * macro is undefined and must stay a compile error when referenced — never be
 * papered over here. Without these, a typo that puts a rail signal on the
 * pump gate (SENS_PWR_EN == MAIN_PUMP) would compile silently. */
#if BOARD_HAS_VBAT_SENSE
#if (BOARD_PIN_VBAT_SENSE == BOARD_PIN_MAIN_PUMP) ||  \
    (BOARD_PIN_VBAT_SENSE == BOARD_PIN_LEVEL_LOW) ||  \
    (BOARD_PIN_VBAT_SENSE == BOARD_PIN_LEVEL_HIGH) || \
    (BOARD_PIN_VBAT_SENSE == BOARD_PIN_I2C_SDA) ||    \
    (BOARD_PIN_VBAT_SENSE == BOARD_PIN_I2C_SCL) ||    \
    (BOARD_PIN_VBAT_SENSE == BOARD_PIN_RS485_TX) ||   \
    (BOARD_PIN_VBAT_SENSE == BOARD_PIN_RS485_RX) ||   \
    (BOARD_PIN_VBAT_SENSE == BOARD_PIN_STATUS_LED)
#error "Board sanity: BOARD_PIN_VBAT_SENSE collides with a core function pin"
#endif
#endif
#if BOARD_HAS_PWR_PG
#if (BOARD_PIN_PWR_PG == BOARD_PIN_MAIN_PUMP) ||  \
    (BOARD_PIN_PWR_PG == BOARD_PIN_LEVEL_LOW) ||  \
    (BOARD_PIN_PWR_PG == BOARD_PIN_LEVEL_HIGH) || \
    (BOARD_PIN_PWR_PG == BOARD_PIN_I2C_SDA) ||    \
    (BOARD_PIN_PWR_PG == BOARD_PIN_I2C_SCL) ||    \
    (BOARD_PIN_PWR_PG == BOARD_PIN_RS485_TX) ||   \
    (BOARD_PIN_PWR_PG == BOARD_PIN_RS485_RX) ||   \
    (BOARD_PIN_PWR_PG == BOARD_PIN_STATUS_LED)
#error "Board sanity: BOARD_PIN_PWR_PG collides with a core function pin"
#endif
#endif
#if BOARD_HAS_SENS_PWR_EN
#if (BOARD_PIN_SENS_PWR_EN == BOARD_PIN_MAIN_PUMP) ||  \
    (BOARD_PIN_SENS_PWR_EN == BOARD_PIN_LEVEL_LOW) ||  \
    (BOARD_PIN_SENS_PWR_EN == BOARD_PIN_LEVEL_HIGH) || \
    (BOARD_PIN_SENS_PWR_EN == BOARD_PIN_I2C_SDA) ||    \
    (BOARD_PIN_SENS_PWR_EN == BOARD_PIN_I2C_SCL) ||    \
    (BOARD_PIN_SENS_PWR_EN == BOARD_PIN_RS485_TX) ||   \
    (BOARD_PIN_SENS_PWR_EN == BOARD_PIN_RS485_RX) ||   \
    (BOARD_PIN_SENS_PWR_EN == BOARD_PIN_STATUS_LED)
#error "Board sanity: BOARD_PIN_SENS_PWR_EN collides with a core function pin"
#endif
#endif
#if BOARD_HAS_VBAT_SENSE && BOARD_HAS_PWR_PG
#if BOARD_PIN_VBAT_SENSE == BOARD_PIN_PWR_PG
#error "Board sanity: BOARD_PIN_VBAT_SENSE and BOARD_PIN_PWR_PG must differ"
#endif
#endif
#if BOARD_HAS_VBAT_SENSE && BOARD_HAS_SENS_PWR_EN
#if BOARD_PIN_VBAT_SENSE == BOARD_PIN_SENS_PWR_EN
#error "Board sanity: BOARD_PIN_VBAT_SENSE and BOARD_PIN_SENS_PWR_EN must differ"
#endif
#endif
#if BOARD_HAS_PWR_PG && BOARD_HAS_SENS_PWR_EN
#if BOARD_PIN_PWR_PG == BOARD_PIN_SENS_PWR_EN
#error "Board sanity: BOARD_PIN_PWR_PG and BOARD_PIN_SENS_PWR_EN must differ"
#endif
#endif
/* Latent cross-check: unreachable on both boards today, since no board has
 * both signals (rev1 DE=25 without a sensor rail, rev2 SENS_PWR_EN=25 without
 * a DE pin). A future board combining them would land both on IO25 and
 * collide silently — the double guard keeps the check honest until then. */
#if BOARD_HAS_SENS_PWR_EN && BOARD_HAS_RS485_DE
#if BOARD_PIN_SENS_PWR_EN == BOARD_PIN_RS485_DE
#error "Board sanity: BOARD_PIN_SENS_PWR_EN collides with the RS485 DE pin"
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

/* Feature flag consistency: BOARD_HAS_BTN_* == 1 iff the button pin exists
 * (rev2 has no buttons — same pattern as RS485 DE) */
#if BOARD_HAS_BTN_MANUAL && !defined(BOARD_PIN_BTN_MANUAL)
#error "Board sanity: BOARD_HAS_BTN_MANUAL is 1 but BOARD_PIN_BTN_MANUAL is not defined"
#endif
#if !BOARD_HAS_BTN_MANUAL && defined(BOARD_PIN_BTN_MANUAL)
#error "Board sanity: BOARD_PIN_BTN_MANUAL is defined but BOARD_HAS_BTN_MANUAL is 0"
#endif
#if BOARD_HAS_BTN_CONFIG && !defined(BOARD_PIN_BTN_CONFIG)
#error "Board sanity: BOARD_HAS_BTN_CONFIG is 1 but BOARD_PIN_BTN_CONFIG is not defined"
#endif
#if !BOARD_HAS_BTN_CONFIG && defined(BOARD_PIN_BTN_CONFIG)
#error "Board sanity: BOARD_PIN_BTN_CONFIG is defined but BOARD_HAS_BTN_CONFIG is 0"
#endif

/* Feature flag consistency: the rev2-only power/rail signals. Same pattern —
 * on boards without them the pin macros stay undefined so a PR-14 driver
 * cannot reference them unguarded. */
#if BOARD_HAS_VBAT_SENSE && !defined(BOARD_PIN_VBAT_SENSE)
#error "Board sanity: BOARD_HAS_VBAT_SENSE is 1 but BOARD_PIN_VBAT_SENSE is not defined"
#endif
#if !BOARD_HAS_VBAT_SENSE && defined(BOARD_PIN_VBAT_SENSE)
#error "Board sanity: BOARD_PIN_VBAT_SENSE is defined but BOARD_HAS_VBAT_SENSE is 0"
#endif
#if BOARD_HAS_PWR_PG && !defined(BOARD_PIN_PWR_PG)
#error "Board sanity: BOARD_HAS_PWR_PG is 1 but BOARD_PIN_PWR_PG is not defined"
#endif
#if !BOARD_HAS_PWR_PG && defined(BOARD_PIN_PWR_PG)
#error "Board sanity: BOARD_PIN_PWR_PG is defined but BOARD_HAS_PWR_PG is 0"
#endif
#if BOARD_HAS_SENS_PWR_EN && !defined(BOARD_PIN_SENS_PWR_EN)
#error "Board sanity: BOARD_HAS_SENS_PWR_EN is 1 but BOARD_PIN_SENS_PWR_EN is not defined"
#endif
#if !BOARD_HAS_SENS_PWR_EN && defined(BOARD_PIN_SENS_PWR_EN)
#error "Board sanity: BOARD_PIN_SENS_PWR_EN is defined but BOARD_HAS_SENS_PWR_EN is 0"
#endif

/* Feature flag consistency: BOARD_HAS_INA226 == 1 iff the address exists */
#if BOARD_HAS_INA226 && !defined(BOARD_INA226_ADDR)
#error "Board sanity: BOARD_HAS_INA226 is 1 but BOARD_INA226_ADDR is not defined"
#endif
#if !BOARD_HAS_INA226 && defined(BOARD_INA226_ADDR)
#error "Board sanity: BOARD_INA226_ADDR is defined but BOARD_HAS_INA226 is 0"
#endif

/* Expansion-header reservation — REV 2 ONLY.
 * J7 carries VSPI SCK/MISO/MOSI plus CS and IRQ on IO18/19/23/4/27 — read
 * pairwise: SCK=IO18, MISO=IO19, MOSI=IO23, CS=IO4, IRQ=IO27
 * (08-expansion.md §8.3). Core
 * firmware must never claim one of them, or an attached expansion device
 * fights the core (and its bus traffic can be misread as core input).
 * This is deliberately NOT a cross-board check: rev1 legitimately uses IO18
 * (config button) and IO27 (reservoir pump) — the reservation is a property
 * of the rev2 board, not of the firmware. */
#if CONFIG_BOARD_REV2
#define BOARD_PIN_IS_EXPANSION(pin)                                     \
    ((pin) == 18 || (pin) == 19 || (pin) == 23 || (pin) == 4 || (pin) == 27)

#if BOARD_PIN_IS_EXPANSION(BOARD_PIN_I2C_SDA) ||    \
    BOARD_PIN_IS_EXPANSION(BOARD_PIN_I2C_SCL) ||    \
    BOARD_PIN_IS_EXPANSION(BOARD_PIN_RS485_TX) ||   \
    BOARD_PIN_IS_EXPANSION(BOARD_PIN_RS485_RX) ||   \
    BOARD_PIN_IS_EXPANSION(BOARD_PIN_MAIN_PUMP) ||  \
    BOARD_PIN_IS_EXPANSION(BOARD_PIN_LEVEL_LOW) ||  \
    BOARD_PIN_IS_EXPANSION(BOARD_PIN_LEVEL_HIGH) || \
    BOARD_PIN_IS_EXPANSION(BOARD_PIN_STATUS_LED) || \
    BOARD_PIN_IS_EXPANSION(BOARD_PIN_VBAT_SENSE) || \
    BOARD_PIN_IS_EXPANSION(BOARD_PIN_PWR_PG) ||     \
    BOARD_PIN_IS_EXPANSION(BOARD_PIN_SENS_PWR_EN)
#error "Board sanity: a rev2 core pin lands on the reserved expansion set (IO18/19/23/4/27)"
#endif

/* Optional pins too — checked under their capability flags so an undefined
 * macro is never evaluated. None of these exist on rev2 today; the checks
 * exist so a future re-addition cannot silently take an expansion pin. */
#if BOARD_HAS_RS485_DE && BOARD_PIN_IS_EXPANSION(BOARD_PIN_RS485_DE)
#error "Board sanity: BOARD_PIN_RS485_DE lands on the reserved expansion set"
#endif
#if BOARD_HAS_RESERVOIR_PUMP && BOARD_PIN_IS_EXPANSION(BOARD_PIN_RESERVOIR_PUMP)
#error "Board sanity: BOARD_PIN_RESERVOIR_PUMP lands on the reserved expansion set"
#endif
#if BOARD_HAS_BTN_MANUAL && BOARD_PIN_IS_EXPANSION(BOARD_PIN_BTN_MANUAL)
#error "Board sanity: BOARD_PIN_BTN_MANUAL lands on the reserved expansion set"
#endif
#if BOARD_HAS_BTN_CONFIG && BOARD_PIN_IS_EXPANSION(BOARD_PIN_BTN_CONFIG)
#error "Board sanity: BOARD_PIN_BTN_CONFIG lands on the reserved expansion set"
#endif

#undef BOARD_PIN_IS_EXPANSION
#endif /* CONFIG_BOARD_REV2 */

#endif /* WATERINGSYSTEM_BOARD_BOARD_H */
