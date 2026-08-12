# Block 8 — Expansion header (rev2 node)

**Status:** Draft for review · 2026-06-24
**Scope:** One 8-pin expansion header (J7) breaking out the SYNC-1 reserved GPIO +
rails, for future add-on boards in the node family.
**Out of scope:** the GPIOs' primary assignment (block 2 §2.2 — these are the
*reserved/spare* set); whatever the add-on board does.

**Refdes:** **J7** (generic KiCad header, no LCSC import — like J6). Source of truth
for connectivity, not numbering.

---

## 8.0 Hierarchical labels (sheet pins) — place these on the sheet

The five expansion GPIO arrive from the MCU and break out to J7. Shape =
**bidirectional** (they are general GPIO, matrix-remappable to SPI/UART/GPIO).

| Hierarchical label | Shape | Inside this sheet connects to | Other end |
|---|---|---|---|
| `EXP_SCK` | **bidirectional** | J7 pin 3 | mcu IO18 |
| `EXP_MOSI` | **bidirectional** | J7 pin 4 | mcu IO23 |
| `EXP_MISO` | **bidirectional** | J7 pin 5 | mcu IO19 |
| `EXP_CS` | **bidirectional** | J7 pin 6 | mcu IO4 |
| `EXP_IRQ` | **bidirectional** | J7 pin 7 | mcu IO27 |

**Rails are NOT hierarchical labels** — J7 pin 1 (`+3V3`) and pins 2/8 (`GND`) are
placed as global power symbols (§0.3), not labels.

---

## 8.1 Decision — flexible GPIO breakout, default-labelled VSPI (resolves §0.9 item 3)

The SPI-vs-UART question is **not locked in copper.** The ESP32 GPIO matrix can route
SPI, UART, or plain GPIO to these pins, so J7 simply exposes the reserved GPIO set
and firmware decides:

- **IO18 / IO19 / IO23** are the **native VSPI** pins (SCK / MISO / MOSI) → best as an
  SPI bus (IOMUX direct, full clock). Default labels reflect that.
- **IO4, IO27** are general spares → CS, interrupt, UART, or GPIO.
- Need UART instead? Remap (e.g. IO23 = TX, IO19 = RX) in firmware — no board change.

No strapping pins are exposed (avoids boot-mode hazards on an unpopulated/wired
header). Two grounds give a clean SPI return.

---

## 8.2 Component list

| Ref | Qty | Component | Value / type | Package | Function |
|---|---|---|---|---|---|
| J7 | 1 | Pin header 1×8 | 2.54 mm | THT | Expansion breakout (generic KiCad header) |

*(Optional, not placed by default: series resistors / ESD array on the GPIO if the
add-on is externally cabled — the add-on board normally handles its own protection.)*

---

## 8.3 Connectivity — J7 pinout (recommended)

| J7 pin | Net | ESP32 | Default role |
|---|---|---|---|
| 1 | `+3V3` | — | 3.3 V rail (limited current — local add-on logic) |
| 2 | `GND` | — | ground |
| 3 | `EXP_SCK` | **IO18** | VSPI SCK |
| 4 | `EXP_MOSI` | **IO23** | VSPI MOSI (or UART TX) |
| 5 | `EXP_MISO` | **IO19** | VSPI MISO (or UART RX) |
| 6 | `EXP_CS` | **IO4** | SPI CS / GPIO |
| 7 | `EXP_IRQ` | **IO27** | interrupt / GPIO |
| 8 | `GND` | — | ground |

These 5 GPIO are exactly the block-2 §2.2 reserved/spare set (IO18/19/23 + IO4/IO27);
nothing else on the board uses them. All are hierarchical pins to the MCU
(`EXP_*`). The two GND pins flank the bus for signal-integrity return.

---

## 8.4 Open items / verify

1. **Pinout / intended use — accepted 2026-06-24: generic VSPI + 2-GPIO breakout**
   (no specific add-on yet). Revisit the pin roles if a concrete add-on is chosen
   (e.g. swap a GND for `VBAT` to power a 12 V add-on, or dedicate IO4/IO27).
2. **Populate vs DNP — RESOLVED 2026-06-24: populate** J7 (it is the expansion point;
   no strapping pins exposed, so populating is boot-safe).
3. **3V3 current budget** — pin 1 draws from the buck; an add-on must stay within the
   block-1 buck headroom (don't run a hungry add-on off it — use `VBAT` + a local
   regulator instead, if exposed).

---

## Coverage check

Consumes from the pin contract (`00-architecture.md §0.5` expansion): `EXP_*` (to
mcu — pins now concrete: IO18/19/23/4/27), rails `+3V3`, `GND`. Provides no rails.
Touches no already-drawn block. **This is the last block** — with it the §0.5 pin
contract is fully realized; see the cross-block close-out.
