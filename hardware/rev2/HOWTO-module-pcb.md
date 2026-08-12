# HOWTO — build rev2 as separate module PCBs (stage A)

How to turn the existing hierarchical schematic into **one small PCB per block**,
hand-assembled on a hot plate and interconnected with 2.54 mm headers. This is
the stage-A prototype path decided in `design-notes/09-prototype-modules.md`.

**Relation to the other docs — read this first:**

| Doc | Covers |
|---|---|
| `HOWTO-kicad-hierarchical.md` | How the hierarchical schematic was drawn; §9 = per-module layout **on one integrated board** (that is stage B) |
| `design-notes/09-prototype-modules.md` | *Why* modules first, bring-up order |
| **this file** | *How* to produce the module boards from the same schematic |

Core rule: **the schematic is drawn once.** Module projects reuse the existing
`*.kicad_sch` block files by reference — they are never copied, never forked.
Only layout is done twice (module boards now, integrated board later).

---

## 1. Preflight — blockers to clear before starting

1. **Four symbols still have no footprint.** `Update PCB from Schematic` (F8)
   refuses to run until every symbol has one. Names verified against the
   KiCad 10 libraries on disk 2026-08-12:

   | Symbol | Sheet | Footprint |
   |---|---|---|
   | `J6` (JTAG 1×6, DNP) | mcu | `Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical` |
   | `J7` (expansion 1×8) | expansion | `Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical` |
   | `JP1`, `JP2` (MODE) | level-sensors | `Jumper:SolderJumper-2_P1.3mm_Open_TrianglePad1.0x1.5mm` |

   Solder-jumper choice: **Open** matches the design default (floating MODE =
   active-HIGH, `06` §6.3); the **TrianglePad** variant is the easiest of the
   three open variants to bridge with an iron after assembly.

   Assign these in the block sheets (they belong to the design, not to the
   prototype) so stage B inherits them.
2. **Off-LCSC parts** — the LCSC order will NOT cover these; source separately:
   Weidmüller 1715010000 ×4 (J2 battery, J3 pump, J8/J9 panel), and the generic
   pin headers. See §9.
3. Close KiCad before any scripted edit; stale `~*.lck` files are gitignored but
   a second KiCad instance on the same sheet file will fight you.

## 2. Mental model

A module project is a **thin wrapper**:

```
mod-power.kicad_sch  (root, lives in the module project)
   ├── sheet symbol → ../../power.kicad_sch      ← THE shared block sheet
   └── J100/J101 headers, wired to the sheet pins + rail power symbols
```

The block sheet is untouched. Its hierarchical labels (the §0.5 pin contract)
become sheet pins on the wrapper root, and there you wire them out to headers.
Rails (`VBAT`, `+3V3`, `GND`, `SENS_12V`) travel by name as always, so on the
wrapper you place a power symbol on the header pin that carries each rail.

**Why this works:** KiCad stores symbol *instance* data (reference, unit) per
project inside the sheet file — one `(project "…")` entry each. The integrated
project and every module project coexist in the same file without collision.

**Why not Design Blocks** (`HOWTO-kicad-hierarchical.md` §8): a design block is
dropped in as an **independent copy**. Great for a future *different* board,
wrong here — a fix found during bring-up would have to be applied twice and the
two would drift. Reference, don't copy.

## 3. Directory layout

```
hardware/rev2/
  power.kicad_sch  mcu.kicad_sch  …          ← shared block sheets (unchanged)
  modules/
    mod-power/     mod-power.kicad_pro/.kicad_sch/.kicad_pcb
    mod-mcu/       …
```

Keep the depth exactly two levels down — the sheet reference
`../../power.kicad_sch` is stored relative to the project.

## 4. What to build

Eight blocks → **six boards**. Two groupings, both deliberate:

| Module board | Blocks | Why grouped |
|---|---|---|
| `mod-power` | 1 | — |
| `mod-mcu` | 2 + 3 + 8 | The auto-reset circuit spans mcu/usb-uart (`03` §3.3.5); splitting it puts the EN/BOOT timing across a jumper. Expansion (8) is just J7 — no board of its own. |
| `mod-rs485` | 4 | — |
| `mod-pump` | 5 | — |
| `mod-level` | 6 | — |
| `mod-i2c-env` | 7 | Owns the bus pull-ups — keep it on the bench for every I²C test |

## 5. Per-module recipe (worked example: `mod-power`)

1. **New project** → `hardware/rev2/modules/mod-power/mod-power.kicad_pro`.
2. **Reference the block sheet:** in the root schematic, `Place → Add
   Hierarchical Sheet` (`S`), draw the box, and set **File name** =
   `../../power.kicad_sch`. KiCad warns the file already exists and asks whether
   to use it — **yes, use the existing file.** Sheet name: `power`.
3. **Import Sheet Pins:** right-click the box → *Import Sheet Pins*. For power
   you get `SENS_GATE`, `VBAT_SENSE`, `PWR_PG`, `I2C_SDA`, `I2C_SCL`.
4. **Place the headers** (see §6 for the pinout convention) and wire each sheet
   pin to its header pin.
5. **Rails on the wrapper:** place a power symbol (`P`) on each rail pin of the
   rail header — `VBAT`, `+3V3`, `GND`, `SENS_12V` for power.
   - A module that **consumes** a rail through a header needs **one `PWR_FLAG`**
     on that header pin, or ERC reports "power input not driven". `mod-power`
     *produces* its rails and already carries the PWR_FLAGs inside block 1 — do
     not add a second set here.
6. **Annotate — carefully.** `Tools → Annotate` with **"Keep existing
   annotation"**. The block parts must keep their master-BOM refdes (U4, L20,
   C20…) or the BOM, the assembly and every design note stop matching. Headers
   are new parts: give them the **J100+ series**, which exists only on prototype
   boards.
7. **ERC** at root. Expect: unconnected sheet pins you chose not to break out
   (mark with a no-connect flag `Q` on the wrapper, never inside the block
   sheet), and the PWR_FLAG item from step 5.
8. **PCB:** F8 → all footprints arrive clustered by sheet. Draw an `Edge.Cuts`
   outline (see §7), add 4× M3 mounting holes, place and route per §8.
9. **DRC clean**, then Fabrication Toolkit export (§9).

Repeat per module. After the first one the loop takes minutes — the schematic
work is already done.

## 6. Header pinout convention

Two headers per module, so jumper bundles look the same everywhere.

**J100 — rail header, 1×6, fixed pinout on every module** (omit pins a module
does not use, keep the positions):

| Pin | Net |
|---|---|
| 1 | `VBAT` |
| 2 | `GND` |
| 3 | `+3V3` |
| 4 | `GND` |
| 5 | `SENS_12V` |
| 6 | `GND` |

**J101 — signal header**, the module's §0.5 pins in contract order, `GND` at
both ends (return + keying):

| Module | J101 pinout (pin 1 → n) |
|---|---|
| `mod-power` | GND · `SENS_GATE` · `VBAT_SENSE` · `PWR_PG` · `I2C_SDA` · `I2C_SCL` · GND |
| `mod-mcu` | GND · `VBAT_SENSE` · `PWR_PG` · `SENS_PWR_EN` · `RS485_TX` · `RS485_RX` · `PUMP_EN` · `RESERVOIR_LOW_LEVEL` · `RESERVOIR_HIGH_LEVEL` · `I2C_SDA` · `I2C_SCL` · GND |
| `mod-rs485` | GND · `RS485_TX` · `RS485_RX` · `SENS_PWR_EN` · `SENS_GATE` · GND |
| `mod-pump` | GND · `PUMP_EN` · `I2C_SDA` · `I2C_SCL` · GND |
| `mod-level` | GND · `RESERVOIR_LOW_LEVEL` · `RESERVOIR_HIGH_LEVEL` · GND |
| `mod-i2c-env` | GND · `I2C_SDA` · `I2C_SCL` · GND |

`mod-mcu` keeps `U0TXD`/`U0RXD`/`EN`/`BOOT` internal (blocks 2+3 are on the same
board) and exposes J7 expansion + J6 JTAG as their own headers, as designed.

**Silkscreen every header pin with its net name.** This is the whole
self-documenting-bundle idea from `09-prototype-modules.md`.

## 7. Current — what must NOT go through a 2.54 mm header

2.54 mm headers are ~3 A per pin at best, and the design has three paths above
that:

| Path | Current | Route it as |
|---|---|---|
| Pump `VBAT` / `PUMP_P` / `PUMP_N` | ~4 A run, **~8 A max** | `mod-pump` gets its own screw terminal for `VBAT`+`GND`, wired directly to the power module's screw terminal — **never** via J100 |
| `VBAT` daisy-chain to other modules | < 0.5 A | J100 pin 1 is fine |
| `PANEL_RTN` / `PANEL_IN`/`OUT` | ~3 A | Screw terminals only (J8/J9 as designed); never on a header |

Rule of thumb for the module boards: **anything that can melt gets a terminal
block; anything that carries information gets a header.**

## 8. Layout rules per module (from the design notes)

Carry these across to stage B — they are the reason the notes exist:

- **`mod-power`:** minimal `C22`/`C32`–`U4`–`L20`–`C24` loop; keep `SW_NODE`
  away from the FB network `R21/R22/R28/C30` (`01` §1.2). `PANEL_RTN` ≥ 1 mm
  trace, isolated — the GND pour must not swallow it (`01` §1.5.1).
- **`mod-pump`:** tight switch loop `C5(−)`–`Q2` source on the same GND copper
  (`05` §5.2.1); `D1` freewheel right at the connector; Kelvin sense across
  `R1`; wide copper on `PUMP_P`/`PUMP_N`.
- **`mod-mcu`:** ESP32 antenna keep-out (WROOM-32UE is external-antenna, but
  respect the pad area); `C42` (1 µF) close to `EN`; USB island GND handling per
  `03` §3.4 — the island is separate on purpose, do not merge the pours.
- **`mod-rs485`:** `D4` (SM712) at the connector, before anything else;
  120 Ω `R3` at the far end of the pair.
- **`mod-level`:** `JP1`/`JP2` reachable with an iron after assembly.
- **All:** decoupling caps hard against their IC pin; test point (or an
  unpopulated 1-pin pad) on every rail — you will want a probe point.

## 9. Fabrication + parts ordering

**Boards.** Six designs at JLCPCB = six order lines + six shipping-weight items.
Cheaper and simpler: **panelize all six into one ≤100×100 mm board** with
mouse-bite tabs, ordered as a single design (5 pcs ≈ one cheap order → five
complete module sets). Trade-off: respinning one module means re-ordering the
whole panel — at this price that is acceptable, and a respin usually comes with
lessons for its neighbours anyway.

**Stencil.** Order a framework-less stencil for the panel, **0.1 mm (4 mil)** —
suits the finest pitch on the boards (CP2102N QFN-24 at 0.5 mm, MP2393 SOT583).
Ask for the stencil to match the panel, not the individual boards.

**Parts.** From `hardware/bom/WateringSystem-rev2-BOM-LCSC.md` — order every
C-number, **2× quantity on passives** (hand-assembly attrition), plus:

- 2.54 mm pin headers (male) and matching female sockets/jumper leads for J100/J101
- Solder paste (Sn63Pb37 or SAC305 — leaded is more forgiving on a hot plate),
  flux, braid, tweezers, 0.1 mm stencil (above)

**Already in stock — do not re-order:** Weidmüller 1715010000 ×4 (J2 battery,
J3 pump, J8/J9 panel). They are off-LCSC and were chosen because Paul already
had them.

**Solar telemetry group is POPULATED** (decided 2026-06-20, gate cleared
2026-08-10 — `01` §1.5): `U20` C49851, `R27` C553969, `C29` C14663 belong in
the LCSC order like any other part. The old "DNP by default" wording in the
design notes and BOM was stale and has been corrected.

## 10. Bring-up order and acceptance per module

Per `09-prototype-modules.md`, each step is powered by the previous one:

| # | Module | Accept when |
|---|---|---|
| 1 | `mod-power` | `+3V3` = 3.29 V ±2 %, `PWR_PG` HIGH, `SENS_12V` switches on `SENS_GATE`, `VBAT_SENSE` scales 14.6 V → 2.56 V, no part above hand-warm at load |
| 2 | `mod-mcu` | Auto-program works over USB-C (the `C42`=1 µF check, `03` §3.3.5); ESP-IDF hello-world flashes; `PWR_PG` reads HIGH on IO35 |
| 3 | `mod-rs485` | Modbus read of the NPK sensor; TX echo present as predicted (FW-4); IO17 pull-up masks the floating-RX case (FW-2) |
| 4 | `mod-pump` | FL-35 runs; `PUMP_EN` LOW at boot/reset/OTA (safety invariant); INA226 @ 0x40 current within ~10 % of a clamp meter |
| 5 | `mod-level` | XKC-Y26 reads active-LOW through the 2N7002 (FW-5); ≥ 500 ms settling honoured (FW-3) |
| 6 | `mod-i2c-env` | BME280 @ 0x77 plausible T/RH/P; all three I²C devices coexist on one bus |

These map 1:1 onto the deferred HIL checklists in `docs/checkpoints/hil-*.md`
and onto PR-14 bring-up.

## 11. When bring-up finds a problem

**Schematic-first, always.** Fix the design note → fix the shared block sheet →
both the module project and the integrated project pick it up (the module
project only needs `F8` again). Never patch a module board's layout to work
around a schematic error, and never edit a block sheet from inside a module
project to add prototype-only parts — those belong on the wrapper root.

Log every change in the block's design note with a date and the bring-up
observation that caused it, the same way the review findings are logged.
