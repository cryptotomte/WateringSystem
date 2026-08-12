# Archived rev2 BOM files — reference only

**Frozen 2026-06-20.** These are the pre-rework rev2 BOM files, kept as a
**reference source** (component ideas, verified LCSC numbers, sourcing
reasoning). They are **not** maintained and **not** authoritative.

## Why they were archived

The four files had drifted out of sync with each other and with the per-block
design-notes (`hardware/rev2/design-notes/`). Notably the CSVs still carried the
*two-pump* quantities from before the 2026-06-10 single-pump decision. Rather
than merge four parallel "truths", we restarted the master BOM block-by-block
from the design-notes. See `hardware/rev2/design-notes/00-architecture.md`.

## What replaced them

- **`hardware/bom/WateringSystem-rev2-BOM.md`** — the new master BOM. Built
  incrementally: one block's rows land only when that block's design-note is
  worked. Procurement columns (LCSC#, status, JLC type, alternatives) live here.
- **`hardware/rev2/design-notes/0N-*.md`** — per-block source of truth for the
  *design* (refdes, value, package, MPN, connectivity, net names).

## What's still useful in here

- **`WateringSystem-rev2-BOM-LCSC.md`** — verified LCSC C-numbers (2026-05 pass)
  for parts that carry over. Mine this when pulling a block into the new master.
- **`WateringSystem-rev2-BOM.md`** — the old full component list + the global
  content (cost summary, purchasing strategy, multi-node changelog) that hasn't
  been re-homed yet.
- The CSVs — the Mouser/JLCPCB export shapes, for reference only (stale: 2-pump).

Do not edit these. If something here is correct and current, copy it forward
into the new master or the relevant design-note.
