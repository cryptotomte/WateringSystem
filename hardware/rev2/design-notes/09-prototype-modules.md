# Block 9 — Prototype strategy: hand-assembled modules first

**Status:** Decided 2026-08-12
**Decision:** rev2 is realized in two stages. The integrated, JLCPCB-assembled
board remains the end goal, but the design is proven first as **per-block
module PCBs**, hand-assembled on Paul's hot plate and interconnected with
2.54 mm pin headers — devkit style. A wiring or component error then costs one
cheap module respin, not a full-board respin.

**Step-by-step execution: `../HOWTO-module-pcb.md`** (module projects that
reference the shared block sheets, header pinouts, panelization, bring-up
acceptance criteria).

## Stage A — modules (now)

- **One module PCB per schematic block** (or a sensible grouping — see bring-up
  order below). Same schematic sheets as the integrated board: **schematic is
  drawn once; only layout is done twice.**
- Each module exposes on 2.54 mm headers: its **hierarchical labels per the
  §0.5 pin contract** (`00-architecture.md`) plus the rails it consumes/provides
  (`VBAT`, `+3V3`, `GND`, `SENS_12V` as applicable). Net names in silkscreen at
  every header pin.
- Bare boards from JLCPCB (no assembly), **paste stencil per module** for the
  hot plate. Fine-pitch parts are all hotplate-friendly: MP2393 SOT583,
  CP2102N QFN28, INA226 VSSOP-10, ESP32-WROOM castellated.
- Components ordered from **LCSC using the master BOM C-numbers**
  (`hardware/bom/WateringSystem-rev2-BOM-LCSC.md`) + pin headers/sockets +
  stencil-size solder paste. Order min 2× quantities on passives (hand-assembly
  attrition).

Suggested bring-up order (each step powered by the previous):

1. **power** (block 1) — verify 3V3, PG, sensor-rail switch, VBAT_SENSE scaling
2. **mcu + usb-uart** (blocks 2+3, one module — the auto-program circuit and
   the USB island interact, keep them together)
3. **rs485** (block 4) + sensor on the bench
4. **pump** (block 5) with FL-35 + INA226
5. **level-sensors** (block 6), **i2c-env** (block 7)

This maps 1:1 onto the deferred HIL checklists and PR-14 bring-up tasks.

## Stage B — integrated board (after modules prove the design)

- Single board, JLCPCB fab + assembly (fabrication toolkit flow, BP/EP flags in
  the master BOM).
- Layout redone compactly; module experience feeds directly into placement
  (proven block layouts can be largely copy-pasted per KiCad section).
- Any changes discovered in stage A go **schematic-first** (design notes →
  sheets → both layouts), so the two stages never diverge.

## Consequences / reminders

- The layout task list starts with **module board outlines + header pinouts**,
  not the integrated board.
- Header pinout per module should follow the §0.5 contract table ordering, so
  inter-module jumper bundles are self-documenting.
- Power module headers must handle real current: pump path (block 5) needs
  screw terminals or ≥ 3 A rated headers, not signal pins — same for `VBAT`
  daisy-chaining and `PANEL_RTN` (~3 A).
- ESD/robustness caveat: header interconnects add inductance and pickup vs the
  final board — RS485 and buck behavior on modules is indicative, not final;
  re-verify on the stage-B board.
