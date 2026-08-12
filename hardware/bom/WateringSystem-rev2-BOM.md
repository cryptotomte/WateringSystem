# WateringSystem rev2 — Master BOM

**The procurement roll-up for the rev2 node.** Built **incrementally**, one
block at a time, from the per-block design-notes. A block's rows appear here only
once its design-note (`hardware/rev2/design-notes/0N-*.md`) has been worked — so
a missing block means *"not started yet"*, not *"forgotten"*. See the coverage
table below.

> ⚠️ **WORK IN PROGRESS — all 8 blocks drafted (2026-06-24), not yet order-ready.**
> Both open design decisions are now closed (2026-06-24): level-sensor supply =
> switched `SENS_12V` (`06` §6.2); expansion J7 = populated, generic VSPI breakout
> (`08` §8.4). Remaining before fabrication: (1) confirm the `cand`/`—` LCSC numbers
> + connector models in the Fabrication Toolkit plugin; (2) per-sheet + root-level
> ERC. Cost summary / purchasing strategy
> stay in the archived reference (`archive/WateringSystem-rev2-BOM.md`) until the
> `cand` parts are confirmed, so no half-finished total can mislead.

## Single source of truth — who owns what

| Fact | Lives in | Notes |
|---|---|---|
| refdes, value, package, MPN, role, connectivity, net names | **design-note** `0N-*.md` | the *design* truth |
| LCSC#, LCSC status, JLC type, alternatives | **this file** | the *procurement* truth |

Flow is one-way: design → procurement. Change an MPN in the design-note, this
file follows. The volatile vendor data (C-number, stock, Basic/Extended) has
exactly one home: here.

## Coverage / block status

| # | Block | Design-note | BOM status |
|---|---|---|---|
| 1 | Power | `01-power.md` | ✅ **in this file** |
| 2 | MCU (ESP32) | `02-mcu.md` | ✅ **in this file** |
| 3 | USB-UART (CP2102N) | `03-usb-uart.md` | ✅ **in this file** |
| 4 | RS485 / sensor | `04-rs485.md` | ✅ **in this file** |
| 5 | Pump driver + INA226 | `05-pump.md` | ✅ **in this file** |
| 6 | Level sensors | `06-level-sensors.md` | ✅ **in this file** |
| 7 | I²C / environment (BME280) | `07-i2c-env.md` | ✅ **in this file** |
| 8 | Expansion header | `08-expansion.md` | ✅ **in this file** |

## How to read the LCSC columns

**Workflow:** for each row, read `LCSC#` → pull symbol/footprint/3D into KiCad
via the Fabrication Toolkit plugin → confirm the part. Resolve `choice`/`—` rows
with Paul in chat, then the C-number is written back here.

- **St** (LCSC status):
  - `✓` — verified (confirmed in plugin, or carried from the archive's verified pass)
  - `cand` — candidate from an LCSC web-search hit, **unconfirmed** — confirm in plugin
  - `choice` — several valid options exist; **your pick** (see Alternatives)
  - `—` — not found / generic passive you'll pick as a Basic part in the plugin
- **JLC** — JLCPCB type: `BP` Basic (no setup cost) · `EP` Extended (~$3 setup) · `?` unconfirmed · `—` n/a
- **Alternatives** — up to 3 other LCSC listings for the same part (clones / second sources). Empty = none worth listing.

`LCSC#` is the primary suggestion; `Alternatives` are the runners-up.

## External / off-board materiel (per node)

Not on the PCB, but part of every node build. Decisions tracked here so the
procurement picture is complete.

| Item | Chosen / candidate | Status | Notes |
|---|---|---|---|
| Charge controller | **Renogy Voyager RCC10VOYP** (PWM 10 A, IP67, Li profile) | ✅ chosen 2026-08-10 | **Common-POSITIVE** → J9 pin 1 n.c. (`01-power.md` §1.5). Battery type must be set to **LI manually** at install (auto-recognition = non-lithium only). Self-consumption 0.22 W ≈ 5 Wh/day — in energy budget, margin OK |
| Battery | 12.8 V 7.2 Ah LiFePO4 w/ built-in BMS (e.g. V-TAC) | candidate | ≈ 9 days autonomy without sun (always-on profile) |
| Solar panel | Rigid 20 W mono, glass + aluminium frame | candidate | ≈ 80–100 Wh/day in season; PWM harvest loss acceptable at this margin. Avoid flexible AliExpress panels |
| Fuse | Inline blade-fuse holder + 5 A fuse, battery + lead | required | As close to the battery as practical (`01-power.md` §1.1) |
| Antenna | SMA bulkhead + U.FL pigtail (WROOM-32UE) | per node | Mounted on the IP66/68 enclosure |

---

# Block 1 — Power

Design-note: `hardware/rev2/design-notes/01-power.md` (connectivity, net names,
buck calculations, `[VERIFY]` items). Roles below are abbreviated — the note is
authoritative.

## Main group (always populated)

| Ref | Qty | Value / part | Package | MPN | LCSC# | St | JLC | Alternatives (≤3) | Role |
|---|---|---|---|---|---|---|---|---|---|
| J2 | 1 | Terminal block 2-pin 5.08 mm | THT | Weidmüller 1715010000 | — | — | — | — | Battery input — model chosen; THT, off-LCSC (source via Weidmüller/Mouser) |
| Q20 | 1 | P-MOSFET −40 V, ≤10 mΩ | TO-252 (DPAK) | AOD4185 (AOS) | C400894 | ✓ | EP | C5261064 (HXY clone), C879147 (VBsemi clone) | Reverse-polarity protection (ideal diode) — genuine AOS chosen |
| Q21 | 1 | P-MOSFET −30 V | SOT-23 | AO3401A | C15127 | ✓ | BP | — | High-side switch, sensor 12 V rail |
| D20 | 1 | TVS unidir, 15 V standoff, 600 W | SMB (DO-214AA) | SMBJ15A/TR13 (Brightking) | C78409 | ✓ | EP | C135046 (Diodes), C283618 (ST) | Input transient clamp |
| D21 | 1 | Zener 10 V, 500 mW | SOD-123 | BZT52C10 | C216719 | cand | EP | C5244686 (Diodes), C511887 (BORN) | Q20 gate clamp — unified to 10 V to share with D22 |
| D22 | 1 | Zener 10 V, 500 mW | SOD-123 | BZT52C10 | C216719 | cand | EP | C5244686 (Diodes) | **Q21 gate clamp** (review A2; Vgs ≤ AO3401A ±12 V) — **same part as D21** |
| U4 | 1 | Buck 3 A COT, 4.2–24 V in, PG+SS | SOT583 | MP2393GTL-Z | C6674609 | ✓ | EP | — | 12 V → 3.3 V (replaces NRND MP2307) |
| L20 | 1 | Power inductor 3.3 µH, ≥4 A sat, shielded | SMD 6×6 | CYA0630-3.3UH (SHOU HAN) | C5189747 | ✓ | EP | — | Buck inductor — sat verified 9.5 A DC per datasheet (2026-08-12) ✓ |
| C20 | 1 | Electrolytic 100 µF, 25 V | SMD radial 6.3×7.7 | RVT1E101M0607 (ROQANG) | C72477 | ✓ | EP | C179802 (SUNCON), C970685 (DMBJ) | Input bulk |
| C21 | 1 | Ceramic X7R 10 µF, ≥25 V | 1206 | CL31B106KBHNNNE (50V) | C89632 | ✓ | EP | — | Input HF decoupling (50 V ≥ 25 V OK) |
| C22 | 1 | Ceramic X7R 10 µF, ≥25 V | 1206 | CL31B106KBHNNNE (50V) | C89632 | ✓ | EP | — | Buck IN decoupling |
| C32 | 1 | Ceramic 0.1 µF, 50 V X7R | 0603 | CC0603KRX7R9BB104 | C14663 | ✓ | BP | — | Input HF at IN (datasheet C1A) |
| C23 | 1 | Ceramic 1 µF, 50 V X5R | 0603 | CL10A105KB8NNNC (Samsung) | C15849 | ✓ | BP | — | Bootstrap cap (BST–SW); X5R fine here |
| R29 | 1 | 20 Ω 1% | 0603 | 0603WAF200JT5E (Uniroyal) | C22950 | ✓ | BP | — | Bootstrap series resistor (SW–BST) |
| C24, C25 | 2 | Ceramic 22 µF, 25 V X5R | 1206 | CL31A226KAHNNNE (Samsung) | C12891 | ✓ | BP | C49326822 (22µF 1210 X7R 63V), C87996 (22µF 10V X7R 1206) | 3V3 output — X5R chosen (OK at 3.3 V) |
| R21 | 1 | 40.2 kΩ 1% | 0603 | RC0603FR-0740K2L (Yageo) | C137723 | ✓ | EP | — | FB divider top |
| R22 | 1 | 13 kΩ 1% | 0603 | 0603WAF1302T5E (Uniroyal) | C22797 | ✓ | BP | — | FB divider bottom (V_out 3.29 V) |
| R28 | 1 | 20 kΩ 1% | 0603 | 0603WAF2002T5E (Uniroyal) | C4184 | ✓ | BP | — | RT — series into FB (COT ripple injection) |
| C30 | 1 | 10 pF C0G, 50 V | 0603 | CL10C100JB8NNNC (Samsung) | C1634 | ✓ | BP | — | Feedforward across R21 |
| C27 | 1 | Ceramic 6.8 nF, 50 V X7R | 0603 | 0603B682K500NT (FH) | C1631 | ✓ | BP | — | Soft-start (SS, ~1.5 ms) — C-number confirmed (2026-08-12) ✓ |
| R24 | 1 | 1 MΩ 1% | 0603 | 0603WAF1004T5E (Uniroyal) | C22935 | ✓ | BP | — | EN pullup → VBAT (datasheet 604k; 1 MΩ → 11 µA, safe) |
| C31 | 1 | Ceramic 1 nF, 50 V X7R | 0603 | CL10B102KB8NNNC (Samsung) | C1588 | ✓ | BP | — | PG decoupling |
| R30 | 1 | 100 kΩ 1% | 0603 | 0603WAF1003T5E (Uniroyal) | C25803 | ✓ | BP | — | PG pullup → 3V3 |
| R31 | 1 | 1 kΩ 1% | 0603 | 0603WAF1001T5E (Uniroyal) | C21190 | ✓ | BP | — | Q21 gate series (review A2; limits D22 clamp current) |
| C28 | 1 | Ceramic 100 nF, 50 V X7R | 0603 | CC0603KRX7R9BB104 | C14663 | ✓ | BP | — | ADC filter, battery sense |
| R20 | 1 | 100 kΩ 1% | 0603 | 0603WAF1003T5E (Uniroyal) | C25803 | ✓ | BP | — | Q20 gate pulldown |
| R25 | 1 | 470 kΩ 1% | 0603 | 0603WAF4703T5E (Uniroyal) | C23178 | ✓ | BP | — | Battery sense, top |
| R26 | 1 | 100 kΩ 1% | 0603 | 0603WAF1003T5E (Uniroyal) | C25803 | ✓ | BP | — | Battery sense, bottom |

**Buck-IC change 2026-06-21:** MP2307 (NRND) → **MP2393GTL-Z** (active, COT, FB-ref
0.805 V). Removed vs MP2307: **R23** (6.8 kΩ COMP) and **C26** (3.9 nF COMP) — COT
needs no external compensation. Added: R28 (RT), R29 (BST resistor), R30 (PG
pullup), C30 (feedforward), C31 (PG cap), C32 (input HF). New signal **`PWR_PG`**
(PG → MCU GPIO). See `01-power.md` §1.2.

## Optional group — solar telemetry (on board; populate per node)

**Decision 2026-06-20: the design includes the solar section** — J8/J9, U20, R27,
C29 footprints are on every board. Whether they are *placed* at assembly is a
per-node BOM toggle, so it can be dropped later at no PCB cost.
~~**Gate before populating:** confirm a common-negative charge controller;
on a common-positive controller leave U20/R27 unpopulated.~~
**GATE CLEARED 2026-08-10 — this node POPULATES the solar group.** The chosen
Renogy Voyager RCC10VOYP is **common-positive**, but the analysis in
`01-power.md` §1.5 showed the telemetry loop works regardless: the shunt sits in
the panel **+** lead, so its common-mode potential is battery voltage — well
inside the INA226's 36 V range. The old gate assumed a common-negative
controller was *required*; it is not. The one real constraint is wiring:
**J9 must never be tied to board GND** (it is the isolated `PANEL_RTN`
pass-through, §1.5.1). Populate U20, R27, C29, J8, J9 on this node.

| Ref | Qty | Value / part | Package | MPN | LCSC# | St | JLC | Alternatives (≤3) | Role |
|---|---|---|---|---|---|---|---|---|---|
| J8, J9 | 2 | Terminal block 2-pin 5.08 mm (2× ganged = 4 pos) | THT | Weidmüller 1715010000 | — | — | — | — | Panel pass-through — **off-LCSC, already in Paul's stock** (same part as J2/J3); gang two 2-pole at 5.08 mm pitch: J8 = `PANEL_IN`/`PANEL_OUT`, J9 = `PANEL_RTN` in/out (isolated, never GND) |
| U20 | 1 | INA226, addr 0x41 | VSSOP-10 (MSOP-10) | INA226AIDGSR | C49851 | ✓ | EP | — | Panel telemetry |
| R27 | 1 | Shunt 20 mΩ 1 W 1% | 2512 | WSL2512R0200FEA (Vishay) | C553969 | ✓ | EP | — | Panel current shunt — populated (Isc 3.17 A → 63 mV, §1.5) |
| C29 | 1 | Ceramic 100 nF, 50 V X7R | 0603 | CC0603KRX7R9BB104 | C14663 | ✓ | BP | — | U20 decoupling |

## Open procurement / design items for block 1

1. **L20 = 3.3 µH — RESOLVED 2026-06-24** (review C2). The old "10 µH" was the
   MP2307-era value (~340 kHz). The **MP2393 runs 650 kHz**, so the standard
   selection L = Vout·(Vin−Vout)/(Vin·f·ΔIL) at 12 V→3.3 V, 650 kHz, ΔIL≈1 A gives
   **≈3.1–3.7 µH → 3.3 µH** (CYA0630-3.3UH, `C5189747`, ≥4 A sat). The frequency jump
   is exactly why the inductor shrank from 10 µH. The stale 10 µH text is removed.
2. **D20 polarity — resolved.** D20 = SMBJ15A/TR13 (Brightking) `C78409`,
   **unidirectional** (was bidirectional SMBJ15CA `C78809` in the archive).
   Correct for a DC rail clamp that never goes negative behind the
   reverse-protection FET.
3. **Genuine vs clone — chosen** (pulled into KiCad): Q20 = genuine AOS `C400894`,
   D20 = Brightking `C78409`. D21/D22 = BZT52C10 `C216719` (cand — confirm in
   plugin; the earlier 12 V/`C2104` pick was superseded by the 10 V unification,
   2026-06-24). The `Alternatives` column keeps the runner-ups as fallback.
4. ~~COMP/FB values (MP2307-era)~~ — **superseded 2026-06-21** by the MP2393
   redesign: R23/C26 (COMP) removed, divider = R21 40.2 k / R22 13 k (§1.2).
   Entry kept for history only — do not order these values.
5. ~~C26 / C24·C25 picks (MP2307-era)~~ — **superseded:** C26 no longer exists
   (COT, no COMP); C24/C25 = `C12891` per the table above. History only.

## Refdes note

This block uses the `01-power.md` numbering (`Q20/Q21`, `D20/D21`, `R20–R30`,
`C20–C32`, `L20`, `U4` buck, `U20` solar). The archived BOM used a different
scheme (`Q3`, `D3`, `C1/C5`, `L1`, `F1`) — ignore it; the design-note is the
source of truth and KiCad re-annotates anyway.

---

# Block 2 — MCU (ESP32-WROOM-32UE)

Design-note: `hardware/rev2/design-notes/02-mcu.md` (pin-type table, SYNC 1 GPIO
map, pin-by-pin connectivity). New parts use the 40-series; passives mostly reuse
block-1 / archive C-numbers.

| Ref | Qty | Value / part | Package | MPN | LCSC# | St | JLC | Alternatives (≤3) | Role |
|---|---|---|---|---|---|---|---|---|---|
| U1 | 1 | ESP32-WROOM-32UE-N4 (4 MB, U.FL) | Module | ESP32-WROOM-32UE-N4 | C701344 | ✓ | EP | C701341 (−32E PCB-ant) | MCU |
| C40 | 1 | Ceramic 10 µF, 25 V X5R | 0805 | CL21A106KAYNNNE (Samsung) | C15850 | ✓ | BP | — | Module bulk decoupling |
| C41 | 1 | Ceramic 100 nF, 50 V X7R | 0603 | CC0603KRX7R9BB104 | C14663 | ✓ | BP | — | Module HF decoupling (3V3 pin) |
| C42 | 1 | Ceramic 1 µF, 50 V X5R | 0603 | CL10A105KB8NNNC (Samsung) | C15849 | ✓ | BP | — | EN power-on-reset RC — 1 µF for auto-reset (block 3 §3.3.5); X5R OK (POR, X7R 1µF is Extended-only) |
| C43 | 1 | Ceramic 1 nF, 50 V X7R | 0603 | CL10B102KB8NNNC (Samsung) | C1588 | ✓ | BP | — | IO0 noise filter (optional) |
| R11 | 1 | 10 kΩ 1% | 0603 | 0603WAF1002T5E (Uniroyal) | C25804 | ✓ | BP | — | EN pullup → +3V3 |
| R12 | 1 | 10 kΩ 1% | 0603 | 0603WAF1002T5E (Uniroyal) | C25804 | ✓ | BP | — | IO0 pullup → +3V3 |
| R40 | 1 | 1 kΩ 1% | 0603 | 0603WAF1001T5E (Uniroyal) | C21190 | ✓ | BP | — | Status-LED series |
| LED2 | 1 | LED bright green | 0805 | 150080VS75000 (Wurth) | C5148523 | ✓ | EP | — | Status LED (IO2) |
| SW1 | 1 | Tactile switch | SMD 5.1×5.1 | TS-1187A-B-A-B (XKB) | C318884 | ✓ | BP | — | BOOT (IO0 → GND) |
| SW2 | 1 | Tactile switch | SMD 5.1×5.1 | TS-1187A-B-A-B (XKB) | C318884 | ✓ | BP | — | RESET (EN → GND) |
| J6 | 1 | Pin header 1×6 (DNP) | 2.54 mm THT | generic (Conn_01x06) | — | — | — | — | JTAG breakout — KiCad generic, no LCSC import; DNP (TMS/TCK/TDI/TDO/GND/+3V3 — pin 6 = +3V3 Vtarget sense, resolved 2026-06-23) |

**Notes:** C41/C42/C43 reuse block-1 ceramics; R11/R12 (10 kΩ) = C25804, R40
(1 kΩ) = C21190 (archive-verified). U1 = −32UE (U.FL ext-antenna) with −32E
(C701341, PCB antenna) as the populate-time alternative — **same footprint**.
LED2/SW1/SW2 confirmed in the plugin (✓, 2026-06/07).
J6 is DNP (debug insurance) — pick a header model in the plugin if/when populating.

---

# Block 3 — USB-UART programming bridge (CP2102N)

Design-note: `hardware/rev2/design-notes/03-usb-uart.md` (CP2102N pin-type table,
bus-powered island topology, USB-C device wiring, the cross-coupled two-transistor
auto-program circuit). New parts use the 50-series. Bus-powered bridge — its 3.3 V
(`VDD_USB`) is a separate island, **not** the board `+3V3`.

| Ref | Qty | Value / part | Package | MPN | LCSC# | St | JLC | Alternatives (≤3) | Role |
|---|---|---|---|---|---|---|---|---|---|
| U2 | 1 | CP2102N USB-UART bridge | QFN-24 (4×4 EP) | CP2102N-A02-GQFN24R | C969151 | ✓ | EP | — | USB-to-UART — A02 silicon; **on hand (Paul)**; separate VIO pin (tie pin 5→6) |
| J1 | 1 | USB-C receptacle, 16-pin, USB-2.0 | SMD | TYPE-C-31-M-12 (Korean Hroparts) | C165948 | ✓ | EP | — | Host connector — bare USB-2.0 (no internal CC resistors) |
| D50 | 1 | USB ESD array, 2-line | SOT-23-6 | USBLC6-2SC6 (ST) | C7519 | ✓ | EP | — | D+/D− ESD + VBUS clamp — verify SOT-23-6 pinout at place |
| Q50 | 1 | NPN transistor | SOT-23 | MMBT3904 | C20526 | ✓ | BP | C2146 (S8050), C2150 (SS8050) | Auto-reset: pulls `EN` low (emitter → RTS) |
| Q51 | 1 | NPN transistor | SOT-23 | MMBT3904 | C20526 | ✓ | BP | C2146 (S8050), C2150 (SS8050) | Auto-reset: pulls `BOOT` low (emitter → DTR) |
| R50 | 1 | 10 kΩ 1% | 0603 | 0603WAF1002T5E (Uniroyal) | C25804 | ✓ | BP | — | Q50 base series (from DTR) |
| R51 | 1 | 10 kΩ 1% | 0603 | 0603WAF1002T5E (Uniroyal) | C25804 | ✓ | BP | — | Q51 base series (from RTS) |
| R52 | 1 | 5.1 kΩ 1% | 0603 | 0603WAF5101T5E (Uniroyal) | C23186 | ✓ | BP | — | USB-C CC1 pulldown (Rd, sink) |
| R53 | 1 | 5.1 kΩ 1% | 0603 | 0603WAF5101T5E (Uniroyal) | C23186 | ✓ | BP | — | USB-C CC2 pulldown (Rd, sink) |
| R54 | 1 | 22 kΩ 1% | 0603 | 0603WAF2202T5E (Uniroyal) | C31850 | ✓ | BP | — | VBUS sense divider, top (E24 Basic; →3.41 V) — C# Paul-confirmed |
| R55 | 1 | 47 kΩ 1% | 0603 | 0603WAF4702T5E (Uniroyal) | C25819 | ✓ | BP | — | VBUS sense divider, bottom (E24 Basic) — C# verified 0603 |
| R56 | 1 | 470 Ω 1% | 0603 | 0603WAF4700T5E (Uniroyal) | C23179 | ✓ | BP | — | `U0TXD` series — island back-power isolation (review A3, 2026-07-02); C# Paul-confirmed |
| R57 | 1 | 470 Ω 1% | 0603 | 0603WAF4700T5E (Uniroyal) | C23179 | ✓ | BP | — | `U0RXD` series — island back-power isolation; same part as R56 |
| C50 | 1 | Ceramic 1 µF, 50 V X5R | 0603 | CL10A105KB8NNNC (Samsung) | C15849 | ✓ | BP | — | VREGIN bulk (reuse block-1 1 µF) |
| C51 | 1 | Ceramic 100 nF, 50 V X7R | 0603 | CC0603KRX7R9BB104 | C14663 | ✓ | BP | — | VREGIN HF |
| C52 | 1 | Ceramic 4.7 µF, 16 V X5R | 0603 | CL10A475KO8NNNC (Samsung) | C19666 | ✓ | BP | — | VDD (LDO output) decoupling — X5R Basic 0603 (Paul-confirmed) |
| C53 | 1 | Ceramic 100 nF, 50 V X7R | 0603 | CC0603KRX7R9BB104 | C14663 | ✓ | BP | — | VDD HF |

**Notes:** U2 = **CP2102N-A02-GQFN24R (`C969151`)** — the part Paul has on hand (QFN-24,
separate VIO pin → tie pin 5→6); if hand-placed (hotplate), its JLC class is moot.
J1/D50 are **Extended** (no Basic USB-C / ESD array at JLC) — accept the feeder fee. Q50/Q51 use **MMBT3904 (C20526,
Basic)** to keep the transistor pair off the Extended list; S8050/SS8050 are Basic
alternatives. Passives mostly reuse block-1 C-numbers; 22 k/47 k are E24 Basic
(swapped from E96 22.1k/47.5k, same 3.41 V divider); C52 + R52/R53 confirmed ✓.
**R56/R57 (470 Ω UART series — island back-power isolation, second-opinion review A3
2026-07-02): 0603WAF4700T5E `C23179` (Basic, Paul-confirmed 2026-07-02).**

**Auto-reset note:** Q50/Q51 emitters cross-couple to the *opposite* control line
(Q50 emitter → RTS, Q51 emitter → DTR), **not** to GND — see `03-usb-uart.md` §3.3.5.
The EN/BOOT pull-ups + RC live in block 2; this block only pulls them low. Reliable
auto-reset wants block-2 `C42` = **1 µF** (recommended in `03-usb-uart.md` §3.4).

---

# Block 4 — RS485 / sensor interface (THVD1426)

Design-note: `hardware/rev2/design-notes/04-rs485.md` (THVD1426 pin-type table,
resolved bias/RE̅/SHDN̅ decisions, pin-by-pin connectivity, sensor-rail gate driver).
New parts use the 60-series. **External A/B bias (old R4/R5) removed** — the
THVD1426 internal receiver fail-safe makes it unnecessary (§4.2).

| Ref | Qty | Value / part | Package | MPN | LCSC# | St | JLC | Alternatives (≤3) | Role |
|---|---|---|---|---|---|---|---|---|---|
| U3 | 1 | RS485 auto-direction, ±12 kV IEC | SOT-8 (5×3) | THVD1426DRLR (TI) | C5215922 | ✓ | EP | — | Transceiver — only variant stocked at both LCSC + Mouser; not SOIC-8 |
| C60 | 1 | Ceramic 100 nF, 50 V X7R | 0603 | CC0603KRX7R9BB104 | C14663 | ✓ | BP | — | U3 VCC decoupling |
| R3 | 1 | 120 Ω 1% | 0603 | 0603WAF1200T5E (Uniroyal) | C22787 | ✓ | BP | — | Bus termination (single-end; DNP if sensor embeds one) |
| D4 | 1 | RS485 TVS, asymmetric +12/−7 V | SOT-23 | SM712.TCT (Semtech) | C12067 | ✓ | EP | SMAJ12CA-pair | A/B surge/EFT — restored from archive verified pass (review B1) |
| J4 | 1 | Connector 4-pin (A/B/SENS_12V/GND) | THT | JST-XH4 | C144395 | — | — | — | Sensor/Modbus field connector — pick model in plugin (JST-XH or terminal) |
| Q60 | 1 | N-MOSFET 60 V | SOT-23 | 2N7002 | C8545 | ✓ | BP | — | Sensor-rail gate driver (pulls SENS_GATE low) |
| R60 | 1 | 100 kΩ 1% | 0603 | 0603WAF1003T5E (Uniroyal) | C25803 | ✓ | BP | — | Q60 gate pulldown → GND (default-off safety) |
| R61 | 1 | 100 kΩ 1% | 0603 | 0603WAF1003T5E (Uniroyal) | C25803 | ✓ | BP | — | SENS_GATE pullup → VBAT (holds Q21 off) |

**Notes:** U3 = THVD1426**DRLR** (SOT-8 5×3) — the variant available at both LCSC
(C5215922) and Mouser; **not** THVD1426DR (SOIC-8), and don't confuse with
THVD1452DR (C1850237, a different full-duplex device). **R4/R5 (old 680 Ω bias)
removed** — internal fail-safe. D4 = SM712.TCT `C12067` (✓ archive-verified,
restored review B1). Q60 = 2N7002 `C8545` (✓) — same line item as block-6 Q1.
R3 120 Ω + J4: pick Basic / connector model in the plugin.

---

# Block 5 — Pump driver + current sense (INA226)

Design-note: `hardware/rev2/design-notes/05-pump.md` (INA226 pin-type table,
low-side FET switch + flyback/TVS, high-side shunt, pin-by-pin connectivity).
Adapted from the archived two-pump design to **single pump**. New part = C70.
LCSC numbers carried from the archive's verified pass (`hardware/bom/archive/`).

| Ref | Qty | Value / part | Package | MPN | LCSC# | St | JLC | Alternatives (≤3) | Role |
|---|---|---|---|---|---|---|---|---|---|
| Q2 | 1 | N-MOSFET 40 V 120 A, Vgs(th) ≤2.5 V | DPAK (TO-252) | NCE40H12K | C148243 | ✓ | EP | DMTH4004LK3 (C260930), AOD4184A (C99124) | Low-side pump switch — low-threshold for direct 3.3 V gate (§5.3; bench-confirm) |
| D1 | 1 | Schottky 40 V 10 A | TO-277A (SMPC) | SS10P4-M3/86A (Vishay) | C968538 | ✓ | EP | — (no Basic 10 A exists; EP accepted) | Flyback across pump — 10 A headroom for FL-35; K = tab, A = pins 1+2 (§5.3-1) |
| D2 | 1 | TVS bidirectional, 16 V standoff | SMB | SMBJ16CA (Littelfuse) | C151255 | ✓ | EP | — | Q2 drain–source clamp |
| R1 | 1 | Current shunt 5 mΩ 1% ≥2 W | 2512 | HoYLR2512-2W-5mR-1% (Milliohm) | C5375417 | ✓ | EP | HoJLR2512-3W-5mR-1% (C2903482, 3 W) | High-side pump shunt — 5 mΩ → INA226 FS ≈ 16 A (FL-35: ~4 A run / 8 A max; P ≤ 0.32 W on 2 W part, §5.3) |
| R8 | 1 | 10 kΩ 1% | 0603 | 0603WAF1002T5E (Uniroyal) | C25804 | ✓ | BP | — | Q2 gate pulldown (pump OFF at boot — safety) |
| R9 | 1 | 100 Ω 1% | 0603 | 0603WAF1000T5E (Uniroyal) | C22775 | ✓ | BP | — | Q2 gate series |
| C5 | 1 | Electrolytic 470 µF 25 V | SMD | UWT1E471MNL1GS (Nichicon) | C507622 | ✓ | EP | — | Pump-start bulk (VBAT) — confirm LCSC/footprint in plugin |
| U5 | 1 | INA226, addr 0x40 | MSOP-10 | INA226AIDGSR | C49851 | ✓ | EP | — | Pump current + bus voltage (A0/A1 = GND) |
| C70 | 1 | Ceramic 100 nF, 50 V X7R | 0603 | CC0603KRX7R9BB104 | C14663 | ✓ | BP | — | U5 VS decoupling |
| J3 | 1 | Terminal block 2-pin 5.08 mm | THT | Weidmüller 1715010000 | — | — | — | — | Pump output — off-LCSC, same family as J2 |

**Notes:** U5 = INA226 @ **0x40** (A0/A1→GND) — distinct from block-1 U20 @ 0x41 and
BME280 0x76/0x77. Q2 = **NCE40H12K** (`C148243`, was NCE4060K) — switched to a
**low-threshold** part (Vgs(th) max 2.5 V) so it fully turns on from a 3.3 V GPIO; no
DPAK FET publishes a 2.5 V RDS spec → bench-confirm on the phase-1 rig (`05-pump.md`
§5.3). **Pump confirmed (2026-06-25): Floline / Sunflo FL-35, 12 V, ~4 A running,
~8 A max** → R1 resized 2 mΩ → **5 mΩ** (FS ≈ 16 A). D1 upgraded SS54 → **SS10P4**
(`C968538`, 10 A, TO-277A) for headroom; footprint SMA → TO-277A. NCE40H12K at 4–5 A:
~0.18 W (~7 °C) with copper pour. R9 100 Ω + C5 470 µF: pick Basic / electrolytic
model in the plugin.

---

# Block 6 — Reservoir level sensors (XKC-Y26 + 2N7002 invert)

Design-note: `hardware/rev2/design-notes/06-level-sensors.md` (XKC-Y26 facts, MODE
tie, 2N7002 invert topology, fail-safe, supply decision). **Two identical channels
with explicit refdes** (LOW = Q1/R80/R6/R7/JP1/J5 → IO32; HIGH = Q81/R81/R82/R83/
JP2/J10 → IO33) → qty 2 per line covers both. The XKC-Y26 sensors are **off-board**
(field-wired via J5/J10) — not PCB BOM lines.

| Ref | Qty | Value / part | Package | MPN | LCSC# | St | JLC | Alternatives (≤3) | Role |
|---|---|---|---|---|---|---|---|---|---|
| Q1 *(LOW)*, Q81 *(HIGH)* | 2 | N-MOSFET 60 V | SOT-23 | 2N7002 | C8545 | ✓ | BP | — | Level-shift/invert XKC OUT (12 V) → 3.3 V GPIO |
| R6 *(LOW)*, R82 *(HIGH)* | 2 | 100 kΩ 1% | 0603 | 0603WAF1003T5E (Uniroyal) | C25803 | ✓ | BP | — | Gate pulldown → GND (default-off, disconnected-safe) |
| R7 *(LOW)*, R83 *(HIGH)* | 2 | 10 kΩ 1% | 0603 | 0603WAF1002T5E (Uniroyal) | C25804 | ✓ | BP | — | Drain pullup → +3V3 |
| R80 *(LOW)*, R81 *(HIGH)* | 2 | 10 kΩ 1% | 0603 | 0603WAF1002T5E (Uniroyal) | C25804 | ✓ | BP | — | Gate series (OUT→gate) — field-transient hardening (review D1) |
| JP1 *(LOW)*, JP2 *(HIGH)* | 2 | Solder jumper (MODE↔GND) | — | KiCad SolderJumper | — | — | — | — | MODE polarity — **default OPEN** (floating = active-HIGH, mirrors rev1 JP4); close→GND to invert. No part (review A1) |
| J5 *(LOW)*, J10 *(HIGH)* | 2 | JST-XH 4-pin (VCC/OUT/GND/MODE) | XH-4P | B4B-XH-A(LF)(SN) | C144395 | ✓ | EP | — | XKC-Y26 field connector |

**Notes:** Q1 = 2N7002 `C8545` — **same part as block-4 Q60** (one 2N7002 line item
across the board). R6 = **gate pulldown** (corrected from the archive's "OUT pullup
to 12 V" — XKC output is push-pull, no OUT pull-up needed). XKC-Y26 **MODE = floating**
(JP1 default open) = active-HIGH OUT → active-LOW GPIO after Q1 — **proven by rev1**
(JP4 "normal_open" + firmware active-HIGH, `06` §6.4-3). Sensor **supply = `SENS_12V`**
(switched, resolved). Fails safe toward "don't pump". The XKC-Y26-V sensors themselves
are a mechanical/system BOM item, not on the PCB.

---

# Block 7 — I²C environment sensor + bus pull-ups (BME280)

Design-note: `hardware/rev2/design-notes/07-i2c-env.md` (BME280 pin map, address
0x77, the whole-bus I²C pull-ups, bus address map). **R90/R91 are the only I²C
pull-ups on the board** (§0.7) — the archive omitted them (assumed a module).

| Ref | Qty | Value / part | Package | MPN | LCSC# | St | JLC | Alternatives (≤3) | Role |
|---|---|---|---|---|---|---|---|---|---|
| U6 | 1 | BME280, temp/humidity/pressure | LGA-8 | BME280 (Bosch) | C92489 | ✓ | EP | — | Environment sensor, addr 0x77 (SDO→VDDIO, rev1 parity) |
| R90 | 1 | 4.7 kΩ 1% | 0603 | 0603WAF4701T5E (Uniroyal) | C23162 | ✓ | BP | - | I²C SDA pull-up → +3V3 (whole-bus) |
| R91 | 1 | 4.7 kΩ 1% | 0603 | 0603WAF4701T5E (Uniroyal) | C23162 | ✓ | BP | - | I²C SCL pull-up → +3V3 (whole-bus) |
| C90 | 1 | Ceramic 100 nF, 50 V X7R | 0603 | CC0603KRX7R9BB104 | C14663 | ✓ | BP | — | BME280 VDD decoupling |
| C91 | 1 | Ceramic 100 nF, 50 V X7R | 0603 | CC0603KRX7R9BB104 | C14663 | ✓ | BP | — | BME280 VDDIO decoupling |

**Notes:** U6 BME280 @ **0x77** (SDO→VDDIO, matches rev1 `src/main.cpp:45`); CSB→+3V3
selects I²C. **R90/R91 (4.7 kΩ) are the single I²C pull-up pair** for the whole bus
(BME280 0x77 + pump INA226 0x40 + solar INA226 0x41, all distinct) — confirm
C23162 in plugin (drop to 2.2 kΩ only if rise-time is marginal). C90/C91 reuse the
block-1 100 nF.

---

# Block 8 — Expansion header

Design-note: `hardware/rev2/design-notes/08-expansion.md` (J7 pinout, VSPI-default
flexible GPIO breakout). Resolves the SPI-vs-UART question (not locked in copper —
GPIO-matrix-remappable).

| Ref | Qty | Value / part | Package | MPN | LCSC# | St | JLC | Alternatives (≤3) | Role |
|---|---|---|---|---|---|---|---|---|---|
| J7 | 1 | Pin header 1×8 | 2.54 mm THT | generic (Conn_01x08) | — | — | — | — | Expansion: +3V3/GND/GND + IO18/19/23/4/27 (VSPI default) — KiCad generic, no LCSC import |

**Notes:** J7 breaks out the block-2 §2.2 reserved/spare GPIO (IO18/19/23 native
VSPI + IO4/IO27) with `+3V3` and two `GND`. No strapping pins exposed. **Populated**
(resolved 2026-06-24) — pick a header model in the plugin. Optional ESD/series-R not
placed — the add-on board handles its own protection.
