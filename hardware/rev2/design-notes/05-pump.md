# Block 5 — Pump driver + current sense (rev2 node)

**Status:** Draft for review · 2026-06-24 (adapted from the archived two-pump
design to single-pump + standard format)
**Scope:** Low-side N-MOSFET pump switch, flyback + drain TVS, pump-start bulk cap,
high-side current/voltage sense (INA226 @ 0x40), pump connector J3.
**Out of scope:** the `PUMP_EN` GPIO (block 2, IO26); I²C pull-ups (block 7);
`VBAT` generation (block 1).

**Design change vs rev1:** rev1 switched the pump with a **relay** (OJS-SH-124HMF).
rev2 goes **solid-state**: a logic-level N-FET low-side switch + INA226 current
monitoring. Parity that carries over: `PUMP_EN` on **IO26** (active-HIGH), and
**pump OFF at boot / watchdog / OTA** (safety invariant — held by R8).

**Refdes:** reused BOM names **U5** (INA226), **Q2** (N-FET), **D1** (flyback),
**D2** (TVS), **R1** (shunt), **R8/R9** (gate), **C5** (bulk), **J3** (connector).
New part uses the 70-series (**C70**). Source of truth for connectivity, not numbering.

**Net naming:**

| Net | Meaning |
|---|---|
| `VBAT` | Board 12 V rail (from block 1) — pump supply |
| `PUMP_P` | Pump + terminal, downstream of the shunt (≈ `VBAT`) |
| `PUMP_N` | Pump − terminal = Q2 drain (the switched node) |
| `PUMP_EN` | 3V3 GPIO (IO26) — HIGH = pump ON |
| `I2C_SDA`, `I2C_SCL` | shared I²C (U5 @ 0x40) |

---

### Hierarchical labels (sheet pins) — place these on the sheet

Shape is from this block's POV (matches `00-architecture.md §0.5` pump).

| Hierarchical label | Shape | Inside this sheet connects to | Other end |
|---|---|---|---|
| `PUMP_EN` | **input** | Q2 gate (via R9) | mcu IO26 |
| `I2C_SDA` | **bidirectional** | U5 SDA | shared I²C (mcu, pull-ups block 7) |
| `I2C_SCL` | **bidirectional** | U5 SCL | shared I²C |

**Rails are NOT hierarchical labels** — `VBAT`, `+3V3`, `GND` are global power symbols
(§0.3). `PUMP_P`/`PUMP_N` are **local** (they terminate at connector J3, a board edge),
so no hierarchical label.

---

## 5.0 INA226 symbol — pin electrical types (curate first)

Same chip as block-1 U20 (solar). MSOP-10 (VSSOP-10).

| Pin | Name | Electrical type | Note |
|---|---|---|---|
| `VS` | `power_input` | logic supply → `+3V3` |
| `GND` | `power_input` | |
| `IN+` | `input` | shunt high side (`VBAT`) |
| `IN−` | `input` | shunt low side (`PUMP_P`) |
| `VBUS` | `input` | bus-voltage sense (tie to `PUMP_P` ≈ `VBAT`) |
| `SDA` / `SCL` | `bidirectional` | I²C |
| `A0` / `A1` | `input` | address straps (both `GND` → 0x40) |
| `ALERT` | `open_collector` *(or NC)* | not used → leave NC |

NCE40H12K (Q2) — DPAK: pin 1 = Gate, pin 2 / tab = Drain, pin 3 = Source.

---

## 5.1 Component list

| Ref | Qty | Component | Value / type | Package | Function |
|---|---|---|---|---|---|
| Q2 | 1 | N-MOSFET, low-threshold | 40 V, 120 A, **Vgs(th) ≤ 2.5 V** | DPAK (TO-252) | Low-side pump switch (NCE40H12K — full turn-on at 3.3 V gate; §5.3) |
| D1 | 1 | Schottky | 40 V, 10 A | TO-277A (SMPC) | Flyback across the pump (SS10P4-M3/86A — C968538; §5.3) |
| D2 | 1 | TVS, bidirectional | 16 V standoff | SMB | Drain–source clamp on Q2 |
| R1 | 1 | Current shunt | 5 mΩ, 1%, ≥2 W | 2512 | High-side pump shunt (FS ≈ 16 A; sized for ~5 A pump, §5.3) |
| R8 | 1 | Resistor | 10 kΩ | 0603 | Q2 gate **pulldown → GND** (pump OFF at boot — safety) |
| R9 | 1 | Resistor | 100 Ω | 0603 | Q2 gate series (limits switching transient) |
| C5 | 1 | Electrolytic | 470 µF, 25 V | SMD | Bulk for pump-start inrush (`VBAT`→GND) |
| U5 | 1 | INA226 | 16-bit I²C current/voltage monitor | MSOP-10 | Pump current + bus voltage, addr 0x40 |
| C70 | 1 | Ceramic X7R | 100 nF, 50 V | 0603 | U5 VS decoupling |
| J3 | 1 | Terminal block 2-pin | 5.08 mm | THT | Pump output (+ / −) |

---

## 5.2 Connectivity

Pin-by-pin (`01-power.md` style).

### 5.2.1 Pump switch + protection
| Component / pin | Connect to | Net |
|---|---|---|
| R1 (5 mΩ) pin 1 | rail | `VBAT` |
| R1 pin 2 | pump + supply (J3 pin 1) | `PUMP_P` |
| J3 pin 1 | pump + | `PUMP_P` |
| J3 pin 2 | pump − | `PUMP_N` |
| Q2 **drain** (tab) | pump − / switched node | `PUMP_N` |
| Q2 **source** | ground | `GND` |
| Q2 **gate** | gate node (R9 pin 2 + R8 pin 1) | (gate node) |
| R9 (100 Ω) pin 1 | `PUMP_EN` (from IO26) | `PUMP_EN` |
| R9 (100 Ω) pin 2 | Q2 gate (gate node) | (gate node) |
| R8 (10 kΩ) pin 1 | Q2 gate (gate node) | (gate node) |
| R8 (10 kΩ) pin 2 | ground (default-off pulldown) | `GND` |
| D1 (SS10P4) cathode = **tab (K)** | pump + supply (R1 pin 2 side) | `PUMP_P` |
| D1 (SS10P4) anode = **pin 1 + pin 2** (both, tied) | Q2 drain / switched node | `PUMP_N` |
| D2 (SMBJ16CA) pin 1 | Q2 drain (across D–S) | `PUMP_N` |
| D2 (SMBJ16CA) pin 2 | ground | `GND` |
| C5 (470 µF) **+ (pin 1)** | `VBAT` rail at the power stage (R1 pin 1 side) | `VBAT` |
| C5 (470 µF) **− (pin 2)** | ground, on the Q2-source GND node | `GND` |

How it works: `PUMP_EN` HIGH → Q2 ON → `PUMP_N` pulled to GND → pump runs (current
`VBAT`→R1→pump→Q2→GND). `PUMP_EN` LOW / undriven → R8 holds the gate low → Q2 OFF →
pump stopped. **D1** freewheels the motor's inductive current when Q2 switches off
(cathode at the + rail, anode at the drain). **D2** clamps the drain spike below
Q2's 40 V rating. **C5** absorbs the start-inrush so `VBAT` doesn't sag.

**Layout hint:** place C5 physically at the R1 / J3 / Q2 power-stage cluster, bridging
`VBAT`–`GND`, so the switched-current loop `C5(+) → VBAT → R1 → pump → Q2 → GND → C5(−)`
stays small (low loop inductance). Tie C5(−) and Q2 source to the same GND copper.

⚠️ **Safety invariant:** R8 + the IO26 boot-default (LOW) guarantee the pump is OFF
at boot, after watchdog reset, and across OTA restarts (CLAUDE.md). Do not move the
gate pulldown to another block.

### 5.2.2 Current / voltage sense (INA226 @ 0x40)
| Component / pin | Connect to | Net |
|---|---|---|
| U5 **IN+** | R1 pin 1 (high side) | `VBAT` |
| U5 **IN−** | R1 pin 2 (load side) | `PUMP_P` |
| U5 **VBUS** | R1 pin 2 (bus-voltage sense) | `PUMP_P` |
| U5 **VS** | logic supply | `+3V3` |
| C70 (100 nF) pin 1 | U5 VS (tight at the pin) | `+3V3` |
| C70 (100 nF) pin 2 | ground | `GND` |
| U5 **GND** | ground | `GND` |
| U5 **SDA** / **SCL** | shared I²C (pull-ups in block 7) | `I2C_SDA` / `I2C_SCL` |
| U5 **A0** | ground → addr **0x40** | `GND` |
| U5 **A1** | ground | `GND` |
| U5 **ALERT** | not used | NC |

Note: high-side shunt → INA226 measures pump current (across R1) and bus voltage
(`VBUS` ≈ `VBAT`, common-mode ~12–14.6 V, well within INA226's 0–36 V). **R1 = 5 mΩ
→ INA226 full-scale ±81.92 mV / 5 mΩ ≈ ±16.4 A**; a ~5 A pump uses ~31 % of range
(good resolution, ~0.5 mA LSB) with headroom for start-inrush. Address 0x40
(A0/A1 = GND) is distinct from block-1 U20 (0x41) and BME280 (0x76/0x77).

---

## 5.3 Open items / verify

1. **Pump confirmed (2026-06-25): Floline / Sunflo FL-35** (Floline renamed Sunflo),
   12 V freshwater boat pump, ~4 A running, **~8 A max**, ~11–13 l/min. Sizing:
   - **R1 = 5 mΩ** (was 2 mΩ) → FS ≈ 16 A, good resolution at 4–5 A. ✓ resolved.
   - **D1 = SS10P4 (10 A, 40 V, TO-277A — C968538).** On/off control means the
     flyback only conducts the brief turn-off transient, so 5 A (SS54) would also
     suffice; 10 A is headroom for a beefier pump and plain robustness. ✓ resolved.
     Footprint changed SMA → **TO-277A (SMPC)**; per datasheet **cathode = tab (K)**
     → `PUMP_P`, **anode = pins 1 + 2** (tied) → `PUMP_N`. EP-only (no Basic exists).
   - **Q2 (NCE40H12K — see item 2), C5 470 µF kept** — ample at 4–8 A.
   ⚠️ **Revisit Q2 thermal if a larger pump is chosen** (D1 now has 10 A headroom).
2. **Gate FET — RESOLVED 2026-06-24: NCE40H12K (was NCE4060K).** Review found NCE4060K
   is uncharacterized at Vgs = 3.3 V (RDS only specced at 10 V; Vgs(th) max 2.5 V →
   marginal at 3.3 V). **No JLC-stocked DPAK part publishes RDS at 2.5 V at all** — so
   the selection criterion became *lowest Vgs(th) + RDS@4.5 V + bench-confirm*.
   NCE40H12K has **Vgs(th) max 2.5 V** (guarantees full turn-on at 3.3 V), 7 mΩ@4.5 V,
   40 V/120 A → at 5 A: P ≈ 0.18 W, ΔTj ≈ 7 °C (DPAK + ≥2 cm² copper pour). Same maker
   (Wuxi NCE) + DPAK as NCE4060K → drop-in footprint.
   ⚠️ **HIL (phase 1):** bench-confirm the gate fully closes at a 3.3 V drive under
   pump load (no DPAK part guarantees a 2.5 V RDS number). Keep the copper pour +
   ~6 thermal vias on the drain pad.
3. **R1 LCSC** — 5 mΩ 2512: HoYLR2512-2W-5mR-1% (`C5375417`, ~2 W) is the candidate;
   3 W variant HoJLR2512-3W-5mR-1% (`C2903482`) for extra margin. Confirm in plugin.

---

## Coverage check

Consumes from the pin contract (`00-architecture.md §0.5` pump): `PUMP_EN` (from
mcu, IO26), `I2C_SDA`/`I2C_SCL` (shared, pull-ups block 7), rails `VBAT`, `+3V3`,
`GND`. Pump output to J3 (board edge). Provides no rails. U5 adds one I²C device
(0x40) — no pull-ups here (block 7 owns them). Touches no already-drawn block.
Open `[VERIFY]` items in §5.3 — only item 1 (pump current) needs your input.
