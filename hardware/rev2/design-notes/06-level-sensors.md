# Block 6 — Reservoir level sensors (rev2 node)

**Status:** Draft for review · 2026-06-24
**Scope:** Two XKC-Y26-V non-contact level sensors (reservoir LOW + HIGH), each
with a 2N7002 level-shift/invert stage to a 3.3 V GPIO, and their connectors J5/J10.
**Out of scope:** the GPIOs themselves (block 2 — IO32/IO33); the 12 V rail
generation (block 1); `SENS_12V` gating control (block 4).

**Two identical channels** (LOW → IO32, HIGH → IO33), each with its **own refdes**
— §6.3 gives both channels pin-by-pin (2026-07-02: HIGH channel written out
explicitly; "mirror it yourself" is not build documentation).

**Design change vs rev1:** rev1 level-shifted with a TXS0108E path (active-HIGH at
the GPIO). rev2 uses a **2N7002 inverter** → **active-LOW at the GPIO** (water
present = GPIO LOW). Matches CLAUDE.md FR5 / parity (IO32 low, IO33 high).

**Refdes — channel map** (LOW keeps the reused BOM names; HIGH uses the block-6
80-series). Source of truth for connectivity, not numbering:

| Function | LOW channel (IO32) | HIGH channel (IO33) |
|---|---|---|
| 2N7002 inverter | **Q1** | **Q81** |
| Gate series 10 kΩ | **R80** | **R81** |
| Gate pulldown 100 kΩ | **R6** | **R82** |
| Drain pullup 10 kΩ | **R7** | **R83** |
| MODE solder jumper | **JP1** | **JP2** |
| Sensor connector (JST-XH) | **J5** | **J10** |

**Net naming:**

| Net | Meaning |
|---|---|
| `SENS_12V` | 12 V sensor supply (switched — see §6.2) → XKC VCC |
| `RESERVOIR_LOW_LEVEL` | LOW-sensor GPIO (IO32), active-LOW |
| `RESERVOIR_HIGH_LEVEL` | HIGH-sensor GPIO (IO33), active-LOW |

---

### Hierarchical labels (sheet pins) — place these on the sheet

Shape is from this block's POV (matches `00-architecture.md §0.5` level-sensors).

| Hierarchical label | Shape | Inside this sheet connects to | Other end |
|---|---|---|---|
| `RESERVOIR_LOW_LEVEL` | **output** | Q1 drain | mcu IO32 |
| `RESERVOIR_HIGH_LEVEL` | **output** | Q81 drain | mcu IO33 |

**Rails are NOT hierarchical labels** — `+3V3`, `GND`, and `SENS_12V` (sensor supply,
§6.2) are global power symbols (§0.3). The XKC sensor connections (`SENS_12V`/OUT/GND/
MODE) terminate at connectors J5/J10 (board edge) — local, no hierarchical label.

---

## 6.0 Parts

- **XKC-Y26-V** (4-wire: brown=VCC, yellow=OUT, blue=GND, **black=MODE**). OUT is
  **push-pull**, swings 0 ↔ VCC (≈12 V), active-HIGH when **MODE tied to VCC**.
  ~5 mA, 500 ms response, 5–24 V. Off-board (field-wired) — only J5/J10 are on the PCB.
- **Q1 (2N7002)** N-FET, SOT-23: gate ← XKC OUT, source → GND, drain → GPIO. Vgs(max)
  ±20 V → a 12 V gate drive is within rating.

---

## 6.1 Component list (per the two channels)

| Ref | Qty | Component | Value / type | Package | Function |
|---|---|---|---|---|---|
| Q1 *(LOW)*, Q81 *(HIGH)* | 2 | N-MOSFET (2N7002) | — | SOT-23 | Level-shift + invert XKC OUT (12 V) → 3.3 V GPIO |
| R80 *(LOW)*, R81 *(HIGH)* | 2 | Resistor | 10 kΩ | 0603 | **Gate series** (OUT → gate) — field-cable transient hardening (review D1) |
| R6 *(LOW)*, R82 *(HIGH)* | 2 | Resistor | 100 kΩ | 0603 | 2N7002 **gate pulldown** → GND (default-off + disconnected-safe) |
| R7 *(LOW)*, R83 *(HIGH)* | 2 | Resistor | 10 kΩ | 0603 | Drain pullup → `+3V3` (defines GPIO high when off) |
| JP1 *(LOW)*, JP2 *(HIGH)* | 2 | Solder jumper (MODE↔GND) | **default OPEN** | — | **MODE polarity** — open = floating = active-HIGH (rev1-proven); close = GND = invert. No LCSC (review A1) |
| J5 *(LOW)*, J10 *(HIGH)* | 2 | Connector 4-pin | VCC/OUT/GND/MODE | — | XKC-Y26 field connector (JST-XH 4-pin) |

*(Corrected vs archive: R6 is a **gate pulldown to GND**, not an OUT-pullup to 12 V —
the XKC output is push-pull, so no OUT pull-up is needed. R80 gate-series added per
review D1; JP1 MODE-select solder jumper added per review A1.)*
*(Optional, not placed: a low-cap TVS on OUT at J5/J10 for harsh field installs — DNP footprint.)*

---

## 6.2 Sensor supply — switched `SENS_12V` (recommended) — arch §0.9 item 1

The XKC draws ~5 mA each → **~10 mA continuous** for the pair. Two options:

- **`SENS_12V` (switched) — recommended.** Powers the level sensors only when the
  sensor domain is awake (shared with the NPK/RS485 sensor), saving ~10 mA
  (~240 mAh/day) on this solar/LiFePO4 node. Consistent with the duty-cycle design.
  Firmware: enable `SENS_PWR_EN`, **wait ≥ 500 ms** (XKC response) before trusting
  the read; keep it on for the duration of a watering run to catch mid-run depletion.
- **`VBAT` (always-on) — alternative.** Level continuously known, no settling
  latency, simplest — at ~10 mA always-on.

**Fail-safe (important — parity-checklist item 97).** With the switched supply OFF
(or a disconnected sensor cable), the XKC OUT is unpowered → R6 holds the 2N7002
gate low → Q1 OFF → R7 pulls the GPIO **HIGH** → reads **"water absent"** (active-LOW).
For this node's role (the pump *draws from* the reservoir), "absent" → **do not pump**
→ **fails safe** (no dry-running). Note this is the *inverted* fail-direction vs
rev1's fill-unit logic — safe here because rev2 draws rather than fills.

✅ **RESOLVED 2026-06-24: switched `SENS_12V`.** Saves ~10 mA, fails safe toward
"don't pump", shares the sensor-domain power window with the NPK/RS485 sensor.
Firmware: enable `SENS_PWR_EN`, wait ≥ 500 ms before trusting the level read.

---

## 6.3 Connectivity — both channels, pin by pin

### 6.3.1 LOW channel (reservoir-empty sensor → IO32)

| Component / pin | Connect to | Net |
|---|---|---|
| J5 pin 1 **VCC** (brown) | sensor supply | `SENS_12V` |
| J5 pin 4 **MODE** (black) | JP1 pin 1 (open = active-HIGH; close = invert) | (local: `MODE_LOW`) |
| J5 pin 3 **GND** (blue) | ground | `GND` |
| J5 pin 2 **OUT** (yellow) | R80 pin 1 | (local: `LVL_LOW_OUT`) |
| JP1 pin 1 | J5 MODE (pin 4) | (local: `MODE_LOW`) |
| JP1 pin 2 | ground (**default OPEN** → MODE floats → active-HIGH, mirrors rev1 JP4) | `GND` |
| R80 (10 kΩ) pin 1 | J5 OUT (pin 2) | (local: `LVL_LOW_OUT`) |
| R80 (10 kΩ) pin 2 | Q1 gate (gate node) | (gate node) |
| Q1 **gate (pin 1)** | gate node (R80 pin 2 + R6 pin 1) | (gate node) |
| R6 (100 kΩ) pin 1 | Q1 gate (gate node) | (gate node) |
| R6 (100 kΩ) pin 2 | ground (pulldown — default-off) | `GND` |
| Q1 **source (pin 2)** | ground | `GND` |
| Q1 **drain (pin 3)** | GPIO + R7 pin 1 | `RESERVOIR_LOW_LEVEL` |
| R7 (10 kΩ) pin 1 | Q1 drain (`RESERVOIR_LOW_LEVEL`) | `RESERVOIR_LOW_LEVEL` |
| R7 (10 kΩ) pin 2 | `+3V3` (pullup) | `+3V3` |

### 6.3.2 HIGH channel (reservoir-full sensor → IO33)

| Component / pin | Connect to | Net |
|---|---|---|
| J10 pin 1 **VCC** (brown) | sensor supply | `SENS_12V` |
| J10 pin 4 **MODE** (black) | JP2 pin 1 (open = active-HIGH; close = invert) | (local: `MODE_HIGH`) |
| J10 pin 3 **GND** (blue) | ground | `GND` |
| J10 pin 2 **OUT** (yellow) | R81 pin 1 | (local: `LVL_HIGH_OUT`) |
| JP2 pin 1 | J10 MODE (pin 4) | (local: `MODE_HIGH`) |
| JP2 pin 2 | ground (**default OPEN** → MODE floats → active-HIGH, mirrors JP1/rev1 JP4) | `GND` |
| R81 (10 kΩ) pin 1 | J10 OUT (pin 2) | (local: `LVL_HIGH_OUT`) |
| R81 (10 kΩ) pin 2 | Q81 gate (gate node) | (gate node) |
| Q81 **gate (pin 1)** | gate node (R81 pin 2 + R82 pin 1) | (gate node) |
| R82 (100 kΩ) pin 1 | Q81 gate (gate node) | (gate node) |
| R82 (100 kΩ) pin 2 | ground (pulldown — default-off) | `GND` |
| Q81 **source (pin 2)** | ground | `GND` |
| Q81 **drain (pin 3)** | GPIO + R83 pin 1 | `RESERVOIR_HIGH_LEVEL` |
| R83 (10 kΩ) pin 1 | Q81 drain (`RESERVOIR_HIGH_LEVEL`) | `RESERVOIR_HIGH_LEVEL` |
| R83 (10 kΩ) pin 2 | `+3V3` (pullup) | `+3V3` |

How it works (both channels): water present → XKC OUT = 12 V (MODE floating/open, per
rev1) → Q1/Q81 ON → drain pulled to GND → **GPIO LOW = water present** (active-LOW).
Water absent / sensor off → OUT 0 V or unpowered → R6/R82 holds gate low → Q1/Q81 OFF
→ R7/R83 pulls **GPIO HIGH = absent**.

Notes:
- **R80 (10 kΩ gate series) is now mandatory** (review D1) — hardens the field-cabled
  OUT line against transients into the 2N7002 gate. Driving the gate to ~12 V is within
  the 2N7002 ±20 V Vgs; R80 limits transient current.
- **MODE polarity = floating/open (active-HIGH), proven by rev1** (§6.4-3; rev1 JP4
  "normal_open" + firmware active-HIGH). JP1 default OPEN replicates it; close → GND to
  invert if ever needed.
- R7 (external 10 kΩ) is the primary pullup; the MCU's internal pullup (§2.2) may be
  left enabled (redundant, harmless) or disabled.

---

## 6.4 Open items / verify

1. **Sensor supply — RESOLVED 2026-06-24: switched `SENS_12V`** (§6.2). Firmware must
   power the sensor domain and wait ≥ 500 ms before reading level.
2. **J5/J10 connectors** — JST-XH 4-pin (B4B-XH-A, `C144395` per archive) matches the
   XKC 4-wire pinout; confirm the model in the plugin.
3. **MODE polarity — RESOLVED 2026-06-24 via rev1 precedent (review A1).** Datasheets
   conflict, but **rev1 is the empirical ground truth**: it runs the *same* XKC-Y26
   sensors with a MODE jumper (**JP4 = `Jumper_2_Open`, "normal_open"**, default open →
   MODE floating) and the firmware reads **active-HIGH** (`src/main.cpp:504`: "XKC-Y26
   sensors are active HIGH — output HIGH when water detected"). So **MODE floating =
   active-HIGH (water → OUT HIGH)** is proven, independent of supply voltage (rev1 = 5 V,
   rev2 = 12 V; MODE selects polarity, not level). rev2 replicates this: **JP1 default
   OPEN** (mirrors rev1 JP4). The jumper stays as insurance to invert without a respin
   if a future sensor batch differs. *No bench gate required — rev1 proves it.*
4. **Firmware sequencing (review D5, HIL).** Level reads are valid only ≥ 500 ms after
   `SENS_PWR_EN` is asserted; an early sample reads "absent". Add to the Phase-1 HIL
   checklist.

---

## Coverage check

Consumes from the pin contract (`00-architecture.md §0.5` level-sensors):
`RESERVOIR_LOW_LEVEL`/`RESERVOIR_HIGH_LEVEL` (to mcu, IO32/33), rails `+3V3`, `GND`,
and 12 V (`SENS_12V` recommended, §6.2). Provides no rails. XKC sensors are
field-wired via J5/J10 (board edge). Touches no already-drawn block. Open items in §6.4
— only item 1 (supply) needs your input.
