# Block 3 — USB-UART programming bridge (CP2102N)

**Status:** Draft for review · 2026-06-23
**Scope:** USB-C receptacle, CP2102N USB-to-UART bridge (bus-powered island),
USB ESD protection, the **auto-program (auto-reset) two-transistor circuit** that
drives `EN`/`BOOT` from DTR/RTS, and the programming UART seam to the MCU.
**Out of scope:** ESP32 + the EN/BOOT pull-ups and RC reset cap (block 2 — this
block only *pulls* `EN`/`BOOT` low through transistors); board power (block 1).

**Refdes convention:** new parts in block 3 use the **50-series** (R50…, C50…,
Q50…, D50) to avoid the 20–32 (block 1) and 40 (block 2) ranges. `U2` (CP2102N)
and `J1` (USB-C) are the BOM-named parts. KiCad re-annotates — this file is the
source of truth for *connectivity*, not numbering.

**Net naming:** UPPERCASE_SNAKE, 1:1 with this table.

| Net | Meaning |
|---|---|
| `USB_5V` | USB VBUS, 5 V from the host — **local to this block, not a board rail** |
| `USB_VBUS_SENSE` | Divided VBUS (≈3.4 V) to the CP2102N VBUS *sense* pin |
| `VDD_USB` | CP2102N internal-LDO 3.3 V output — **bridge-only island, NOT the board `+3V3`** |
| `USB_DP`, `USB_DM` | USB 2.0 full-speed data pair |
| `U0TXD`, `U0RXD` | Programming UART to MCU (names from the ESP32's point of view) |
| `EN`, `BOOT` | Reset / boot-select lines to MCU (pulled low here, pulled high in block 2) |

---

### Hierarchical labels (sheet pins) — place these on the sheet

Four signals cross to the MCU (matches `00-architecture.md §0.5` usb-uart). Shape =
**bidirectional** for all four: the UART pair crosses at the bridge, and EN/BOOT are
shared nodes (block 2 pulls high / this block pulls low).

| Hierarchical label | Shape | Inside this sheet connects to | Other end |
|---|---|---|---|
| `U0TXD` | **bidirectional** | R56 pin 1 (→ U2 RXD pin 20) | mcu IO1 (ESP TX) |
| `U0RXD` | **bidirectional** | R57 pin 1 (→ U2 TXD pin 21) | mcu IO3 (ESP RX) |
| `EN` | **bidirectional** | Q50 collector | mcu EN node (R11/C42/SW2) |
| `BOOT` | **bidirectional** | Q51 collector | mcu IO0 node (R12/C43/SW1) |

**Not hierarchical labels:** `GND` is a global power symbol (§0.3). `USB_5V`, `VDD_USB`,
`USB_VBUS_SENSE`, `USB_DP`, `USB_DM` are **local** to this bus-powered island (they never
leave the block — §3.2), so no hierarchical label.

---

## 3.0 CP2102N symbol — pin electrical types (curate first)

Imported EasyEDA/LCSC symbols come in `unspecified`. Set the pins as below before
ERC (same exercise as block 1/2). **Pin numbers below are for the CP2102N-A02-GQFN24R**
(the part on hand — QFN-24, datasheet Table 5.2); verify against the imported symbol.

| Pin(s) | GQFN24 pin # | Electrical type | Note |
|---|---|---|---|
| `VREGIN` | 7 | `power_input` | 5 V input to the internal LDO (from `USB_5V`) |
| `VDD` | 6 | `power_output` | 3.3 V LDO **output** — drives `VDD_USB` |
| `VIO` | 5 | `power_input` | I/O-rail supply — **separate pin on GQFN24; tie to VDD** (pin 5→6) |
| `VBUS` | 8 | `input` | attach **sense** only (not a supply) — fed by the divider |
| `GND` + center EPAD | 2 + pad | `power_input` | EPAD **must** be soldered to ground |
| `D+` | 3 | `bidirectional` | USB data + |
| `D-` | 4 | `bidirectional` | USB data − |
| `TXD` | 21 | `output` | bridge → ESP32 RX |
| `RXD` | 20 | `input` | ESP32 TX → bridge |
| `RTS` | 19 | `output` | modem-control → Q50 (auto-reset) |
| `DTR` | 23 | `output` | modem-control → Q51 (auto-reset) |
| `/RST` (RSTb) | 9 | `input` | active-low; internal pull-up — leave NC (optional ~10 k to `VDD_USB` for noise) |
| `CTS`,`DSR`,`DCD`,`RI`, GPIO0–3, SUSPEND/SUSPEND̅ | — | `passive` *(or no_connect)* | unused — leave NC |

Rule of thumb: `VDD` is the only `power_output` (it sources the island); `VREGIN`
/`VIO`/`GND` are `power_input`; UART/modem pins per direction; everything unused → NC.

**J1 (USB-C receptacle) — all pins `passive`.** Connectors are `passive` in KiCad;
VBUS/GND are power pins but stay `passive` and the ERC "driven by no pin" check is
satisfied by a **`PWR_FLAG` on `USB_5V`** (at the VBUS pins) — see the coverage check.
Set VBUS, GND, CC1, CC2, D+, D−, SBU1/2, and shield all to `passive`.

**D50 (USBLC6-2SC6 ESD array) — all pins `passive`.** A TVS/clamp array drives
nothing: the two I/O lines, the GND pin and the VBUS/Vcc clamp pin are all `passive`.
(Pin *numbers* per the imported symbol — verify the SOT-23-6 layout, §3.3.2.)

---

## 3.1 Component list

| Ref | Qty | Component | Value / type | Package | Function |
|---|---|---|---|---|---|
| U2 | 1 | CP2102N-A02-GQFN24R | USB 2.0 FS UART bridge | QFN-24 (4×4, EP) | USB-to-UART — **on hand (Paul)**; separate VIO pin (tie to VDD) |
| J1 | 1 | USB-C receptacle, 16-pin, USB-2.0 | SMD | USB-C | Host connector |
| D50 | 1 | USBLC6-2SC6 | USB ESD array (2-line) | SOT-23-6 | D+/D- ESD + VBUS clamp |
| Q50 | 1 | NPN (MMBT3904) | — | SOT-23 | Auto-reset: pulls `EN` low |
| Q51 | 1 | NPN (MMBT3904) | — | SOT-23 | Auto-reset: pulls `BOOT` low |
| R50 | 1 | Resistor | 10 kΩ | 0603 | Q50 base series (from DTR) |
| R51 | 1 | Resistor | 10 kΩ | 0603 | Q51 base series (from RTS) |
| R52 | 1 | Resistor | 5.1 kΩ | 0603 | USB-C CC1 pulldown (Rd, sink) |
| R53 | 1 | Resistor | 5.1 kΩ | 0603 | USB-C CC2 pulldown (Rd, sink) |
| R54 | 1 | Resistor | 22 kΩ 1% | 0603 | VBUS sense divider, top (`USB_5V`→sense) — Basic E24 |
| R55 | 1 | Resistor | 47 kΩ 1% | 0603 | VBUS sense divider, bottom (sense→GND) — Basic E24 |
| R56 | 1 | Resistor | 470 Ω | 0603 | `U0TXD` series — island back-power isolation (§3.3.4) |
| R57 | 1 | Resistor | 470 Ω | 0603 | `U0RXD` series — island back-power isolation (§3.3.4) |
| C50 | 1 | Ceramic X7R | 1 µF | 0603 | VREGIN decoupling (bulk) |
| C51 | 1 | Ceramic X7R | 100 nF | 0603 | VREGIN decoupling (HF) |
| C52 | 1 | Ceramic X5R | 4.7 µF, 16 V | 0603 | VDD decoupling (LDO output) — X5R Basic, 0603 (Samsung CL10A475KO8NNNC) |
| C53 | 1 | Ceramic X7R | 100 nF | 0603 | VDD decoupling (HF) |

*(All passives + MMBT3904 are JLC **Basic**. U2/J1/D50 are **Extended** — see
master BOM. CC = two separate 5.1 kΩ; do **not** share one resistor.)*

---

## 3.2 Power topology — bus-powered island

The bridge is **bus-powered and a separate power island** from the board:

- `USB_5V` (VBUS) → **VREGIN** → CP2102N internal LDO → **VDD** (3.3 V) = `VDD_USB`.
- **VIO tied to `VDD_USB`** → all UART/DTR/RTS logic is at 3.3 V, matching the ESP32.
- **`VDD_USB` is NOT connected to the board `+3V3`.** The only nets shared with the
  rest of the board are `GND`, `U0TXD`, `U0RXD`, `EN`, `BOOT`.

Why isolate: when USB is unplugged, VREGIN collapses, `VDD_USB`/VIO die, and the
bridge's TXD/RTS/DTR output drivers go high-impedance — **so they cannot back-feed
the ESP32's 3.3 V domain or the board buck**. Tying VIO to the board `+3V3` would
keep those drivers alive and risk contention on the UART and a back-feed path into
the buck output. Keep the islands separate; let the bridge live only when USB is in.

---

## 3.3 Connectivity

Pin-by-pin (`01-power.md` style).

### 3.3.1 USB-C receptacle J1 (TYPE-C-31-M-12, 16-pin USB-2.0)
Pad names per the HRO drawing (A1–A12 / B1–B12).

| Component / pin | Connect to | Net |
|---|---|---|
| J1 **VBUS** — pads A4, A9, B4, B9 (tie all 4) | bridge 5 V | `USB_5V` |
| J1 **GND** — pads A1, A12, B1, B12 (tie all 4) | ground | `GND` |
| J1 **shell** — 4 corner mounting tabs | ground | `GND` |
| J1 **CC1** (pad A5) | R52 pin 1 | — |
| R52 (5.1 kΩ) pin 1 | J1 CC1 (A5) | — |
| R52 pin 2 | ground | `GND` |
| J1 **CC2** (pad B5) | R53 pin 1 | — |
| R53 (5.1 kΩ) pin 1 | J1 CC2 (B5) | — |
| R53 pin 2 | ground | `GND` |
| J1 **DP1 (A6) + DP2 (B6)** (tie the two D+ pads) | → D50 pin 1 | `USB_DP` |
| J1 **DN1 (A7) + DN2 (B7)** (tie the two D− pads) | → D50 pin 3 | `USB_DM` |
| J1 **SBU1 (A8), SBU2 (B8)** | not used (USB-2.0) | NC |

Note: **CC1 (A5) and CC2 (B5) each get their OWN 5.1 kΩ pulldown to GND** (R52, R53)
— do not short them together; that's what advertises the board as a 5 V sink and gives
orientation detection. The two D+ pads (A6/B6) tie together, the two D− pads (A7/B7)
tie together → one pair into the ESD array. No series resistors on D+/D− (CP2102N is
full-speed). Shell-to-GND direct is fine here (RC `1 MΩ ‖ 4.7 nF` is the EMC alternative).
⚠️ The shell pin's *symbol name* (SHELL/SH/MH…) depends on the imported symbol — verify.

### 3.3.2 ESD protection D50 (USBLC6-2SC6, SOT-23-6)
Pinout per ST datasheet Fig. 1: **I/O1 = pins 1 & 6** (same node, pass-through),
**I/O2 = pins 3 & 4**, **GND = pin 2**, **VBUS = pin 5**.

| Component / pin | Connect to | Net |
|---|---|---|
| D50 **pin 1** (I/O1, connector side) | J1 D+ pair (A6/B6) | `USB_DP` |
| D50 **pin 6** (I/O1, device side) | U2 D+ (pin 3) | `USB_DP` |
| D50 **pin 3** (I/O2, connector side) | J1 D− pair (A7/B7) | `USB_DM` |
| D50 **pin 4** (I/O2, device side) | U2 D− (pin 4) | `USB_DM` |
| D50 **pin 2** (GND) | ground | `GND` |
| D50 **pin 5** (VBUS) | 5 V (clamp reference) | `USB_5V` |

Place D50 **in-line, right at the connector**: the data trace enters on the
connector-side pin and leaves on the device-side pin of each pair (pins 1→6 for D+,
3→4 for D−), so the part sits in series for best ESD performance. The two pins of each
pair are internally the same net, so direction within a pair is electrically symmetric.

### 3.3.3 CP2102N power
| Component / pin | Connect to | Net |
|---|---|---|
| U2 **VREGIN** (pin 7) | `USB_5V` | `USB_5V` |
| U2 **VDD** (pin 6) | `VDD_USB`; **short to VIO (pin 5)** | `VDD_USB` |
| U2 **VIO** (pin 5) | short to VDD (pin 6) — sets 3.3 V logic | `VDD_USB` |
| U2 **VBUS** (pin 8, sense) | R54/R55 divider node | `USB_VBUS_SENSE` |
| U2 **GND** + center EPAD | ground | `GND` |
| C50 (1 µF) pin 1 | `USB_5V` (tight at VREGIN pin 7) | `USB_5V` |
| C50 pin 2 | ground | `GND` |
| C51 (100 nF) pin 1 | `USB_5V` (tight at VREGIN pin 7) | `USB_5V` |
| C51 pin 2 | ground | `GND` |
| C52 (4.7 µF) pin 1 | `VDD_USB` (tight at VDD pin 6) | `VDD_USB` |
| C52 pin 2 | ground | `GND` |
| C53 (100 nF) pin 1 | `VDD_USB` (tight at VDD pin 6) | `VDD_USB` |
| C53 pin 2 | ground | `GND` |
| R54 (22 kΩ) pin 1 | `USB_5V` | `USB_5V` |
| R54 pin 2 | divider node → U2 VBUS (pin 8) | `USB_VBUS_SENSE` |
| R55 (47 kΩ) pin 1 | divider node | `USB_VBUS_SENSE` |
| R55 pin 2 | ground | `GND` |

⚠️ **VBUS divider is mandatory** — the CP2102N VBUS pin is a *sense* input with an
abs-max of VIO + 2.5 V; 5 V straight in damages it. **22 k / 47 k → ≈3.41 V** at the
sense node — same as the datasheet's 22.1 k/47.5 k (3.41 V), comfortably between the
2.7 V attach threshold (VIO−0.6) and the 5.8 V abs-max, with margin even at 5 %
tolerance (E96 not needed — Basic E24 is fine). Keep total capacitance on
`USB_5V` ≤ 4.7 µF so the host inrush protection does not trip on plug-in.

### 3.3.4 Programming UART (→ block 2)
| Component / pin | Connect to | Net |
|---|---|---|
| R56 (470 Ω) pin 1 | `U0TXD` hierarchical pin (ESP32 IO1) | `U0TXD` |
| R56 pin 2 | U2 **RXD** (pin 20, bridge in) | (local: bridge RXD) |
| U2 **RXD** (pin 20) | R56 pin 2 | (local: bridge RXD) |
| R57 (470 Ω) pin 1 | `U0RXD` hierarchical pin (ESP32 IO3) | `U0RXD` |
| R57 pin 2 | U2 **TXD** (pin 21, bridge out) | (local: bridge TXD) |
| U2 **TXD** (pin 21) | R57 pin 2 | (local: bridge TXD) |

Note: TX↔RX cross at the bridge — bridge RXD listens to the ESP's `U0TXD`, bridge
TXD feeds the ESP's `U0RXD`. Both hierarchical pins (matches block 2 §2.3.4).

**Why R56/R57 (added 2026-07-02, second-opinion review A3):** the bridge island and
the board are powered independently, so one side is routinely alive while the other
is dead — and an idle UART line drives HIGH into the dead side's I/O clamp diode:
- *Board on, USB out (normal field state, 24/7):* ESP32 `U0TXD` idles HIGH into the
  dead bridge RXD → continuous out-of-spec injection + a standing battery drain.
- *USB in, board off (common bench state):* bridge TXD idles HIGH into the unpowered
  ESP32 IO3 → parasitically back-powers the board `+3V3` (~2.5 V, brownout-loop risk).

470 Ω limits either injection to ~5 mA (harmless) and is invisible at programming
baud rates (RC with ~15 pF pin capacitance ≈ 7 ns ≪ a 921600-baud bit).

### 3.3.5 Auto-program (auto-reset) — two NPN, cross-coupled emitters
The canonical Espressif/NodeMCU circuit. **The emitters cross-couple to the
opposite control line — they are NOT grounded.** That cross-coupling is the whole
point: when DTR and RTS are driven to the *same* level, neither transistor conducts,
so opening a serial monitor (which toggles both) does not reset the chip.

| Component / pin | Connect to | Net |
|---|---|---|
| R50 (10 kΩ) pin 1 | U2 **DTR** (pin 23) | — |
| R50 pin 2 | Q50 base | — |
| Q50 **base** | R50 pin 2 | — |
| Q50 **emitter** | U2 **RTS** (pin 19) | — |
| Q50 **collector** | `EN` (pull-up + RC in block 2) | `EN` |
| R51 (10 kΩ) pin 1 | U2 **RTS** (pin 19) | — |
| R51 pin 2 | Q51 base | — |
| Q51 **base** | R51 pin 2 | — |
| Q51 **emitter** | U2 **DTR** (pin 23) | — |
| Q51 **collector** | `BOOT` (pull-up R12 in block 2) | `BOOT` |
| `EN` *(hierarchical pin)* | block 2 — EN node (R11/C42/SW2) | `EN` |
| `BOOT` *(hierarchical pin)* | block 2 — IO0 node (R12/C43/SW1) | `BOOT` |

**How it conducts:** an NPN turns on when base > emitter (+0.7 V).
- Q50 (EN) conducts only when **DTR=H and RTS=L** → pulls `EN` low (reset).
- Q51 (BOOT) conducts only when **RTS=H and DTR=L** → pulls `BOOT` low (bootloader).

Truth table (pin voltages; esptool sequences DTR/RTS to enter download mode):

| DTR | RTS | EN | BOOT | Result |
|---|---|---|---|---|
| H | H | high | high | normal run (both equal → no action) |
| L | L | high | high | normal run |
| H | L | **0** | high | chip held in reset |
| L | H | high | **0** | boot to ROM bootloader |

> **EN reset cap dependency (→ block 2 open item 3):** reliable auto-reset needs
> ≈1 µF on `EN`→GND so EN rises slowly enough that `BOOT` is still low when the chip
> leaves reset. **Recommend resolving block 2 `C42` = 1 µF** (not 100 nF). Without it,
> uploads intermittently fail ("Failed to connect / timed out").

### 3.3.6 Unused CP2102N pins
Leave `CTS`, `DSR`, `DCD`, `RI`, `GPIO0–3`, `SUSPEND`, `SUSPEND̅`, and `/RST`
unconnected (`/RST` has an internal pull-up). Place **no-connect flags** on them so
ERC stays quiet (§2.3.6-style).

---

## 3.4 Open items / decisions

1. **5 V from USB as a board supply (arch §0.9 item 2) — RESOLVED: keep local.**
   `USB_5V` powers only the bridge island; the board runs from the battery/buck.
   USB is programming/console only, not a power source. (If bench-powering off USB
   is later wanted, OR-in `USB_5V` to the buck input via an ideal-diode — a separate
   change, not block 3.)
2. **Block 2 `C42` (EN cap) — recommend 1 µF** for auto-reset reliability (§3.3.5).
   Closes block 2 open item 3 in favour of 1 µF over 100 nF.
3. **USB-C variant** — TYPE-C-31-M-12 (C165948) is a bare USB-2.0 16-pin part
   (no internal CC resistors). Confirm footprint has the through-hole mounting legs
   for mechanical strength on the enclosure wall.
4. **R56/R57 (470 Ω UART series) — ADDED 2026-07-02 (second-opinion review A3).**
   Island back-power isolation, both directions — see §3.3.4. Generic Basic 0603;
   pick the C-number in the plugin.
5. **CP2102N = CP2102N-A02-GQFN24R (`C969151`) — the part Paul has on hand.** QFN-24
   (4×4), A02 silicon. Chosen over the GQFN28 because (a) it's in stock at home — no
   purchase/feeder for it — and (b) it has the **separate VIO pin** this design ties to
   VDD (the GQFN28 has VIO internally bonded to VDD; the GQFN24's 4 GPIO / no
   charger-detect are features we don't use). Confirm Basic/Extended class in the
   plugin if JLC assembles it; if Paul hand-places it (hotplate), the JLC class is moot.

---

## Coverage check

Consumes from the pin contract (`00-architecture.md §0.5` usb-uart): `U0TXD`,
`U0RXD`, `EN`, `BOOT` (all to mcu), plus `GND`. Provides no board rails (`USB_5V`/
`VDD_USB` are local islands). The bridge is fully bus-powered; nothing on the board
depends on it being present.

**ERC/build checklist for Paul:**
- `PWR_FLAG` on `USB_5V` (the connector supplies it — one flag, at J1 VBUS).
- `VDD_USB` is driven by U2 VDD (`power_output`) → VIO (`power_input`) needs no flag.
- EPAD/GND pad soldered; D50 placed at the connector; D+/D- as a short ~90 Ω pair.
- No-connect flags on all unused U2 pins (§3.3.6).
- `EN`/`BOOT` exit as hierarchical pins only — pull-ups/caps belong to block 2.
