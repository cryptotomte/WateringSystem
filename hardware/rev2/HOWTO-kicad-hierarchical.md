# HOWTO — modular hierarchical design in KiCad 10 (rev2)

A from-scratch walkthrough of the workflow described in
`design-notes/00-architecture.md`, using the **power** module as the worked
example. Assumes KiCad 10.0.x and no prior hierarchical-sheet experience.

> Menu wording can shift slightly between point releases. Where a hotkey is
> given it is the KiCad default; check **Preferences → Hotkeys** if it differs.

---

## 0. Mental model (read once)

- The **root sheet** holds no parts — only one box ("sheet symbol") per module.
- Each **module** is its own `.kicad_sch` file with the actual components.
- A module's **interface** is its *hierarchical pins*. You make a pin by placing
  a **hierarchical label** *inside* the module; that label then appears as a pin
  on the module's box in the root sheet, where you wire it.
- **Power and ground are not pins.** You place a **power symbol** (`GND`, `+3V3`,
  custom `VBAT`, `SENS_12V`) wherever a rail is needed; KiCad joins all symbols
  of the same name everywhere. One `PWR_FLAG` per rail tells ERC the rail is fed.

So: signals travel through pins (visible wiring on the root sheet); rails travel
by name (invisible, via power symbols). This is the whole trick.

---

## 1. Create the rev2 project

1. **KiCad → File → New Project** → save as
   `hardware/rev2/WateringSystem-rev2.kicad_pro`. (Separate project from the
   rev1 `hardware/` one — see `00-architecture.md` §0.2.)
2. Open the **Schematic Editor**. The blank page that opens *is* the root sheet
   (`WateringSystem-rev2.kicad_sch`).
3. Fill in the title block (bottom-right): **File → Page Settings**. Set title
   "WateringSystem rev2 — root".

## 2. Create the power module sheet

1. On the root sheet: **Place → Add Hierarchical Sheet** (hotkey `S`). Click two
   corners to draw a box.
2. In the dialog: **Sheet name** = `power`, **File name** = `power.kicad_sch`.
   OK. You now have an empty module referenced from the root.
3. **Double-click the box** to enter the power sheet. (The breadcrumb at the top
   shows `Root » power`; double-click empty space or use the up-arrow to go
   back.)

## 3. Draw the power module (per `design-notes/01-power.md`)

Inside `power.kicad_sch`:

1. **Place symbols** (`A` = Add Symbol) for the BOM parts this module owns: J2,
   the buck U4 (MP2307) + L1 + caps, reverse protection + TVS, the battery-sense
   divider, the `SENS_12V` high-side FET, LED1. Wire them per the connectivity
   tables in `01-power.md` §1.1–§1.5.
   - If a symbol/footprint isn't in the stock libraries (MP2307, THVD1426,
     ESP32-WROOM, INA226), grab it from the manufacturer or SnapEDA and add it
     via **Preferences → Manage Symbol Libraries**. Do this per part as you go.
2. **Place the rails it *produces*** as power symbols (`P` = Add Power Symbol):
   `+3V3` on the buck output, `GND` on grounds, and custom symbols `VBAT` and
   `SENS_12V`. If `VBAT`/`SENS_12V` aren't in the power library, make them: any
   power symbol can be renamed, or copy `+12V` and relabel.
3. **Add one `PWR_FLAG`** each on `VBAT`, `+3V3`, `SENS_12V`, `GND` at the point
   the power module generates them (e.g. `PWR_FLAG` on the buck output net for
   `+3V3`). This is what stops ERC complaining the rails are unfed.

## 4. Define the module's interface (hierarchical labels)

Still inside `power.kicad_sch`, place a **hierarchical label** (Place → Add
Hierarchical Label, hotkey `H`) on each signal that must reach another module —
from `00-architecture.md` §0.5 *power*:

| Label | Shape / direction | Attach to |
|---|---|---|
| `SENS_GATE` | input | gate of the `SENS_12V` high-side FET |
| `VBAT_SENSE` | output | midpoint of the battery-sense divider |
| `I2C_SDA` | bidirectional | only if the solar INA226 is populated (DNP) |
| `I2C_SCL` | bidirectional | only if the solar INA226 is populated (DNP) |

The label *shape* (input/output/bidir) is cosmetic but set it to match the
table — it documents direction and makes the root sheet readable. Do **not**
make hierarchical labels for `VBAT`/`3V3`/`GND`/`SENS_12V` — those are rails
(step 3), not pins.

## 5. Run ERC on the module in isolation

**Inspect → Electrical Rules Checker → Run**. Fix everything before integrating.
Common first-pass items for power: missing `PWR_FLAG`, an unconnected pin you
meant to leave open (mark it with a **no-connect flag**, `Q`), the `VBAT_IN` vs
`VBAT` short check from `01-power.md` (ERC won't catch a bypassed protection
FET — eyeball it).

## 6. Wire the module into the root sheet

1. Go back up to the root sheet.
2. The `power` box now needs its **sheet pins** — one per hierarchical label.
   Right-click the box → **Import Sheet Pins** (or Place → Add Sheet Pin and
   pick from the list). `SENS_GATE`, `VBAT_SENSE`, (`I2C_*`) appear on the box.
   - KiCad 9/10 has a **sheet-pin ↔ hierarchical-label sync** tool so the two
     stay matched if you rename later — use it instead of hand-editing pins.
3. Draw wires from each sheet pin toward where its partner module will sit
   (you'll connect them once those modules exist). For now you can drop a
   matching **hierarchical label** on the root-sheet wire end, or leave a stub.
4. Place the **global rails** on the root sheet too if any root-level part needs
   them; mostly the rails just propagate by name once `power` sources them.

## 7. Annotate + assign footprints

1. **Tools → Annotate Schematic** — assigns refdes. KiCad re-annotates freely;
   the `00-architecture.md` / `01-power.md` numbering is the connectivity source
   of truth, not a hard requirement on KiCad's numbers.
2. **Tools → Assign Footprints** (or per-symbol in its properties). Match the
   packages in the rev2 BOM (SOIC-8-EP for U4, 2512 for the shunt, etc.).
3. Re-run ERC at the root level.

## 8. Save the module as a Design Block (reuse — the payoff)

Once `power` is stable and ERC-clean:

1. Open the **Design Blocks** panel in the Schematic Editor (**View → Panels →
   Design Blocks** if hidden). **Create the library from inside the panel:**
   right-click in the panel → **New Library** → choose scope **global** (visible
   to all projects — the point of the exercise) and point the path somewhere
   shareable, *not* inside a single project folder. **Chosen (2026-07-02):**
   `${KICAD10_3RD_PARTY}/lib/embedded_blocks.kicad_blocks` — the KiCad path
   variable keeps the entry portable. One library, many blocks: every module
   (power, mcu, …) is saved as its own block *inside* this one library, and the
   §9 PCB layout blocks go to the same library/block names (that is what links
   schematic ↔ layout). This creates the `.kicad_blocks` folder on disk AND the
   library-table entry in one go (no need to touch Preferences → Manage Design
   Block Libraries manually — that dialog only *registers* existing folders, it
   never creates them).
   On disk: the library is a folder `Name.kicad_blocks`; each saved block becomes
   a subfolder `blockname.kicad_block` holding a `.kicad_sch` (+ `.kicad_pcb` for
   layout blocks) and a `.json` with metadata.
   ⚠️ Avoid the panel's "hide library tree" button — it hides the whole panel
   (restore via View → Panels → Design Blocks).
2. Inside the sheet: **Ctrl+A** — select *everything*, **including the
   hierarchical labels, power symbols and PWR_FLAGs**, not just the components.
   The labels ARE the module interface: they become the sheet pins when the block
   is reused (Import Sheet Pins), and the power symbols self-connect to the target
   project's global rails. (PWR_FLAGs travel with the *power* block only — one per
   net per board; the other blocks have none.) Then right-click → **Save Selection
   as Design Block** (or the panel's save button). Name it `power-12v-buck`.
   Refdes numbers don't matter — the target project re-annotates.
3. To reuse in a future board: open that project's Design Blocks panel and drop
   `power-12v-buck` in. It comes in as an independent copy with its interface
   intact.

## 9. PCB — module-by-module layout + the layout design block (KiCad 10)

**There is no per-sheet import** — `Update PCB from Schematic` (F8) always brings
in *all* footprints. But you never hand-sort: KiCad clusters and selects by
hierarchical sheet for you. The per-module loop:

### 9.1 One-time import
1. Root-level **ERC clean** first, and every symbol must have a footprint
   (Fabrication Toolkit parts have them; check the generics: J2/J3/J8/J9 terminal
   blocks, J6/J7 pin headers, JP1/JP2 `SolderJumper` footprints).
2. PCB editor → **Tools → Update PCB from Schematic (F8)**. All new footprints
   arrive attached to the cursor, **already clustered by schematic sheet** (and by
   position within the sheet). Drop the whole heap outside the board outline.

### 9.2 Per module — gather, place, route
1. **Grab one sheet's parts:** right-click any footprint from the module →
   **Select → Items in Same Hierarchical Sheet**. (Alternative from the schematic
   side: select the parts in the sheet → right-click → **Select on PCB** —
   cross-probing selects the matching footprints.)
2. **Pack and Move Footprints (`P`)** on that selection — KiCad packs them into a
   tight cluster on the cursor; drop it in its own staging area.
3. Lay the module out there: decoupling tight at IC pins, the loops per the
   design-note layout hints (buck loop `01` §1.2, pump switch loop `05` §5.2.1,
   D50/D4 at their connectors, Q2/Q20 copper pours).
4. Route the module's *internal* nets. Leave inter-module nets (rails, the §0.5
   signals) unrouted — they are routed once modules are placed on the board.
5. Select the finished cluster (step 1 again) → right-click → **Group**.

### 9.3 Save as PCB design block (the reuse payoff)
1. With the group selected → save as a **PCB Design Block** into the shared
   library (same one as the schematic blocks, e.g.
   `hardware/lib/WateringSystem` — Design Blocks panel in the PCB editor).
   The schematic group's *library link* carries over, tying layout ↔ schematic
   blocks together.
2. In a future board, recall the block: whole placement + routing comes back as
   one independent copy — caps already by the pins.

### 9.4 Board integration
Once all modules exist as placed groups: drag the groups into position inside the
board outline (a group moves as one unit), then route the inter-module nets
(rails + §0.5 signals) and pour GND.

> For *this* single-pump node there's only one of each module, so step 9 is
> about future reuse, not this board. If a later board has repeated channels
> (multi-pump), use **multi-channel design** to replicate one channel's layout
> across instances within that project.

---

## Quick reference — the loop per module

```
root: Add Hierarchical Sheet  →  enter sheet
  └ place parts + wire (per NN-*.md)
  └ place rails as power symbols (+ one PWR_FLAG each)
  └ place hierarchical labels for the interface signals
  └ ERC the sheet
root: Import Sheet Pins  →  wire pins to neighbour modules
annotate → assign footprints → root ERC
save stable module as a Design Block
```

Next module after power: **mcu** + **usb-uart** (see `00-architecture.md` §0.8
build order).
