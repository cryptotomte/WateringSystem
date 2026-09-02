#!/usr/bin/env python3
"""Generate the stage-A module-interconnect views from the pin contract.

Source of truth is hardware/rev2/HOWTO-module-pcb.md (§4 modules, §6 header
pinouts, §7 current paths). The MODULES block below mirrors those tables and
is CROSS-CHECKED against the §6 J101 table on every run: a mismatch aborts,
so this script can never quietly draw something the HOWTO does not say.

Run:   python3 hardware/rev2/tools/gen-module-canvas.py
Out:   hardware/rev2/export/module-canvas/   (gitignored)
         module-interconnect.html   <- open in any browser (standalone preview)
         Main.dc.html, NetMatrix.dc.html, canvas.json   <- artboards for the
         editable Claude Design canvas (published from a Claude Code session)

No dependencies beyond Python 3.
"""
import html, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REV2 = os.path.dirname(HERE)
HOWTO = os.path.join(REV2, "HOWTO-module-pcb.md")
OUT = os.path.join(REV2, "export", "module-canvas")
os.makedirs(OUT, exist_ok=True)

RAIL = ["VBAT", "GND", "+3V3", "GND", "SENS_12V", "GND"]          # J100 fixed pinout
MODULES = [  # (name, blocks, J100 pins used, J101 nets, field connectors, notes)
 ("mod-power",  "block 1",       {1,2,3,4,5,6},
  ["GND","SENS_GATE","VBAT_SENSE","PWR_PG","I2C_SDA","I2C_SCL","GND"],
  [("J2","battery in","VBAT_IN · GND"),("J8","panel +","PANEL_IN · PANEL_OUT"),("J9","panel −","PANEL_RTN in · out (isolated)")],
  "SOURCE of all rails · PWR_FLAGs live here"),
 ("mod-mcu",    "blocks 2+3+8",  {3,4},
  ["GND","VBAT_SENSE","PWR_PG","SENS_PWR_EN","RS485_TX","RS485_RX","PUMP_EN","RESERVOIR_LOW_LEVEL","RESERVOIR_HIGH_LEVEL","I2C_SDA","I2C_SCL","GND"],
  [("J1","USB-C","prog / power island"),("J6","JTAG 1×6","IO12–15 (DNP)"),("J7","expansion 1×8","IO18 SCK · 19 MISO · 23 MOSI · 4 CS · 27 IRQ")],
  "U0TXD/U0RXD/EN/BOOT stay on-board"),
 ("mod-rs485",  "block 4",       {1,2,3,4,5,6},
  ["GND","RS485_TX","RS485_RX","SENS_PWR_EN","SENS_GATE","GND"],
  [("J4","NPK sensor 4-pin","A · B · SENS_12V · GND")],
  "THVD1426 auto-direction · Q60 drives SENS_GATE"),
 ("mod-pump",   "block 5",       {3,4},
  ["GND","PUMP_EN","I2C_SDA","I2C_SCL","GND"],
  [("J3","pump out","PUMP_P · PUMP_N"),("—","VBAT/GND feed","screw terminal ← mod-power (≤8 A)")],
  "VBAT NEVER via J100 pin 1 — current path"),
 ("mod-level",  "block 6",       {3,4,5,6},
  ["GND","RESERVOIR_LOW_LEVEL","RESERVOIR_HIGH_LEVEL","GND"],
  [("J5","XKC-Y26 LOW","VCC · OUT · GND · MODE"),("J10","XKC-Y26 HIGH","VCC · OUT · GND · MODE")],
  "active-LOW via 2N7002 · JP1/JP2 MODE"),
 ("mod-i2c-env","block 7",       {3,4},
  ["GND","I2C_SDA","I2C_SCL","GND"],
  [],
  "owns the ONLY I²C pull-ups (R90/R91)"),
]


# ---------------- cross-check against HOWTO §6 (tables are the truth) ---------
def howto_j101():
    """Parse the '| Module | J101 pinout (pin 1 → n) |' table from the HOWTO."""
    text = open(HOWTO, encoding="utf-8").read()
    m = re.search(r"\| Module \| J101 pinout[^\n]*\n\|[-| ]+\n((?:\|[^\n]*\n)+)", text)
    if not m:
        sys.exit("gen-module-canvas: could not find the J101 table in HOWTO-module-pcb.md §6")
    table = {}
    for line in m.group(1).strip().splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        name = cells[0].strip("`")
        nets = [p.strip().strip("`") for p in cells[1].split("·")]
        table[name] = nets
    return table

expected = howto_j101()
for name, _, _, j101, _, _ in MODULES:
    if name not in expected:
        sys.exit(f"gen-module-canvas: {name} missing from HOWTO §6 J101 table")
    if expected[name] != j101:
        sys.exit(f"gen-module-canvas: J101 mismatch for {name}\n  HOWTO : {expected[name]}\n  script: {j101}\nFix the script data to match the HOWTO (the HOWTO is the source of truth).")
extra = set(expected) - {m[0] for m in MODULES}
if extra:
    sys.exit(f"gen-module-canvas: HOWTO lists modules the script does not know: {sorted(extra)}")

# --- KiCad classic palette --------------------------------------------------
C = dict(group="#0000c8", body="#840000", fill="#ffffc2", wire="#008400",
         net="#101010", hier="#848400", muted="#8a8a8a", paper="#fdfdfa", warn="#b00020")
FONT = "'IBM Plex Sans', 'Helvetica Neue', Arial, sans-serif"
MONO = "'IBM Plex Mono', Menlo, Consolas, monospace"

def pin_strip(ref, nets, used=None, title=""):
    rows = []
    for i, net in enumerate(nets, 1):
        on = (used is None) or (i in used)
        col = C["net"] if on else C["muted"]
        label = net if on else "n/c"
        rows.append(
          f'<div style="display:flex;align-items:center;gap:6px;height:18px">'
          f'<span style="font-family:{MONO};font-size:10px;color:{C["body"]};width:14px;text-align:right">{i}</span>'
          f'<span style="display:inline-block;width:10px;height:10px;border:1.5px solid {C["body"]};background:{C["fill"] if on else "#eee"};box-sizing:border-box"></span>'
          f'<span style="display:inline-block;width:16px;height:0;border-top:1.5px solid {C["wire"] if on else "#ccc"}"></span>'
          f'<span style="font-family:{MONO};font-size:11px;color:{col};{"" if on else "font-style:italic"}">{html.escape(label)}</span>'
          f'</div>')
    return (f'<div style="display:flex;flex-direction:column;gap:2px">'
            f'<div style="font-family:{MONO};font-size:11px;color:{C["body"]};font-weight:600">{ref} <span style="color:{C["muted"]};font-weight:400">{title}</span></div>'
            + "".join(rows) + '</div>')

def field_conn(conns):
    if not conns:
        return f'<div style="font-family:{FONT};font-size:10px;color:{C["muted"]};font-style:italic">no field connector</div>'
    items = []
    for ref, name, pins in conns:
        items.append(
          f'<div style="display:flex;gap:6px;align-items:baseline">'
          f'<span style="font-family:{MONO};font-size:11px;color:{C["body"]};font-weight:600;min-width:26px">{html.escape(ref)}</span>'
          f'<span style="font-family:{FONT};font-size:11px;color:{C["net"]}">{html.escape(name)}</span>'
          f'<span style="font-family:{MONO};font-size:10px;color:{C["muted"]}">{html.escape(pins)}</span></div>')
    return f'<div style="display:flex;flex-direction:column;gap:3px">{"".join(items)}</div>'

def module_card(name, blocks, used, j101, conns, note):
    return (
      f'<div style="display:flex;flex-direction:column;gap:10px;border:1.5px solid {C["group"]};background:{C["paper"]};padding:10px 12px;min-width:0">'
      f'<div style="display:flex;justify-content:space-between;align-items:baseline">'
      f'<span style="font-family:{FONT};font-size:15px;font-weight:600;color:{C["group"]}">{name}</span>'
      f'<span style="font-family:{FONT};font-size:11px;color:{C["group"]}">{blocks}</span></div>'
      f'<div style="display:flex;gap:22px;align-items:flex-start">'
      + pin_strip("J100", RAIL, used, "rails 1×6")
      + pin_strip("J101", j101, None, f"signals 1×{len(j101)}")
      + '</div>'
      f'<div style="border-top:1px dashed {C["muted"]};padding-top:8px">{field_conn(conns)}</div>'
      f'<div style="font-family:{FONT};font-size:10.5px;color:{C["hier"]}">{html.escape(note)}</div>'
      '</div>')

HEAD = f'''<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <script src="./support.js"></script>
</head>
<body>
<x-dc>
<helmet>
  <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;600&family=IBM+Plex+Sans:wght@400;600&display=swap">
  <style>
    body {{ margin: 0; background: #ffffff; font-family: {FONT}; }}
    a {{ color: {C["group"]}; }} a:hover {{ color: {C["body"]}; }}
  </style>
</helmet>'''
TAIL = '''
</x-dc>
</body>
</html>
'''

# ---------------- Main: six module cards + current-path mini-diagram -------
cards = "".join(module_card(*m) for m in MODULES)
current_paths = f'''
<div style="display:flex;flex-direction:column;gap:8px;border:1.5px solid {C["warn"]};background:{C["paper"]};padding:10px 12px">
  <div style="font-family:{FONT};font-size:13px;font-weight:600;color:{C["warn"]}">Current paths — screw terminals only, never a 2.54 mm header</div>
  <svg width="1000" height="118" viewBox="0 0 1000 118" style="display:block">
    <defs><marker id="ah" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="{C["body"]}"/></marker></defs>
    <g font-family="{MONO}" font-size="11" fill="{C["net"]}">
      <rect x="10" y="30" width="150" height="56" fill="{C["fill"]}" stroke="{C["body"]}" stroke-width="1.5"/>
      <text x="85" y="52" text-anchor="middle" font-weight="600">Battery 12.8 V</text>
      <text x="85" y="70" text-anchor="middle" font-size="10">10 A blade fuse in + lead</text>
      <line x1="160" y1="50" x2="270" y2="50" stroke="{C["body"]}" stroke-width="3" marker-end="url(#ah)"/>
      <text x="215" y="42" text-anchor="middle" font-size="10">VBAT_IN ≥1.5 mm²</text>
      <line x1="160" y1="70" x2="270" y2="70" stroke="#222" stroke-width="3" marker-end="url(#ah)"/>
      <text x="215" y="86" text-anchor="middle" font-size="10">GND</text>
      <rect x="272" y="20" width="190" height="78" fill="{C["fill"]}" stroke="{C["group"]}" stroke-width="1.5"/>
      <text x="367" y="40" text-anchor="middle" font-weight="600" fill="{C["group"]}">mod-power</text>
      <text x="367" y="58" text-anchor="middle" font-size="10">J2 battery · Q20 ideal diode</text>
      <text x="367" y="74" text-anchor="middle" font-size="10">J8/J9 panel loop (INA226 0x41)</text>
      <text x="367" y="90" text-anchor="middle" font-size="10">VBAT/GND out → pump feed</text>
      <line x1="462" y1="50" x2="590" y2="50" stroke="{C["body"]}" stroke-width="3" marker-end="url(#ah)"/>
      <text x="526" y="42" text-anchor="middle" font-size="10">VBAT ~4 A run / 8 A max</text>
      <line x1="462" y1="70" x2="590" y2="70" stroke="#222" stroke-width="3" marker-end="url(#ah)"/>
      <text x="526" y="86" text-anchor="middle" font-size="10">GND return</text>
      <rect x="592" y="20" width="170" height="78" fill="{C["fill"]}" stroke="{C["group"]}" stroke-width="1.5"/>
      <text x="677" y="40" text-anchor="middle" font-weight="600" fill="{C["group"]}">mod-pump</text>
      <text x="677" y="58" text-anchor="middle" font-size="10">feed terminal · R1 5 mΩ shunt</text>
      <text x="677" y="74" text-anchor="middle" font-size="10">Q2 low-side switch · D1 flyback</text>
      <text x="677" y="90" text-anchor="middle" font-size="10">J3 PUMP_P / PUMP_N</text>
      <line x1="762" y1="60" x2="870" y2="60" stroke="{C["body"]}" stroke-width="3" marker-end="url(#ah)"/>
      <text x="816" y="52" text-anchor="middle" font-size="10">J3</text>
      <rect x="872" y="30" width="118" height="56" fill="{C["fill"]}" stroke="{C["body"]}" stroke-width="1.5"/>
      <text x="931" y="52" text-anchor="middle" font-weight="600">FL-35 pump</text>
      <text x="931" y="70" text-anchor="middle" font-size="10">48 W @ 12 V</text>
    </g>
  </svg>
  <div style="font-family:{FONT};font-size:10.5px;color:{C["muted"]}">Panel loop (~3 A): panel + → J8.1 → R27 shunt → J8.2 → Voyager PV+ · panel − → J9.1 ↔ J9.2 → Voyager PV− (PANEL_RTN, never board GND). Everything else on this canvas travels by net name over J100/J101 jumper bundles.</div>
</div>'''

main = HEAD + f'''
<div style="width:1560px;box-sizing:border-box;padding:24px 28px;display:flex;flex-direction:column;gap:18px;background:#ffffff">
  <div style="display:flex;justify-content:space-between;align-items:baseline;border-bottom:2px solid {C["body"]};padding-bottom:8px">
    <div style="font-family:{FONT};font-size:22px;font-weight:600;color:{C["body"]}">WateringSystem rev2 — stage A module interconnect</div>
    <div style="font-family:{MONO};font-size:11px;color:{C["muted"]}">derived from HOWTO-module-pcb.md §4 · §6 · §7 — regenerate, do not hand-edit nets</div>
  </div>
  <div style="display:flex;gap:18px;align-items:center;font-family:{FONT};font-size:11px;color:{C["net"]}">
    <span style="display:inline-flex;align-items:center;gap:6px"><span style="width:10px;height:10px;border:1.5px solid {C["body"]};background:{C["fill"]};display:inline-block"></span> header pin (populated)</span>
    <span style="display:inline-flex;align-items:center;gap:6px"><span style="width:10px;height:10px;border:1.5px solid {C["body"]};background:#eee;display:inline-block"></span> position left n/c on this module</span>
    <span style="display:inline-flex;align-items:center;gap:6px"><span style="width:16px;border-top:1.5px solid {C["wire"]};display:inline-block"></span> net — same name = same wire in the bundle</span>
    <span style="font-family:{MONO};color:{C["body"]}">J100</span><span>= rail header, identical 1×6 pinout on every module</span>
    <span style="font-family:{MONO};color:{C["body"]}">J101</span><span>= signal header, §0.5 contract order, GND both ends</span>
  </div>
  <div style="display:grid;grid-template-columns:repeat(3, minmax(0, 1fr));gap:18px">{cards}</div>
  {current_paths}
</div>''' + TAIL
open(os.path.join(OUT, "Main.dc.html"), "w").write(main)

# ---------------- NetMatrix: net × module → header pin -----------------------
nets = ["VBAT","+3V3","GND","SENS_12V","SENS_GATE","VBAT_SENSE","PWR_PG","SENS_PWR_EN","RS485_TX","RS485_RX","PUMP_EN","RESERVOIR_LOW_LEVEL","RESERVOIR_HIGH_LEVEL","I2C_SDA","I2C_SCL"]
def cell(net, m):
    name, _, used, j101, _, _ = m
    hits = []
    if net in RAIL:
        hits += [f"J100.{i}" for i, r in enumerate(RAIL, 1) if r == net and i in used]
    hits += [f"J101.{i}" for i, r in enumerate(j101, 1) if r == net]
    if net == "VBAT" and name == "mod-pump": hits = ["screw term."]
    return " · ".join(hits)
rows = []
for net in nets:
    cells = [cell(net, m) for m in MODULES]
    src = "rail" if net in RAIL else "signal"
    tds = "".join(f'<td style="padding:5px 8px;border:1px solid #ddd;font-family:{MONO};font-size:11px;color:{C["net"] if c else C["muted"]};text-align:center">{html.escape(c) if c else "—"}</td>' for c in cells)
    rows.append(f'<tr><td style="padding:5px 8px;border:1px solid #ddd;font-family:{MONO};font-size:11px;font-weight:600;color:{C["hier"] if src=="signal" else C["body"]}">{net}</td>{tds}</tr>')
ths = "".join(f'<th style="padding:6px 8px;border:1px solid #ddd;font-family:{FONT};font-size:12px;color:{C["group"]};background:{C["paper"]}">{m[0]}</th>' for m in MODULES)
matrix = HEAD + f'''
<div style="width:1120px;box-sizing:border-box;padding:24px 28px;display:flex;flex-direction:column;gap:14px;background:#ffffff">
  <div style="border-bottom:2px solid {C["body"]};padding-bottom:8px;display:flex;justify-content:space-between;align-items:baseline">
    <div style="font-family:{FONT};font-size:20px;font-weight:600;color:{C["body"]}">Bench wiring matrix — net → header pin per module</div>
    <div style="font-family:{MONO};font-size:11px;color:{C["muted"]}">one row = one jumper bundle wire</div>
  </div>
  <table style="border-collapse:collapse;width:100%"><thead><tr><th style="padding:6px 8px;border:1px solid #ddd;font-family:{FONT};font-size:12px;text-align:left;color:{C["body"]};background:{C["paper"]}">net</th>{ths}</tr></thead><tbody>{"".join(rows)}</tbody></table>
  <div style="font-family:{FONT};font-size:11px;color:{C["muted"]}">Rails in <span style="color:{C["body"]};font-family:{MONO}">dark red</span>, §0.5 signals in <span style="color:{C["hier"]};font-family:{MONO}">olive</span> (hierarchical-label colour). GND appears on every J100 even position and at both ends of every J101 — use any of them as the bundle return. mod-pump takes VBAT on its screw terminal only.</div>
</div>''' + TAIL
open(os.path.join(OUT, "NetMatrix.dc.html"), "w").write(matrix)

open(os.path.join(OUT, "canvas.json"), "w").write('''{
  "artboards": [
    { "file": "Main.dc.html",      "x": 0,    "y": 0,   "w": 1560, "h": 1020, "title": "Module interconnect" },
    { "file": "NetMatrix.dc.html", "x": 1660, "y": 0,   "w": 1120, "h": 760,  "title": "Bench wiring matrix" }
  ],
  "launch": { "view": "canvas" }
}
''')


# ---------------- standalone browser preview (no canvas editor needed) -------
def strip_dc(doc):
    inner = doc.split("<x-dc>", 1)[1].split("</x-dc>", 1)[0]
    inner = inner.replace("<helmet>", "").replace("</helmet>", "")
    return inner
preview = ("<!doctype html><html><head><meta charset=\"utf-8\"><title>rev2 module interconnect (generated)</title></head>"
           "<body style=\"margin:0;background:#e9e9e6;padding:24px;display:flex;flex-direction:column;gap:32px;align-items:flex-start\">"
           + strip_dc(main) + strip_dc(matrix) + "</body></html>")
open(os.path.join(OUT, "module-interconnect.html"), "w").write(preview)
print(f"ok: {len(MODULES)} modules cross-checked against HOWTO §6 -> {os.path.relpath(OUT)}/module-interconnect.html (+ 2 artboards, canvas.json)")
