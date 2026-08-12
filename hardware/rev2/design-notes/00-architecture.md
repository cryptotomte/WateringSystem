# Block 0 — Architecture & module reuse plan (rev2 node)

**Status:** Draft for review · 2026-06-18
**Scope:** How the rev2 schematic and PCB are partitioned into reusable modules,
the contract (pins + shared rails) between modules, and the KiCad 10 features
this design relies on. This is the *integration* document — the root sheet that
ties the per-block design-notes (`01-power.md`, `04-rs485.md`, …) together.
**Audience:** whoever draws the schematic and lays out the board (currently
Paul, by hand in KiCad). See `HOWTO-kicad-hierarchical.md` for the mechanics.

---

## 0.1 Why modular

The rev2 node is **one instance of a planned family**: it is the *greenhouse
node* in a multi-zone network (`docs/feature-ideas.md`). Future boards — a
central reservoir/valve unit, additional zone nodes, bench/dev breakouts — will
reuse most of the same circuitry (power input + buck, ESP32 + USB-UART
programming, RS485 sensor interface, I²C telemetry). Designing rev2 as a set of
self-contained modules with defined interfaces means those blocks can be lifted
into the next board instead of re-drawn.

The decision **2026-06-10: one pump channel** (see rev2 BOM) is exactly this
philosophy in action — the node carries a single watering-pump module; the
reservoir-fill pump lives on a future central unit that will reuse the *same*
pump module design block.

## 0.2 KiCad 10 features this plan depends on

Upgrade target: **KiCad 10.0.x** (project will be created in 10; the existing
rev1 `hardware/WateringSystem.*` project stays on 9 unless deliberately
migrated — the file-format bump is one-way).

- **Hierarchical sheets** — each module is its own `.kicad_sch` file; the root
  sheet instantiates them as sheet symbols. A module's interface is its set of
  *hierarchical pins* (created from hierarchical labels inside the sheet).
- **Schematic Design Blocks** (since 9) — a finished module sheet can be saved
  to a design-block library and dropped into another project.
- **PCB Design Blocks** (new in 10) — a module's **placement + routing** can be
  saved to a library and recalled per instance. Decoupling caps sit tight to
  their IC pins *once*, then travel with the module. Each placed instance is an
  **independent copy** (edits to the source do not propagate back into boards
  already laid out — this is intentional).
- **Library links** (new in 10) — a schematic group carries a link to its
  source design block, and that link transfers to the PCB, keeping schematic and
  layout reuse in sync.
- **Multi-channel design** (9) — replicate one channel's layout to identical
  instances *within* a project. Not needed for this single-pump node, but the
  pump and level-sensor modules are drawn so a future multi-pump board can use
  it.

Net effect: design a module as (1) a hierarchical sheet with a clean pin
contract and (2) a grouped, well-placed footprint cluster, and it becomes
reusable as both schematic and layout across projects.

## 0.3 Global rails vs hierarchical pins — the partition rule

**Do not** thread power and ground through hierarchical pins. Distribute them as
**global power symbols** so every module just drops the rail it needs.

| Global rail (power symbol) | Meaning | Source module |
|---|---|---|
| `GND` | Common ground | all |
| `+3V3` | Logic rail from buck (KiCad stock power symbol; net = its Value) | power |
| `VBAT` | Reverse-protected 12 V board rail | power |
| `SENS_12V` | Switched 12 V to sensor/RS485 domain (default OFF) | power (FET) + rs485-sensor (control) |

Add a `PWR_FLAG` on each rail at its source (one per net) so ERC does not flag
"driven by no pin" where rails enter a module through a power symbol rather than
a pin.

**Hierarchical pins are for signals only** — buses and control lines that cross
a module boundary. Everything in §0.5 that is not a rail above.

## 0.4 Module inventory

One `.kicad_sch` per row. Refdes are the rev2 BOM assignments; the `N×10+20`
series in the per-block design-notes is for *new* parts that the BOM does not
already name (`01-power.md` §refdes) — KiCad will re-annotate, so the
design-notes are the source of truth for *connectivity*, not numbering.

| # | Sheet file | Owns (BOM refs) | Design-note |
|---|---|---|---|
| 1 | `power.kicad_sch` | J2, Q20 (AOD4185 rev-prot), Q21 (AO3401A sensor-rail FET) + gate clamp D22/R31, D20/D21, U4 (MP2393) + buck passives (L20 3.3 µH, C20–C32, R20–R31), batt-sense divider (R25/R26/C28); optional solar (DNP): U20 (INA226), R27, J8/J9, C29 | `01-power.md` |
| 2 | `mcu.kicad_sch` | U1 (ESP32-WROOM-32UE/-32E), R11 (EN pullup), R12 (BOOT pullup), R40, SW1, SW2, LED2, J6 (JTAG) | `02-mcu.md` |
| 3 | `usb-uart.kicad_sch` | U2 (CP2102N-A02-GQFN24R, QFN-24), J1 (USB-C), D50 (USBLC6 ESD), Q50/Q51 (auto-reset NPN), R50–R55, C50–C53 | `03-usb-uart.md` |
| 4 | `rs485-sensor.kicad_sch` | U3 (THVD1426), R3 (120 Ω term), D4 (SM712), J4, C60 + sensor-rail gate driver (Q60/R60/R61). *R4/R5 bias removed — internal fail-safe* | `04-rs485.md` |
| 5 | `pump.kicad_sch` | Q2 (NCE40H12K low-threshold N-FET), R8/R9, D1 (SS10P4 flyback), D2 (SMBJ16CA), R1 (5 mΩ shunt), C5 (470 µF bulk), U5 (INA226 @ 0x40) + C70, J3 | `05-pump.md` |
| 6 | `level-sensors.kicad_sch` | LOW ch: Q1, R80, R6, R7, JP1, J5 · HIGH ch: Q81, R81, R82, R83, JP2, J10 (2N7002 invert + gate series/pulldown, drain pullup, MODE jumper, XKC-Y26 connector per channel) | `06-level-sensors.md` |
| 7 | `i2c-env.kicad_sch` | U6 (BME280 @ 0x77), R90/R91 (the bus pull-ups), C90/C91 | `07-i2c-env.md` |
| 8 | `expansion.kicad_sch` | J7 (1×8 0.1″: +3V3/GND/GND + IO18/19/23/4/27) | `08-expansion.md` |

Refdes are the as-built rev2 values (resolved 2026-06-23): block 1 = `Q20`
(AOD4185 reverse protection) + `Q21` (AO3401A sensor-rail FET); buck = `U4`
(MP2393); ESP32 = `U1`. The earlier `Q3`/`MP2307` naming is superseded.

## 0.5 Pin contract (module interfaces)

Signals crossing module boundaries. **GPIO-pin-level assignments are deferred to
SYNC 1** — this table fixes the *net names and direction*, not which ESP32 pin
each maps to. Direction is from the named module's point of view.

**I²C note:** `I2C_SDA` and `I2C_SCL` are **two separate `bidirectional` nets**, not
a KiCad graphical bus. Place them as two individual bidirectional hierarchical
labels (open-drain, both directions). Pull-ups live once in block 7 (i2c-env).

### power
| Pin | Dir | Net | Other end |
|---|---|---|---|
| sensor-rail gate | in | `SENS_GATE` | rs485-sensor (driver) |
| battery sense | out | `VBAT_SENSE` | mcu (ADC1) |
| buck power-good | out | `PWR_PG` | mcu GPIO (MP2393 PG, open-drain + 3V3 pullup) |
| panel I²C *(opt, DNP)* | bidir | `I2C_SDA`, `I2C_SCL` | i2c-env |

Provides global rails `+3V3`, `VBAT`, `GND`, and `SENS_12V` (drain of the
high-side FET; enabled via `SENS_GATE`).

### mcu — the signal hub
| Pin | Dir | Net | Other end |
|---|---|---|---|
| battery sense | in | `VBAT_SENSE` | power (ADC1 only — ADC2 unusable with WiFi) |
| buck power-good | in | `PWR_PG` | power (MP2393 PG; GPIO assigned at SYNC 1) |
| sensor-rail enable | out | `SENS_PWR_EN` | rs485-sensor |
| RS485 TX | out | `RS485_TX` | rs485-sensor |
| RS485 RX | in | `RS485_RX` | rs485-sensor |
| pump enable | out | `PUMP_EN` | pump |
| reservoir level | in | `RESERVOIR_LOW_LEVEL`, `RESERVOIR_HIGH_LEVEL` | level-sensors (active LOW on rev2 — 2N7002 inverter, see CLAUDE.md FR5; matches rev1) |
| I²C bus | bidir | `I2C_SDA`, `I2C_SCL` | i2c-env, pump, power(opt) |
| programming UART | bidir | `U0TXD`, `U0RXD` | usb-uart |
| boot/reset | bidir | `EN`, `BOOT` | usb-uart (auto-boot) |
| expansion | — | `EXP_*` | expansion *(pins locked at SYNC 1)* |

Consumes `+3V3`, `GND`.

### usb-uart
| Pin | Dir | Net | Other end |
|---|---|---|---|
| programming UART | bidir | `U0TXD`, `U0RXD` | mcu |
| boot/reset | bidir | `EN`, `BOOT` | mcu |

Consumes `+3V3`, `GND`. (USB `VBUS`/5 V is local; if a 5 V supply alternative is
wanted, expose it as a rail — decide at block 3.)

### rs485-sensor
| Pin | Dir | Net | Other end |
|---|---|---|---|
| RS485 TX | in | `RS485_TX` | mcu |
| RS485 RX | out | `RS485_RX` | mcu |
| sensor-rail enable | in | `SENS_PWR_EN` | mcu |
| sensor-rail gate | out | `SENS_GATE` | power |

Consumes `+3V3`, `GND`, `VBAT` (gate pull-up), `SENS_12V` (sensor connector
power). `A`/`B` go to connector J4 (board edge, not a module boundary).

### pump
| Pin | Dir | Net | Other end |
|---|---|---|---|
| pump enable | in | `PUMP_EN` | mcu |
| I²C bus | bidir | `I2C_SDA`, `I2C_SCL` | shared (U5 INA226 @ 0x40) |

Consumes `VBAT` (pump supply), `+3V3`, `GND`. Pump output to connector J3.

### level-sensors
| Pin | Dir | Net | Other end |
|---|---|---|---|
| reservoir level | out | `RESERVOIR_LOW_LEVEL`, `RESERVOIR_HIGH_LEVEL` | mcu (active LOW on rev2; matches rev1) |

Consumes `+3V3`, `GND`, and `SENS_12V` for the XKC-Y26 supply (**resolved
2026-06-24: switched `SENS_12V`** — §0.9-1 / `06-level-sensors.md` §6.2; fails safe
toward "don't pump"). J5 (LOW) + J10 (HIGH) to the sensors.

### i2c-env
| Pin | Dir | Net | Other end |
|---|---|---|---|
| I²C bus | bidir | `I2C_SDA`, `I2C_SCL` | mcu, pump, power(opt) |

Consumes `+3V3`, `GND`. **Owns the single pair of I²C pull-ups** for the whole
bus (one location, not per-device). Holds U6 (BME280, **0x77** — SDO→VDDIO, rev1
parity).

### expansion
| Pin | Dir | Net | Other end |
|---|---|---|---|
| expansion bus | — | `EXP_SCK`/`EXP_MOSI`/`EXP_MISO`/`EXP_CS`/`EXP_IRQ` (IO18/23/19/4/27) | mcu |

Consumes `+3V3`, `GND`. J7 = 1×8 header. **Pins locked** (`08-expansion.md` §8.3):
native VSPI on IO18/19/23 + IO4/IO27 spare, all GPIO-matrix-remappable (SPI/UART/GPIO).

## 0.6 Net naming

UPPERCASE_SNAKE, matching `01-power.md`. The pin-contract net names in §0.5 are
the canonical bus/signal names; keep schematic labels 1:1 with them so review
across modules stays possible.

## 0.7 Shared-bus ownership (avoid duplicate passives)

- **I²C pull-ups** live only in `i2c-env`. power(solar), pump and i2c-env all
  *consume* the bus; none of them add their own pull-ups.
- **RS485 termination** (R3, 120 Ω) lives only in `rs485-sensor`. External A/B
  **bias is omitted** — the THVD1426 has an internal receiver fail-safe (`04-rs485.md`
  §4.2), so the old R4/R5 bias pair was removed.
- **`SENS_12V` crosses two modules by design:** the power path (high-side FET)
  is in `power`; the gate driver (2N7002 + pull-up) is in `rs485-sensor`. The
  `SENS_GATE` pin is the seam.

## 0.8 Build order

Draw and verify bottom-up so each module's rails exist before consumers need
them:

1. **power** (this round) — produces all rails; nothing depends on un-drawn work.
2. mcu + usb-uart — the programming/boot core.
3. rs485-sensor — needs `SENS_12V` from power and TX/RX from mcu.
4. pump, level-sensors, i2c-env — leaf modules.
5. expansion — last; pins frozen at SYNC 1.

Each module: draw sheet → ERC the sheet in isolation → integrate into root →
root-level ERC → save as design block once stable.

## 0.9 Open items (resolve before ERC sign-off)

1. **Level-sensor supply — RESOLVED 2026-06-24: switched `SENS_12V`** (saves ~10 mA,
   fails safe toward "don't pump"; firmware waits ≥ 500 ms after power-up). See
   `06-level-sensors.md §6.2`.
2. **5 V supply alternative from USB — RESOLVED 2026-06-23: keep local.** `USB_5V`
   powers only the CP2102N bridge island; the board runs from battery/buck. USB is
   programming/console only (`03-usb-uart.md §3.4`). (Bench-power-off-USB later =
   OR-in `USB_5V` to the buck input via an ideal diode — a separate change.)
3. **SYNC 1 GPIO assignment — COMPLETE.** All rev2 signals mapped (`02-mcu.md §2.2`,
   ADC1 for `VBAT_SENSE` ✓). Expansion pins finalized in `08-expansion.md §8.3`
   (IO18/19/23/4/27, VSPI default, matrix-remappable) — no SPI-vs-UART copper lock.
