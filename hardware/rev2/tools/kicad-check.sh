#!/usr/bin/env bash
# KiCad headless checks + exports for the rev2 hardware (and rev1 DRC).
# Same commands CI runs (.github/workflows/hardware-check.yml) — run locally
# before pushing. Output lands in hardware/rev2/export/ (gitignored).
#
#   hardware/rev2/tools/kicad-check.sh            # erc + drc + exports
#   hardware/rev2/tools/kicad-check.sh erc        # one step only
#
# Uses kicad-cli from PATH, else the macOS KiCad 10 bundle.
set -euo pipefail
HW="$(cd "$(dirname "$0")/../.." && pwd)"
REV2="$HW/rev2"; OUT="$REV2/export"; mkdir -p "$OUT"
KC="${KICAD_CLI:-$(command -v kicad-cli || echo /Applications/KiCad/Kicad10.app/Contents/MacOS/kicad-cli)}"
step="${1:-all}"; rc=0
run() { echo "== $*"; "$@" 2>&1 | grep -v '^Fontconfig' || true; }

has_outline() { grep -qE '\(gr_(line|rect|poly|arc|circle)' "$1" && grep -q 'Edge.Cuts' "$1"; }

if [[ $step == all || $step == erc ]]; then
  echo "### ERC rev2 schematic"
  "$KC" sch erc -o "$OUT/rev2-erc.rpt" --severity-error --exit-code-violations \
      "$REV2/WateringSystem-rev2.kicad_sch" 2>&1 | grep -v '^Fontconfig' || rc=1
fi
if [[ $step == all || $step == drc ]]; then
  echo "### DRC rev1 board"
  "$KC" pcb drc -o "$OUT/rev1-drc.rpt" --severity-error --exit-code-violations \
      "$HW/WateringSystem.kicad_pcb" 2>&1 | grep -v '^Fontconfig' || rc=1
  if has_outline "$REV2/WateringSystem-rev2.kicad_pcb"; then
    echo "### DRC rev2 board"
    "$KC" pcb drc -o "$OUT/rev2-drc.rpt" --severity-error --exit-code-violations \
        "$REV2/WateringSystem-rev2.kicad_pcb" 2>&1 | grep -v '^Fontconfig' || rc=1
  else
    echo "### DRC rev2 board: SKIPPED — no board outline yet (stage-A modules / stage-B layout pending)"
  fi
fi
if [[ $step == all || $step == export ]]; then
  echo "### Export rev2 schematic → SVG + PDF"
  run "$KC" sch export svg -o "$OUT/svg" --no-background-color "$REV2/WateringSystem-rev2.kicad_sch"
  run "$KC" sch export pdf -o "$OUT/WateringSystem-rev2.pdf" "$REV2/WateringSystem-rev2.kicad_sch"
fi
echo "### done (rc=$rc) — reports and exports in $OUT"
exit $rc
