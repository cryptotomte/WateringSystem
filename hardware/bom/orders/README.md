# Parts orders

One file per order, named `YYYY-MM-DD-<vendor>-order.csv`. These are records of
what was actually ordered, generated from the master BOM
(`../WateringSystem-rev2-BOM.md`) — never hand-maintained.

## 2026-08-12 — LCSC, module prototype stage

Generated from the master BOM for the stage-A module boards
(`../../rev2/design-notes/09-prototype-modules.md`). **51 line items,
~2300 components.**

### Upload

LCSC's **BOM Tool** (lcsc.com → *BOM Tool* / *Quick Order*) takes a CSV or
Excel upload and turns it into a cart. Upload `2026-08-12-lcsc-order.csv`,
then map the columns when prompted — the file uses LCSC's own header names,
so the mapping is usually detected automatically:

| Column | Purpose |
|---|---|
| `LCSC Part Number` | The one column that actually matters (C-numbers) |
| `Quantity` | Desired count — see the policy below |
| `Manufacturer Part Number` | Cross-check that LCSC resolves the same part |
| `Designator`, `Description` | Human context; LCSC ignores them |

**LCSC rounds quantities up to each part's MOQ / packaging multiple** and shows
the adjustment before checkout — so the numbers below are intent, not the exact
invoice. Review that screen rather than pre-computing MOQs.

### Quantity policy

Enough for **two complete board sets** (one to build, one to redo after a
hot-plate mistake), with deliberate over-ordering where a part is cheap and
losing one costs a re-order:

| Class | Ordered | Why |
|---|---|---|
| 0402/0603 R and C (pF/nF/0.1 µF/1 µF) | 100 | Cents each; hand-assembly attrition is real (tweezer launches, reflow drift) |
| Bulk ceramics (4.7/10/22 µF) | 20 | Bigger, pricier, less likely to vanish |
| Electrolytics, inductor, shunts | 5 | Few per board, distinctive |
| Discrete semis (MOSFET, TVS, zener, Schottky, ESD) | 5 | Cheap, easy to cook |
| Connectors, switches | 5 | THT/SMD mechanical, survives rework |
| ICs (ESP32, CP2102N, MP2393, THVD1426, BME280) | 2 | One spare each — the parts most likely to be killed by a bad reflow |
| INA226 | 4 | Two per board (0x40 pump + 0x41 solar) × 2 sets |
| LED | 10 | Trivial cost |

### NOT in this order — source elsewhere

| Item | Where | Note |
|---|---|---|
| Weidmüller 1715010000 ×4 (J2, J3, J8, J9) | **Already in Paul's stock** | Off-LCSC; chosen precisely because they were on hand |
| 2.54 mm pin headers + female sockets (J100/J101 module interconnect, J6, J7) | **Already in Paul's stock** | Assorted female-header kit + breakable male strips. Deliberately no C-number here: the old BOM's `C2337` for J6 was status *Partial* (unverified) and is not worth trusting |
| Solder paste, flux, braid, 0.1 mm stencil | JLCPCB (stencil) / local | See `../../rev2/HOWTO-module-pcb.md` §9 |
| 10 A blade fuse + waterproof inline holder | Jula, at install | `../../rev2/design-notes/01-power.md` §1.1 — SAFETY-REQUIRED |

### Verified before generating

- Built from `WateringSystem-rev2-BOM.md` only — the CSV/LCSC exports that sat
  beside it in `hardware/bom/` were byte-identical copies of the archived
  pre-restructure generation (MP2307, 22 µH inductor, 2 mΩ shunts, F1 polyfuse,
  R4/R5 bias) and would have produced a wrong order. They have been removed;
  `archive/` still holds them.
- Solar telemetry group **included** (U20 C49851, R27 C553969, C29 C14663) per
  the cleared gate — see `01-power.md` §1.5.
- Current generation confirmed: U4 = C6674609 (MP2393), L20 = C5189747
  (3.3 µH, 9.5 A sat), R1 = C5375417 (5 mΩ), U1 = C701344 (WROOM-32UE-N4).
