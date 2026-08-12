# Block 1 — Power (rev2 node)

**Status:** Draft for review · 2026-06-11
**Scope:** Battery input, protection, 3.3V buck, battery voltage sense, switched
12V sensor rail (power path only), optional solar-panel telemetry (INA226 #3).
**Out of scope:** Pump driver + pump shunt/INA226 (block 5), sensor-rail enable
GPIO assignment (SYNC 1), control side of sensor rail (block 4/RS485).

**Refdes convention:** new parts in block N use series N×10+20 — block 1 owns
R20–R31, C20–C29, D20–D22, Q20–Q21, U20, J8/J9. Existing BOM refdes (U4 = MP2393,
J2 = 12V input terminal) are reused as-is. Adjust freely in KiCad if it
collides; this file is the source of truth for *connectivity*, not numbering.

**Net naming:** UPPERCASE_SNAKE. Rename to taste, but keep 1:1 with this list
so review stays possible.

| Net | Meaning |
|---|---|
| `VBAT_IN` | Battery + at connector, unprotected |
| `VBAT` | Battery + after reverse protection — the board's 12V rail |
| `+3V3` | Logic rail from buck |
| `GND` | Common ground |
| `VBAT_SENSE` | Divided battery voltage to ADC |
| `SENS_12V` | Switched 12V to NPK sensor / RS485 domain |
| `SENS_PWR_EN` | Gate-driver input (GPIO, assigned at SYNC 1, ADC not required) |
| `RP_GATE` | Reverse-protection FET gate (local) |
| `PWR_PG` | Buck power-good (open-drain) → MCU GPIO (SYNC 1); pullup to 3V3 |
| `SW_NODE`, `BST_NODE`, `FB_NODE`, `SS_NODE` | Buck internals (local; MP2393 is COT — no COMP node) |
| `PANEL_IN`, `PANEL_OUT` | Solar + pass-through for telemetry shunt (optional) |
| `PANEL_RTN` | Solar − pass-through, isolated — **never tied to `GND`** (see §1.5) |

---

## 1.0a Hierarchical labels (sheet pins) — place these on the sheet

Shape is from this block's POV (matches `00-architecture.md §0.5` power).

| Hierarchical label | Shape | Inside this sheet connects to | Other end |
|---|---|---|---|
| `VBAT_SENSE` | **output** | batt-sense divider tap (R25/R26) | mcu IO34 |
| `PWR_PG` | **output** | U4 PG + R30 pull-up | mcu IO35 |
| `SENS_GATE` | **input** | Q21 gate (via R31) | rs485-sensor (Q60 driver) |
| `I2C_SDA` | **bidirectional** | U20 SDA *(solar, DNP)* | shared I²C |
| `I2C_SCL` | **bidirectional** | U20 SCL *(solar, DNP)* | shared I²C |

**Rails are global power symbols, not labels — and block 1 is their SOURCE.** Place a
**`PWR_FLAG` on each** of `+3V3`, `VBAT`, `SENS_12V`, `GND` here (one per rail, §0.3) so
ERC sees them driven. `VBAT_IN`, `RP_GATE`, `SW_NODE`/`BST_NODE`/`FB_NODE`/`SS_NODE`,
`Q21_GATE`, `PANEL_IN`/`PANEL_OUT` are **local** to this sheet — no hierarchical label.

---

## 1.0 Component list (pick & place this first, then wire per 1.1–1.5)

**Main group (always populated):**

| Ref | Qty | Component | Value / type | Package | MPN (suggestion) | Function |
|---|---|---|---|---|---|---|
| J2 | 1 | Terminal block 2-pin | 5.08 mm | THT | Weidmüller (per BOM, ”välj modell”) | Battery input *(already in BOM)* |
| Q20 | 1 | P-MOSFET | −40 V, ≤10 mΩ | DPAK/TO-252 | AOD4185 | Reverse-polarity protection (ideal diode) |
| Q21 | 1 | P-MOSFET | −30 V | SOT-23 | AO3401A | High-side switch, sensor 12V rail |
| D20 | 1 | TVS diode, unidir. | 15 V standoff | SMB | SMBJ15A | Input transient clamp |
| D21 | 1 | Zener diode | 10 V | SOD-123 | BZT52C10 | Q20 gate–source clamp (−10 V fully enhances AOD4185; ±20 V part) |
| D22 | 1 | Zener diode | 10 V | SOD-123 | BZT52C10 | **Q21 gate–source clamp** (§1.4 — Vgs ≤ AO3401A ±12 V) |

*(D21 unified to 10 V to share one zener part with D22 — one BOM line / feeder, qty 2.)*
| U4 | 1 | Buck converter (COT) | 3 A, 4.2–24 V in, PG+SS | SOT583 | MP2393GTL-Z | 12 V → 3.3 V |
| L20 | 1 | Power inductor | 3.3 µH, ≥4 A sat, shielded | 6×6 mm | SHOU HAN CYA0630-3.3UH | Buck inductor |
| C20 | 1 | Electrolytic cap | 100 µF, ≥25 V | SMD radial | Panasonic FK series | Input bulk |
| C21 | 1 | Ceramic cap X7R | 10 µF, **50 V** | 1206 | — | Input HF decoupling (50 V: TVS clamps ~24 V + DC-bias derating) |
| C22 | 1 | Ceramic cap X7R | 10 µF, **50 V** | 1206 | — | Buck IN pin decoupling (50 V) |
| C32 | 1 | Ceramic cap X7R | 0.1 µF, 50 V | 0603 | — | Input HF at IN (datasheet C1A) |
| C23 | 1 | Ceramic cap X7R | 1 µF, 50 V | 0603 | — | Bootstrap cap (BST–SW) |
| R29 | 1 | Resistor | 20 Ω | 0603 | — | Bootstrap series resistor (SW–BST) |
| C24, C25 | 2 | Ceramic cap X5R | 22 µF, ≥16 V (25 V part chosen) | 1206 | — | 3V3 output (X5R OK at 3.3 V; X7R only in 1210) |
| R21 | 1 | Resistor 1% | 40.2 kΩ | 0603 | — | FB divider top |
| R22 | 1 | Resistor 1% | 13 kΩ | 0603 | — | FB divider bottom |
| R28 | 1 | Resistor 1% | 20 kΩ | 0603 | — | RT — series into FB (COT ripple) |
| C30 | 1 | Ceramic cap C0G | 10 pF, 50 V | 0603 | — | Feedforward across R21 |
| C27 | 1 | Ceramic cap X7R | 6.8 nF, 50 V | 0603 | — | Soft-start (SS, ~1.5 ms) |
| R24 | 1 | Resistor 1% | 1 MΩ | 0603 | — | EN pullup → VBAT |
| C31 | 1 | Ceramic cap X7R | 1 nF, 50 V | 0603 | — | PG decoupling |
| R30 | 1 | Resistor 1% | 100 kΩ | 0603 | — | PG pullup → 3V3 |
| R31 | 1 | Resistor | 1 kΩ | 0603 | — | Q21 gate series (limits D22 clamp current) |
| C28 | 1 | Ceramic cap | 100 nF | 0603 | — | ADC filter, battery sense |
| R20 | 1 | Resistor | 100 kΩ | 0603 | — | Q20 gate pulldown |
| R25 | 1 | Resistor 1% | 470 kΩ | 0603 | — | Battery sense, top |
| R26 | 1 | Resistor 1% | 100 kΩ | 0603 | — | Battery sense, bottom |

**Optional group — solar telemetry (DNP by default, populate per node):**

| Ref | Qty | Component | Value / type | Package | MPN (suggestion) | Function |
|---|---|---|---|---|---|---|
| J8, J9 | 2 | Terminal block 2-pin (ganged = 4 pos) | 5.08 mm | THT | Weidmüller, same series as J2 | Panel pass-through (DNP; J8=PANEL_IN/OUT, J9=PANEL_RTN isolated pass-through) |
| U20 | 1 | Current/power monitor | I2C, addr 0x41 | VSSOP-10 | INA226AIDGSR | Panel telemetry |
| R27 | 1 | Shunt resistor 1% | 20 mΩ, 1 W | 2512 | Vishay WSL2512R0200FEA | Panel current shunt |
| C29 | 1 | Ceramic cap | 100 nF | 0603 | — | U20 supply decoupling |

Not on PCB (mechanical/external BOM): inline blade-fuse holder + **10 A** fuse in
battery + lead (resized 2026-07-02 for the FL-35 pump — see §1.1).

**Node energy system — selected/ordered 2026-08-12:**

| Item | Choice | Status |
|---|---|---|
| Battery | Renogy Core 12V 100Ah LiFePO4 (RBT12100LFP-TM-BT) — 150 A BMS **with low-temp charge cutoff**, BT 5.3, M8 terminals | ordered |
| Charge controller | Renogy Voyager RCC10VOYP (PWM 10 A, common-positive — see §1.5) | ordered |
| Solar panel | Nordmax 50 W N-type mono (NM50MNB) — Voc 22.5 V, Isc 3.17 A (see §1.5) | selected |
| Inline fuse | 10 A blade + waterproof holder (Jula), battery + lead ≥ 1.5 mm² | buy at install |

Install reminders: LI mode set MANUALLY on the Voyager (§1.5 verify item 4);
fuse as close to the battery + terminal as practical; battery indoors over
winter (storage 0–45 °C), stored half-charged.

---

## 1.1 Input, fuse, protection

External (mechanical BOM, not on PCB): inline blade-fuse holder **10 A** in the
battery + lead, as close to the battery as practical. Battery + lead ≥ **1.5 mm²**
(16 AWG) to match the 10 A fuse.

**Fuse resized 5 A → 10 A (2026-07-02, second-opinion review A2):** the pump was
confirmed as Floline/Sunflo FL-35 (~4 A running, **~8 A max**, `05-pump.md` §5.3-1).
At 8 A a 5 A blade fuse runs at 160 % and opens in tens of seconds → nuisance-blow
mid-run. 10 A still protects the wiring and still clears on reverse hookup (the
LiFePO4 delivers ≫ 10 A into the forward-conducting D20).

⚠️ **SAFETY-REQUIRED (review D4, 2026-06-24).** The board has **no on-board
overcurrent protection** (the archive's F1 polyfuse was intentionally dropped — wrong
device at 5 A). With a solid-state pump switch (Q2) replacing rev1's relay, a
fail-closed FET relies entirely on this external fuse. The 10 A inline fuse is
**not optional** — document it as required in the install guide, like the mandatory
RS485 GND return (`04` §4.4).

| Component / pin | Connect to | Net |
|---|---|---|
| J2 pin 1 | battery + (via inline fuse) | `VBAT_IN` |
| J2 pin 2 | battery − | `GND` |
| D20 (TVS SMBJ15A, unidirectional) cathode | J2 pin 1 | `VBAT_IN` |
| D20 anode | ground | `GND` |
| Q20 (P-MOSFET DPAK, e.g. AOD4185) **drain** | J2 pin 1 | `VBAT_IN` |
| Q20 **source** | board 12V rail | `VBAT` |
| Q20 **gate** | R20 pin 1 | `RP_GATE` |
| R20 (100 kΩ) pin 2 | ground | `GND` |
| D21 (zener 10 V, BZT52C10) cathode | Q20 source | `VBAT` |
| D21 anode | Q20 gate | `RP_GATE` |
| C20 (100 µF electrolytic ≥25 V) + | rail | `VBAT` |
| C20 − | ground | `GND` |
| C21 (10 µF X7R 50 V) pin 1 | rail | `VBAT` |
| C21 pin 2 | ground | `GND` |

Notes: Q20 in "ideal diode" orientation — body diode conducts on first
connect, FET then enhances (V_GS ≈ −V_BAT). **D21 (10 V) clamps V_GS to −10 V** —
fully enhances AOD4185 (Rds flat beyond ~−8 V) and stays well within its ±20 V Vgs;
draws ~46 µA standing via R20 when VBAT > 10 V. (10 V chosen to share one zener part
with D22/Q21, whose AO3401A is only ±12 V.) TVS sits *before* Q20 so it also clamps
reverse hookup while the fuse clears. SMBJ15A standoff 15 V > 14.6 V max charge voltage.

## 1.2 Buck 12V→3.3V (U4, MP2393GTL-Z)

**Switched MP2307 (NRND) → MP2393GTL-Z 2026-06-21** — active part, COT control,
FB-ref 0.805 V, SOT583. Pinout per datasheet (R1.0): **1=PG, 2=IN, 3=SW, 4=GND,
5=BST, 6=EN, 7=SS, 8=FB**. Internal synchronous MOSFETs; COT = **no external COMP**
(the MP2307 R23/C26 are removed). Values from datasheet Figure 7 (Vin=19V,
Vout=3.3V/3A).

| Component / pin | Connect to | Net |
|---|---|---|
| U4 pin 2 (IN) | rail | `VBAT` |
| C22 (10 µF X7R) pin 1 | `VBAT` (tight at U4 pin 2) | `VBAT` |
| C22 pin 2 | ground | `GND` |
| C32 (0.1 µF) pin 1 | `VBAT` (tight at U4 pin 2) | `VBAT` |
| C32 pin 2 | ground | `GND` |
| U4 pin 4 (GND) | ground (exposed pad → pin 4) | `GND` |
| U4 pin 3 (SW) | L20 pin 1 + C23 pin 2 | `SW_NODE` |
| L20 (3.3 µH, ≥4 A sat) pin 1 | U4 pin 3 (SW) | `SW_NODE` |
| L20 pin 2 | output rail | `+3V3` |
| U4 pin 5 (BST) | R29 pin 1 | `BST_NODE` |
| R29 (20 Ω) pin 1 | U4 pin 5 (BST) | `BST_NODE` |
| R29 pin 2 | C23 pin 1 | (local: BST mid node) |
| C23 (1 µF) pin 1 | R29 pin 2 | (local: BST mid node) |
| C23 pin 2 | `SW_NODE` (floating driver supply) | `SW_NODE` |
| C24 (22 µF X5R) pin 1 | output rail | `+3V3` |
| C24 pin 2 | ground | `GND` |
| C25 (22 µF X5R) pin 1 | output rail | `+3V3` |
| C25 pin 2 | ground | `GND` |
| R21 (40.2 kΩ 1%) pin 1 | output rail | `+3V3` |
| R21 pin 2 | divider tap (R22 pin 1 + R28 pin 1 + C30 pin 2) | (divider tap) |
| R22 (13 kΩ 1%) pin 1 | divider tap | (divider tap) |
| R22 pin 2 | ground | `GND` |
| C30 (10 pF C0G) pin 1 | `+3V3` (across R21, feedforward) | `+3V3` |
| C30 pin 2 | divider tap | (divider tap) |
| R28 (20 kΩ) pin 1 | divider tap | (divider tap) |
| R28 pin 2 | U4 pin 8 (FB) | `FB_NODE` |
| U4 pin 8 (FB) | R28 pin 2 | `FB_NODE` |
| R24 (1 MΩ) pin 1 | rail | `VBAT` |
| R24 pin 2 | U4 pin 6 (EN) | (local: EN node) |
| U4 pin 6 (EN) | R24 pin 2 | (local: EN node) |
| C27 (6.8 nF) pin 1 | U4 pin 7 (SS) | `SS_NODE` |
| C27 pin 2 | ground | `GND` |
| U4 pin 1 (PG) | R30 pin 1 + C31 pin 1 | `PWR_PG` |
| R30 (100 kΩ) pin 1 | `PWR_PG` (U4 pin 1) | `PWR_PG` |
| R30 pin 2 | `+3V3` (pullup) | `+3V3` |
| C31 (1 nF) pin 1 | `PWR_PG` | `PWR_PG` |
| C31 pin 2 | ground | `GND` |
| `PWR_PG` *(hierarchical)* | → MCU IO35 | `PWR_PG` |

Divider check: V_OUT = V_REF 0.805 V × (1 + R1/R2) = 0.805 × (1 + 40.2/13) =
**3.29 V**. ✓ (datasheet Table 1/2 for 3.3 V). R28 (RT) + C30 set the COT ripple
seen at FB; they do **not** affect DC (FB draws no DC current, so no drop across RT).

EN pullup: datasheet shows 604 kΩ; **1 MΩ chosen** → current into EN's 2.8 V clamp
= (14.6 − 2.8)/(1 M + 35 k) ≈ 11 µA, well under the 30 µA limit, and 1 MΩ is a
common part. Soft-start: C_SS = T_SS·I_SS/(2·V_REF); **6.8 nF ≈ 1.5 ms** (datasheet
typ; increase for slower inrush). PG is open-drain → R30 pullup to 3V3 + C31, then
to an MCU GPIO. Layout: keep C22/C32–U4–L20–C24 loop minimal; keep SW away from the
FB/feedback network (R21/R22/R28/C30).

## 1.3 Battery voltage sense

| Component / pin | Connect to | Net |
|---|---|---|
| R25 (470 kΩ 1%) pin 1 | rail | `VBAT` |
| R25 pin 2 | R26 pin 1 | `VBAT_SENSE` |
| R26 (100 kΩ 1%) pin 2 | ground | `GND` |
| C28 (100 nF) pin 1 | divider tap (ADC filter) | `VBAT_SENSE` |
| C28 pin 2 | ground | `GND` |
| `VBAT_SENSE` | ESP32 **ADC1** channel — pin assigned at SYNC 1. Must be ADC1 (ADC2 unusable with WiFi active) | — |

14.6 V max → 2.56 V at ADC (11 dB attenuation range). Standing drain
25 µA ≈ 0.6 mAh/day — negligible. Firmware: telemetry + soft UVLO.

## 1.4 Switched sensor rail (power path)

Control transistor + GPIO resistors belong to block 4 (RS485/sensor); this
block only places the high-side P-FET so the `VBAT`→`SENS_12V` copper is
planned with the power section.

| Component / pin | Connect to | Net |
|---|---|---|
| Q21 (P-MOSFET SOT-23, e.g. AO3401A) source | rail | `VBAT` |
| Q21 drain | sensor domain supply | `SENS_12V` |
| Q21 **gate** | R31 pin 2 + D22 anode | `Q21_GATE` |
| R31 (1 kΩ) **pin 1** | `SENS_GATE` (= block 4 Q60 drain + R61 pullup) | `SENS_GATE` |
| R31 **pin 2** | Q21 gate | `Q21_GATE` |
| D22 (zener 10 V) **cathode** | `VBAT` (= Q21 source) | `VBAT` |
| D22 **anode** | Q21 gate | `Q21_GATE` |

Load ≈ 40–50 mA (NPK sensor 0.5 W @ 12 V) + RS485 transceiver — AO3401A has
wide margin. Default state OFF (R61 pullup holds gate at `VBAT`; block 4 Q60
pulls low to enable). **Gate clamp (review A2, 2026-06-24):** at turn-on Vgs ≈
−VBAT would reach −14.6 V (LiFePO4 charge), exceeding AO3401A's **±12 V** Vgs(max).
**D22 (10 V zener, G–S) clamps Vgs to −10 V** (still full enhancement); **R31 (1 kΩ
series)** limits the clamp current (≈ (VBAT−10)/R31 ≈ 4.6 mA). Mirrors D21 on Q20.

## 1.5 Optional: solar panel telemetry (DNP by default)

Pass-through current loop on the panel + conductor. Populate per node only if
panel telemetry wanted. **Controller verified against datasheet 2026-08-10:
Renogy Voyager RCC10VOYP is COMMON-POSITIVE** ("Grounding Type: Positive" —
PV+ internally tied to BAT+, PWM switch in the negative leg). Consequences:
the shunt in the panel + lead still carries the full (PWM-chopped) charge
current, and its common-mode potential sits at battery voltage — well within
INA226 range, so the telemetry loop **works**. INA226 averaging smooths the
PWM chop; VBUS will read ≈ battery voltage (expected for PWM — panel is pulled
to battery potential). **J9 must NEVER be tied to `GND`** — wiring board GND to
PV− would short the controller's low-side switching element. Board GND already
references battery − via J2; that is all the INA226 needs.

**J9 repurposed 2026-08-12: isolated pass-through for panel −.** No controller
grounding type *needs* a board GND tie (common-negative bonds PV− to battery −
internally; the telemetry loop lives entirely in the + lead). J9's value is
mechanical: with pins 1–2 joined on their own net, all four field wires (panel
+/−, controller PV+/PV−) land on the ganged J8+J9 block. Isolated is safe for
BOTH grounding types, so no solder-jumper option to GND — a jumper whose only
capability is a destructive mistake is negative insurance.

| Component / pin | Connect to | Net |
|---|---|---|
| J8 (2-pin 5.08 mm) pin 1 | from panel + | `PANEL_IN` |
| J8 pin 2 | to controller PV+ | `PANEL_OUT` |
| J9 (2-pin 5.08 mm, ganged with J8) pin 1 | from panel − | `PANEL_RTN` |
| J9 pin 2 | to controller PV− | `PANEL_RTN` |
| R27 (shunt 20 mΩ 1 W 1%) pin 1 | `PANEL_IN` | — |
| R27 pin 2 | `PANEL_OUT` | — |
| U20 (INA226) IN+ | R27 pin 1 | `PANEL_IN` |
| U20 IN− | R27 pin 2 | `PANEL_OUT` |
| U20 VBUS | R27 pin 2 | `PANEL_OUT` |
| U20 VS | logic supply | `+3V3` |
| U20 GND | ground | `GND` |
| U20 SDA / SCL | shared I2C bus (BME280 block 7 + pump INA226 block 5; pull-ups in block 7) | `I2C_SDA` / `I2C_SCL` |
| U20 A0 | `+3V3` → address **0x41** | — |
| U20 A1 | `GND` | — |
| C29 (100 nF) pin 1 | U20 VS (tight at the pin) | `+3V3` |
| C29 pin 2 | ground | `GND` |

### 1.5.1 KiCad runbook — rewire J9 as isolated pass-through (Paul, by hand)

**DONE 2026-08-12** — executed with a variation: labels placed directly on the
pin ends (J9 nudged 1.27 mm left), no stub wires. Net verified = J9 pins 1–2
only; ERC clean.

Current state on the power sheet (after the 2026-08-12 GND fix): J9 pin 1 has a
no-connect cross directly on the pin; J9 pin 2 has a short wire stub ending in a
no-connect cross.

1. **Delete both no-connect crosses** (pin 1's on the pin, pin 2's at the end of
   its stub). Keep pin 2's wire stub.
2. **Pin 1:** draw a short wire stub from the pin (a label needs a wire end or
   pin end to attach to), then place a **local net label** `PANEL_RTN` on the
   stub (`L` key — plain label, NOT hierarchical: the net stays inside this
   sheet, exactly like `PANEL_IN`/`PANEL_OUT`).
3. **Pin 2:** place the same local label `PANEL_RTN` on the existing stub.
4. No PWR_FLAG (not a power rail — two passive connector pins), no hierarchical
   pin, no GND anywhere on this net.
5. **ERC** — expect clean. If it warns about an unconnected label, the label is
   not sitting on the wire end; nudge it onto the stub.
6. Eyeball check: highlight net `PANEL_RTN` — it must light up ONLY J9 pin 1
   and J9 pin 2. If anything else lights up, stop and re-check.

Layout note (for PCB stage): `PANEL_RTN` carries full panel current (~3 A max)
— route ≥ 1 mm trace, and make sure it is NOT swallowed by the GND pour (it is
its own isolated net, the zone tool must not connect it).

**Panel selected 2026-08-12: Nordmax 50 W N-type mono (NM50MNB)** — Voc 22.5 V,
Vmp 17.8 V, Imp 2.81 A, Isc 3.17 A. Shunt signal: 3.17 A × 20 mΩ = 63 mV
(INA226 FS 81.92 mV) ✓. Voc 22.5 V < 25 V Voyager max-PV ✓, < 36 V INA226
common-mode max ✓.

---

## Coverage check (anti-corner-cutting contract)

Consumes from BOM changelog 2026-06-11: items **2** (battery feed, fuse,
reverse protection, TVS, 14.6 V tolerance), **3** (switched rail — power path
half), **4** (battery sense divider), **7** (panel INA226, DNP).
Touches BOM rows: U4 (MP2393 + its passives now explicit), J2.
Defers: item 1 (32UE → block 2/MCU), item 5 (J7 → block 7), control side of
item 3 (→ block 4), pump shunt + INA226 0x40 (→ block 5), I2C bus (→ block 6).

Open `[VERIFY]` items before ERC sign-off:
1. ~~MP2307 pinout / COMP values~~ — **superseded 2026-06-21**: MP2307 was NRND,
   switched to **MP2393GTL-Z** (active, COT, SOT583). Buck redesigned per datasheet
   Figure 7 — see §1.2. MP2307 R23/C26 (COMP) removed; added R28/R29/R30/C30/C31/C32.
2. ~~L20 saturation~~ — **RESOLVED 2026-08-12**: datasheet (LCSC C5189747,
   CYA0630-3R3M) says saturation current DC **9.5 A** ≥ ~5 A required (peak
   ≈ 3.6 A). Verified by Paul. ✓
3. ~~C27 soft-start C-number~~ — **RESOLVED 2026-08-12**: **C1631** (FH
   0603B682K500NT, 6.8 nF 50 V X7R 0603, Basic) — already in the master BOM. ✓
4. ~~Charge controller common-negative~~ — **RESOLVED 2026-08-10**: Renogy
   Voyager RCC10VOYP chosen; datasheet says Grounding Type: **Positive**. Loop
   still works (common-mode = battery potential); J9 pin 1 left n.c. — see §1.5.
   Also noted: controller self-consumption 0.22 W @ 12 V ≈ 5 Wh/day — add to
   node energy budget (still ample margin with 20 W panel). Lithium mode must
   be set MANUALLY at install (auto-recognition covers non-lithium only);
   LI profile: boost 14.2 V, no float, no equalization, temp comp excluded. ✓

ERC checklist for Paul after drawing: no unconnected power pins; `VBAT_IN` vs
`VBAT` not shorted (protection bypassed = ERC won't catch it, eyeball it);
divider really on `VBAT` not `VBAT_IN` (so sense survives reverse hookup).
