# Block 7 — I²C environment sensor + bus pull-ups (rev2 node)

**Status:** Draft for review · 2026-06-24
**Scope:** BME280 (temp/humidity/pressure) on I²C, and the **single pair of I²C
pull-ups for the whole bus** (this is the one place they live, §0.7).
**Out of scope:** the I²C GPIOs (block 2 — IO21/IO22); the other bus devices
(pump INA226 block 5 @ 0x40, solar INA226 block 1 @ 0x41 — they *consume* the bus,
add no pull-ups).

**Refdes:** reused BOM name **U6** (BME280). New parts use the 90-series
(R90/R91 pull-ups, C90/C91 decoupling). Source of truth for connectivity, not
numbering.

**Net naming:** `I2C_SDA`, `I2C_SCL` (two separate bidirectional nets — not a KiCad
bus, §0.5), rails `+3V3`, `GND`.

---

### Hierarchical labels (sheet pins) — place these on the sheet

Shape is from this block's POV (matches `00-architecture.md §0.5` i2c-env).

| Hierarchical label | Shape | Inside this sheet connects to | Other end |
|---|---|---|---|
| `I2C_SDA` | **bidirectional** | U6 SDI (pin 3) + R90 pull-up | mcu IO21 (shared bus) |
| `I2C_SCL` | **bidirectional** | U6 SCK (pin 4) + R91 pull-up | mcu IO22 (shared bus) |

**Rails are NOT hierarchical labels** — `+3V3`, `GND` are global power symbols (§0.3).
Only the two I²C nets cross the boundary; everything else (BME280 straps, pull-ups) is
local to this sheet.

---

## 7.0 BME280 symbol — pin functions (LGA-8) + electrical types

| Pin | Name | Connect | Electrical type | Note |
|---|---|---|---|---|
| 1 | GND | `GND` | `power_input` | |
| 2 | CSB | `+3V3` | `input` | **tie HIGH → selects I²C** (not SPI) |
| 3 | SDI | `I2C_SDA` | `bidirectional` | I²C data |
| 4 | SCK | `I2C_SCL` | `input` | I²C clock |
| 5 | SDO | `+3V3` | `input` | **address select: → VDDIO = 0x77** (rev1 parity) |
| 6 | VDDIO | `+3V3` | `power_input` | I/O supply |
| 7 | GND | `GND` | `power_input` | |
| 8 | VDD | `+3V3` | `power_input` | core supply |

---

## 7.1 Component list

| Ref | Qty | Component | Value / type | Package | Function |
|---|---|---|---|---|---|
| U6 | 1 | BME280 | temp / humidity / pressure, I²C | LGA-8 (2.5×2.5) | Environment sensor, addr 0x77 |
| R90 | 1 | Resistor | 4.7 kΩ | 0603 | **I²C SDA pull-up → +3V3** (whole-bus) |
| R91 | 1 | Resistor | 4.7 kΩ | 0603 | **I²C SCL pull-up → +3V3** (whole-bus) |
| C90 | 1 | Ceramic X7R | 100 nF, 50 V | 0603 | BME280 VDD decoupling |
| C91 | 1 | Ceramic X7R | 100 nF, 50 V | 0603 | BME280 VDDIO decoupling |

*(⚠️ The archive BOM had **no** I²C pull-ups — it assumed a sensor module with
onboard pulls. With a bare LGA-8 BME280 we add R90/R91 here; they are the only I²C
pull-ups on the board.)*

---

## 7.2 Connectivity

### 7.2.1 BME280
| Component / pin | Connect to | Net |
|---|---|---|
| U6 VDD (8) | logic supply | `+3V3` |
| C90 (100 nF) pin 1 | U6 VDD (pin 8, tight) | `+3V3` |
| C90 (100 nF) pin 2 | ground | `GND` |
| U6 VDDIO (6) | I/O supply | `+3V3` |
| C91 (100 nF) pin 1 | U6 VDDIO (pin 6, tight) | `+3V3` |
| C91 (100 nF) pin 2 | ground | `GND` |
| U6 GND (1, 7) | ground | `GND` |
| U6 CSB (2) | `+3V3` (select I²C) | `+3V3` |
| U6 SDO (5) | `+3V3` → address **0x77** | `+3V3` |
| U6 SDI (3) | I²C data | `I2C_SDA` |
| U6 SCK (4) | I²C clock | `I2C_SCL` |

### 7.2.2 I²C bus pull-ups (whole-bus — only here)
| Component / pin | Connect to | Net |
|---|---|---|
| R90 (4.7 kΩ) pin 1 | `I2C_SDA` | `I2C_SDA` |
| R90 (4.7 kΩ) pin 2 | `+3V3` (pullup) | `+3V3` |
| R91 (4.7 kΩ) pin 1 | `I2C_SCL` | `I2C_SCL` |
| R91 (4.7 kΩ) pin 2 | `+3V3` (pullup) | `+3V3` |

Note: 4.7 kΩ suits 3.3 V I²C at 100/400 kHz with the populated device count (BME280
+ pump INA226; solar INA226 is DNP). If rise-time is marginal on a longer/heavier
bus, drop to 2.2 kΩ — but **only here**, never add a second pair elsewhere.

---

## 7.3 I²C bus address map (verify no clash)

| Device | Block | Addr | Set by |
|---|---|---|---|
| BME280 (U6) | 7 | **0x77** | SDO → VDDIO (rev1 parity) |
| INA226 pump (U5) | 5 | **0x40** | A0/A1 → GND |
| INA226 solar (U20, DNP) | 1 | **0x41** | A0 → VS, A1 → GND |

All distinct ✓. ESP32 master on IO21 (SDA) / IO22 (SCL).

---

## 7.4 Open items / verify

1. **BME280 address 0x77** matches rev1 firmware (`src/main.cpp:45`). Keep SDO → VDDIO.
2. **Pull-up value 4.7 kΩ** — confirm rise time once the bus length/device count is
   final at layout; 2.2 kΩ is the fallback.
3. **U6 LCSC** — C92489 (Bosch BME280, Extended); confirm in plugin.

---

## Coverage check

Consumes from the pin contract (`00-architecture.md §0.5` i2c-env): `I2C_SDA`,
`I2C_SCL` (shared with mcu/pump/power-opt), rails `+3V3`, `GND`. **Owns the single
I²C pull-up pair** (R90/R91) for the whole bus — closes the §0.7 shared-bus
ownership rule. Provides no rails. Touches no already-drawn block. With this block
the I²C contract is complete: 3 addresses (0x77/0x40/0x41), pull-ups in one place.
