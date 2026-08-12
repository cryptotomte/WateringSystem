# Block 2 — MCU (ESP32-WROOM-32UE)

**Status:** Draft for review · 2026-06-23
**Scope:** ESP32-WROOM-32UE module, power/decoupling, EN power-on-reset RC,
boot/auto-program interface (to block 3), status LED, JTAG header, and the
**SYNC 1 GPIO assignment** — the cross-cutting pin map every other block waits on.
**Out of scope:** USB-UART bridge + auto-program transistors (block 3); the actual
connector to USB (block 3); expansion header J7 footprint (block 8 — but its GPIOs
are reserved here).

**Refdes note:** ESP32 = **U1**, buck = **U4** (MP2393) — both set correctly in the
schematics (verified 2026-06-23). New block-2 parts use the 40-series (R40…, C40…)
to avoid the 20–32 range block 1 grew into.

---

## 2.0 ESP32-WROOM symbol — pin electrical types (curate first)

The imported module symbol's pins are likely `unspecified`. Set them per this
table (same exercise as INA226/MP2393/THVD1426). Grouped by category — the module
has ~38 pins:

| Module pin(s) | Electrical type | Note |
|---|---|---|
| `3V3` / VDD | `power_input` | module supply (3.0–3.6 V) |
| `GND` (all + thermal pad) | `power_input` | |
| `EN` | `input` | chip enable (ext RC + pullup) |
| `IO0` | `bidirectional` | **strapping** (boot); boot control |
| `IO1` (U0TXD) | `bidirectional` | UART0 TX (programming) |
| `IO3` (U0RXD) | `bidirectional` | UART0 RX (programming) |
| `IO2` | `bidirectional` | **strapping**; status LED |
| `IO4`,`IO5`,`IO12`–`IO19`,`IO21`–`IO27`,`IO32`,`IO33` | `bidirectional` | general GPIO (5/12/15 are strapping — see §2.2) |
| `IO34`,`IO35`,`IO36`(VP),`IO39`(VN) | `input` | **input-only** — no output, no internal pull |
| `IO6`–`IO11` (SD0–3/CMD/CLK) | `passive` *(or no_connect)* | internal SPI flash — **DO NOT USE** |

Rule of thumb: power pins → `power_input`, EN → `input`, normal GPIO →
`bidirectional`, the four input-only pins → `input`, flash pins → `passive`/NC.

---

## 2.1 Component list

| Ref | Qty | Component | Value / type | Package | Function |
|---|---|---|---|---|---|
| U1 | 1 | ESP32-WROOM-32UE-N4 | 4 MB flash, no PSRAM, U.FL ext-antenna | Module | MCU *(−32E-N4 = PCB-antenna proto alt; both no-PSRAM — see §2.2 PSRAM note)* |
| C40 | 1 | Ceramic X7R | 10 µF, 10 V | 0805 | Module bulk decoupling |
| C41 | 1 | Ceramic X7R | 100 nF, 50 V | 0603 | Module HF decoupling (at 3V3 pin) |
| C42 | 1 | Ceramic X5R | 1 µF, 50 V | 0603 | EN power-on-reset RC (with R11) — 1 µF sized for auto-reset (block 3 §3.3.5) |
| C43 | 1 | Ceramic X7R | 1 nF | 0603 | BOOT noise filter *(optional)* |
| R11 | 1 | Resistor | 10 kΩ | 0603 | EN pullup → 3V3 |
| R12 | 1 | Resistor | 10 kΩ | 0603 | BOOT pullup → 3V3 |
| R40 | 1 | Resistor | 1 kΩ | 0603 | Status-LED series |
| LED2 | 1 | LED green (Würth 150080VS75000) | — | 0805 | Status LED (IO2) |
| SW1 | 1 | Tactile switch | — | SMD 5.1×5.1 | BOOT (IO0 → GND) |
| SW2 | 1 | Tactile switch | — | SMD 5.1×5.1 | RESET (EN → GND) |
| J6 | 1 | Pin header 1×6 | 2.54 mm | THT | JTAG breakout (generic KiCad header — see §2.3.6) |

*(R11 = EN pullup, R12 = BOOT pullup — two separate 10 kΩ parts (was R11a/R11b;
the a/b suffix isn't valid KiCad annotation, so each gets its own number).
LCSC numbers added to the master BOM when block 2 lands — these are generic Basic
parts except U1.)*

---

## 2.2 SYNC 1 — GPIO assignment

The keystone map. Honors ESP32-WROOM constraints and keeps rev1 pin choices where
free (familiarity + the parity-checklist references them). **`VBAT_SENSE` must be
ADC1** (ADC2 is unusable with WiFi active).

**This table doubles as the hierarchical-labels list for the MCU sheet:** the `Net`
column = the hierarchical label, the `Dir` column = its shape (in → `input`, out →
`output`, bidir → `bidirectional`; `EN`/`BOOT` shape = **bidirectional** — shared nodes
with block 3). `STATUS_LED` and the `JTAG` IO12–15 are **local** to this sheet (LED /
J6) — no hierarchical label. **Rails (`+3V3`, `GND`) are global power symbols, not
labels.**

| Function | Net | GPIO | Dir | Note |
|---|---|---|---|---|
| Battery voltage sense | `VBAT_SENSE` | **IO34** | in | ADC1_CH6, input-only ✓ (ADC1 satisfied) |
| Buck power-good | `PWR_PG` | **IO35** | in | input-only; ext pullup R30 (no internal pull needed) |
| Sensor-rail enable | `SENS_PWR_EN` | **IO25** | out | default LOW at boot → rail OFF |
| RS485 TX (UART2) | `RS485_TX` | **IO16** | out | ESP TX (rev1 parity; code truth TX=16) |
| RS485 RX (UART2) | `RS485_RX` | **IO17** | in | ESP RX (rev1 parity; code truth RX=17) |
| Pump enable | `PUMP_EN` | **IO26** | out | **OFF at boot (safety)**; rev1 main-pump pin |
| Reservoir level LOW | `RESERVOIR_LOW_LEVEL` | **IO32** | in | internal pullup; **active-LOW on rev2** (2N7002 inv); matches rev1 |
| Reservoir level HIGH | `RESERVOIR_HIGH_LEVEL` | **IO33** | in | internal pullup; active-LOW on rev2; matches rev1 |
| I²C SDA | `I2C_SDA` | **IO21** | bidir | rev1 parity (pull-ups in block 7; two separate bidir nets, not a KiCad bus) |
| I²C SCL | `I2C_SCL` | **IO22** | bidir | rev1 parity |
| Status LED | `STATUS_LED` | **IO2** | out | strapping; LED→GND OK at boot; rev1 parity |
| Prog UART TX | `U0TXD` | **IO1** | out | UART0 (fixed) → block 3 |
| Prog UART RX | `U0RXD` | **IO3** | in | UART0 (fixed) → block 3 |
| Boot | `BOOT` | **IO0** | bidir | strapping; BOOT btn + auto-program; pullup R12 |
| Reset | `EN` | **EN** | bidir | RESET btn + auto-program; pullup R11 + RC |
| JTAG | — | **IO12–IO15** | — | J6; **local** (no hierarchical label); IO12/IO15 strapping — see below |
| Expansion (J7) | `EXP_*` | **IO18, IO19, IO23, IO4, IO27** | bidir | locked: VSPI SCK/MOSI/MISO + CS/IRQ (`08-expansion.md`) |

**Free/spare after assignment:** IO5 (strapping), IO13/IO14 (if JTAG unused),
IO36(VP), IO39(VN, input-only). (IO4/IO27 now on the expansion header J7.)

### Strapping-pin cautions (ESP32-WROOM)
- **IO0** — boot select. Pullup (R12) + BOOT button to GND. Standard. ✓
- **IO2** — must be LOW/floating at boot. Status LED to GND keeps it low → OK (rev1 proven). Don't add a pull-up here.
- **IO5** — must be HIGH at boot (internal pull-up does this). Leave spare/undriven, or only use for signals that are HIGH/float at boot.
- **IO12 (MTDI)** — sets flash voltage; the WROOM internal flash is 3.3 V so **IO12 must read LOW at boot**. If J6/JTAG is populated, ensure the adapter/header doesn't pull it HIGH at boot. Don't add an external pull-up.
- **IO15 (MTDO)** — LOW at boot suppresses the boot ROM log; JTAG use is fine.

### Input-only pins (IO34–39)
No output drivers, no internal pull-ups. Used here for `VBAT_SENSE` (IO34, analog)
and `PWR_PG` (IO35, digital but has the external R30 pull-up) — both correct uses.

### Module variant — PSRAM caution ⚠️
Use a **no-PSRAM** module: −32UE-N4 (production), −32E-N4 (proto). **Any PSRAM
module (R-suffix, e.g. N16R8 = 8 MB PSRAM) reserves GPIO16 & GPIO17** internally
(PSRAM CS/clock) → they are unusable, which would kill `RS485_TX`/`RS485_RX`
(IO16/17). PSRAM brings nothing to this firmware (4 MB partition plan). −32E/−32UE
in the N4 flavour are pin-compatible; the PSRAM variants are the trap. (rev1's
module is no-PSRAM, which is why IO16/17 work there.)

---

## 2.2.1 Hierarchical labels (sheet pins) — place these on the sheet

The MCU is the hub, so every inter-block signal leaves this sheet as a hierarchical
label. Shape is from **this sheet's POV** and is the complement of the counterpart
sheet (output↔input; I²C + EN/BOOT + EXP are bidirectional). The 19 labels below
match the export — `SENS_GATE` is **not** here (it is internal to power↔rs485, §1/§4).
**Rails (`+3V3`, `GND`) are global power symbols, not labels.**

| Hierarchical label | Shape | GPIO | Other end (counterpart shape) |
|---|---|---|---|
| `VBAT_SENSE` | **input** | IO34 (ADC1) | block 1 power — divider (output) |
| `PWR_PG` | **input** | IO35 | block 1 power — buck PG (output) |
| `SENS_PWR_EN` | **output** | IO25 | block 4 rs485-sensor — Q60 gate + THVD1426 SHDN̅ (input); *block 1 gets `SENS_GATE` from block 4, not this net* |
| `RS485_TX` | **output** | IO16 | block 4 RS485 — THVD1426 DI (input) |
| `RS485_RX` | **input** | IO17 | block 4 RS485 — THVD1426 RO (output) |
| `PUMP_EN` | **output** | IO26 | block 5 pump — Q2 gate via R9 (input) |
| `RESERVOIR_LOW_LEVEL` | **input** | IO32 | block 6 level — Q1 drain (output) |
| `RESERVOIR_HIGH_LEVEL` | **input** | IO33 | block 6 level — Q1 drain (output) |
| `I2C_SDA` | **bidirectional** | IO21 | blocks 5/7 (+ DNP 1) — shared I²C |
| `I2C_SCL` | **bidirectional** | IO22 | blocks 5/7 (+ DNP 1) — shared I²C |
| `U0TXD` | **output** | IO1 | block 3 USB-UART — CP2102N RXD (input) |
| `U0RXD` | **input** | IO3 | block 3 USB-UART — CP2102N TXD (output) |
| `EN` | **bidirectional** | EN | block 3 auto-program (RTS→NPN); shared w/ R11/C42/SW2 |
| `BOOT` | **bidirectional** | IO0 | block 3 auto-program (DTR→NPN); shared w/ R12/SW1 |
| `EXP_SCK` | **bidirectional** | IO18 | block 8 J7 — VSPI SCK |
| `EXP_MOSI` | **bidirectional** | IO23 | block 8 J7 — VSPI MOSI |
| `EXP_MISO` | **bidirectional** | IO19 | block 8 J7 — VSPI MISO |
| `EXP_CS` | **bidirectional** | IO4 | block 8 J7 — SPI CS / GPIO |
| `EXP_IRQ` | **bidirectional** | IO27 | block 8 J7 — interrupt / GPIO |

`STATUS_LED` (IO2 → LED2) and the JTAG `IO12–IO15` (→ J6) are **local** to this sheet —
no hierarchical label.

**Why `EN`/`BOOT` are bidirectional:** the net has several drivers/loads on it
(pullup, button to GND, and the block-3 auto-program transistor). Declaring the
hierarchical label bidirectional avoids spurious ERC "input not driven" / "conflicting
outputs" flags; block 3 drives the transistor side.

---

## 2.3 Connectivity

Pin-by-pin (`01-power.md` style). The GPIO↔function map is §2.2; these tables
cover the discrete support parts so the wiring is unambiguous.

### 2.3.1 Power & decoupling
| Component / pin | Connect to | Net |
|---|---|---|
| U1 all VDD / 3V3 pins | rail | `+3V3` |
| U1 all GND pins + thermal pad | ground | `GND` |
| C41 (100 nF X7R) pin 1 | `+3V3` (tight at the module 3V3 pin) | `+3V3` |
| C41 pin 2 | ground | `GND` |
| C40 (10 µF X7R) pin 1 | `+3V3` (bulk, near the module) | `+3V3` |
| C40 pin 2 | ground | `GND` |

Note: ESP32 RF TX current peaks ~500 mA — keep the `+3V3` path low-impedance; the block-1 buck + its output caps (C24/C25) feed this rail.

### 2.3.2 EN — power-on reset
| Component / pin | Connect to | Net |
|---|---|---|
| U1 **EN** | R11 / C42 / SW2 / J6 junction | `EN` |
| R11 (10 kΩ) pin 1 | rail | `+3V3` |
| R11 pin 2 | EN node | `EN` |
| C42 (1 µF) pin 1 | EN node | `EN` |
| C42 pin 2 | ground | `GND` |
| SW2 (RESET) pin 1 | EN node | `EN` |
| SW2 pin 2 | ground | `GND` |
| `EN` *(hierarchical)* | block 3 — CP2102N auto-program (RTS via NPN) | `EN` |

Note: R11 + C42 = RC power-on-reset delay; SW2 grounds EN for manual reset; block 3 pulses EN low to enter flashing. C42 = **1 µF** (resolved — see §3.3.5 of block 3: EN must rise slowly so BOOT is still low when reset releases).

### 2.3.3 BOOT — boot select
| Component / pin | Connect to | Net |
|---|---|---|
| U1 **IO0** | R12 / C43 / SW1 junction | `BOOT` |
| R12 (10 kΩ) pin 1 | rail | `+3V3` |
| R12 pin 2 | IO0 node | `BOOT` |
| C43 (1 nF) pin 1 | IO0 node (optional noise filter) | `BOOT` |
| C43 pin 2 | ground | `GND` |
| SW1 (BOOT) pin 1 | IO0 node | `BOOT` |
| SW1 pin 2 | ground | `GND` |
| `BOOT` *(hierarchical)* | block 3 — CP2102N auto-program (DTR via NPN) | `BOOT` |

Note: IO0 HIGH at boot = run, LOW = bootloader. R12 holds it high; SW1 (or block 3) pulls it low. Strapping pin (§2.2).

### 2.3.4 Programming UART (→ block 3)
| Component / pin | Connect to | Net |
|---|---|---|
| U1 IO1 (U0TXD) | block 3 CP2102N RXD | `U0TXD` |
| U1 IO3 (U0RXD) | block 3 CP2102N TXD | `U0RXD` |

Note: both hierarchical pins; TX↔RX cross at the bridge.

### 2.3.5 Status LED
| Component / pin | Connect to | Net |
|---|---|---|
| U1 IO2 | R40 pin 1 | `STATUS_LED` |
| R40 (1 kΩ) pin 2 | LED2 anode | — |
| LED2 (green) cathode | ground | `GND` |

Note: active-HIGH (IO2 high → LED on; rev1 parity, patterns in parity-checklist §8). IO2 strapping — LED-to-GND keeps it low at boot ✓.

### 2.3.6 JTAG header J6 (DNP / optional) — generic 1×6 2.54 mm
| J6 pin | Connect to | Net |
|---|---|---|
| 1 | U1 IO14 | `JTAG_TMS` |
| 2 | U1 IO13 | `JTAG_TCK` |
| 3 | U1 IO12 | `JTAG_TDI` |
| 4 | U1 IO15 | `JTAG_TDO` |
| 5 | ground | `GND` |
| 6 | rail (Vtarget sense) | `+3V3` |

Note: KiCad generic `Connector:Conn_01x06_Pin` + footprint `PinHeader_1x06_P2.54mm_Vertical` — **no LCSC import**. DNP by default; jumper to an ESP-Prog. Pin 6 = `+3V3` (Vtarget sense — what the adapter expects). Hardware reset is covered by SW2 (EN button) + OpenOCD soft reset over JTAG, so `EN` is **not** routed to the header. **IO12 strapping** — no external pull-up on IO12; adapter must not drive it HIGH at boot (§2.2).

### 2.3.7 Antenna, rails, signal GPIOs
- **Antenna:** −32UE has on-module **U.FL** → external SMA bulkhead pigtail (mechanical BOM, IP66 enclosure). −32E (PCB antenna) = populate-time alternative, same footprint, no U.FL.
- **Rails consumed:** `+3V3`, `GND`. MCU sources no rails.
- **Signal GPIOs** (`PUMP_EN`, `RS485_TX`/`RS485_RX`, `RESERVOIR_LOW_LEVEL`/`RESERVOIR_HIGH_LEVEL`, `I2C_SDA`/`I2C_SCL`, `SENS_PWR_EN`, `VBAT_SENSE`, `PWR_PG`, `EXP_*`): each is a hierarchical pin to its module — net ↔ GPIO listed in §2.2.

---

## 2.4 Open items / decisions

1. **Operational buttons — RESOLVED 2026-06-23: dropped.** rev1's *manual* (IO5)
   and *config* (IO18) buttons are **not** carried to rev2 — network-centric
   control (web UI / RS485), interface proven reliable. IO5 stays spare (strapping),
   IO18 stays reserved for expansion. The emergency-WiFi-reset parity item moves to
   a software/network trigger (firmware, Phase 2).
   **BOOT/RESET (SW1/SW2) are kept** — note these are *not* required for routine
   flashing (the block-3 CP2102N auto-program circuit toggles EN/IO0 via DTR/RTS
   automatically). They are a manual fallback (BOOT = force bootloader when
   auto-program is flaky) + dev convenience (RESET = restart during CLI debug).
2. **JTAG header J6 — RESOLVED 2026-06-23: keep, DNP by default.** Footprint stays
   on the board as cheap insurance for the ESP-IDF bring-up; populate + an ESP-Prog
   (~$30) only if a tough bug needs hardware breakpoints. **Pin 6 = `+3V3`** (Vtarget
   sense); `EN` is *not* routed to the header — hardware reset is covered by SW2 +
   OpenOCD soft reset (§2.3.6). IO12 strapping caution applies (§2.2 / §2.3.6).
3. **EN cap value — RESOLVED 2026-06-24: 1 µF** (C42 = C15849, Samsung 1 µF 50 V
   X5R). The auto-reset circuit (block 3 §3.3.5) needs EN to rise slowly enough that
   BOOT is still low when the chip leaves reset; 100 nF is marginal. X5R (not X7R)
   because 1 µF X7R 0603 is JLC Extended-only — X5R is Basic, already on the board
   (C23/C50), and ample for a non-self-heating POR cap (same X7R-impractical → X5R
   call as C24/C25/C40).
4. **§0.4 inventory — RESOLVED 2026-06-24.** J6 (JTAG) is now listed under block 2 in
   `00-architecture.md §0.4`. (Closed; the note was stale.)
5. **Brownout detector** (parity QUIRK 4) — keep **enabled** on rev2 (proper power
   design); firmware decision, noted for Phase 1 bring-up. No hardware impact.

---

## Coverage check

Consumes from the pin contract (`00-architecture.md §0.5` mcu): every signal lands
on a GPIO here. Provides no rails. Defers: USB bridge + auto-program transistors
(block 3 — `EN`/`BOOT`/`U0TXD`/`U0RXD` are the seam); expansion footprint (block 8 —
GPIOs IO18/19/23 reserved). Parity pins honored where free: RS485 16/17, I²C 21/22,
pump 26, levels 32/33, status LED 2. New rev2 signals: `VBAT_SENSE` (IO34, ADC1),
`PWR_PG` (IO35), `SENS_PWR_EN` (IO25).

ERC/build checklist for Paul: `VBAT_SENSE` on an ADC1 pin (IO34 ✓); `PUMP_EN`
defaults LOW at boot (pump off — safety); no strapping pin forced to a wrong boot
level (IO0/2/5/12/15 per §2.2); IO6–11 left unconnected.
