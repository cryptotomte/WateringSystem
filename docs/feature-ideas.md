# Feature Ideas

> Concepts under evaluation for future development. Items here are NOT scoped
> work — decided items graduate into a PRD (see `docs/prd/`).

## Production PCB — superseded

The original idea (replace devkit + RS485 Click + TXS0108E breakout with a custom
board) became the **rev2 hardware track**: see
`hardware/bom/WateringSystem-rev2-BOM.md` and the ESP-IDF migration PRD
(`docs/PRD-esp-idf-migration.md`, track B). Historical known issues (TX/RX label
crossing, TXS0108E OE strap, DE/RE bridging) are resolved or designed out in rev2
(THVD1426 auto-direction needs no DE at all). Kept here only as a pointer.

## Multi-zone watering network

Scale from a single watering site (the greenhouse) to several zones on the
property. Refined 2026-06-10 (Paul); originally sketched 2026-04-12.

### Topology (current thinking)

```
[Cistern] ──► [Reservoir unit]                    (ONE central pump)
                ├─ MCU (ESP32) + pump control
                ├─ Solenoid valve per zone (MCU-driven)
                └─ Routes water: pump ON + open valve N → fills zone N's reservoir

[Watering node 1..N]   (= rev2 hardware)
                ├─ Local reservoir + two level sensors (low/high)
                ├─ ONE watering pump channel (from local reservoir to plants)
                ├─ Soil sensor (Modbus RS485), BME280
                ├─ Autonomous watering — keeps working if comms are down
                └─ Reports status; requests refill when reservoir low

[Gateway role]  — collects data, exposes dashboard/API; may live in the
                  reservoir unit or a node; undecided.
```

**Key decisions (2026-06-10):**

- **One central pump, many solenoids.** There is a single cistern to draw from,
  so one pump + MCU-driven solenoid routing replaces per-zone refill pumps.
  Simpler hydraulics, fewer high-current channels, fits the available space.
- **The watering node has exactly ONE pump channel.** Reservoir refill is the
  reservoir unit's job. Consequence for rev2 (the node hardware, design starting
  now): single pump driver stage (one MOSFET/shunt/INA226/terminal) — see the
  rev2 BOM changelog. Level sensors stay on the node (refill requests + local
  fail-safes).
- **Interim consequence (decided):** until the reservoir unit exists, the
  greenhouse rev2 node's reservoir is refilled manually. The firmware keeps the
  reservoir *control logic* behind a board capability flag so the rev1 bench rig
  can still exercise it (parity testing), but rev2 hardware does not expose a
  second pump channel.

### Open questions (for a future PRD)

- Comms: ESP-NOW vs WiFi/MQTT vs LoRa. April assessment still stands —
  ESP-NOW likely sufficient for one property (<200 m, zero extra hardware,
  250-peer limit irrelevant); LoRa only if distance/terrain demands it.
  MQTT enters the picture if the gateway should integrate with home automation
  (listed as a post-migration candidate in the master PRD).
- Refill protocol: request/grant semantics, what happens when several nodes are
  low at once (queueing), and fail-safes (valve stuck open, pump dry-run —
  ties into the INA226 protection PRD).
- Reservoir unit hardware: own design; possibly a rev2 derivative with the pump
  channel + N solenoid drivers instead of sensors.
- Gateway placement and dashboard.

| | LoRa | ESP-NOW |
|---|---|---|
| **Range** | 1–10 km (open field) | ~200 m (open field) |
| **Data rate** | Low (0.3–50 kbps) | High (1 Mbps) |
| **Latency** | High (seconds) | Low (ms) |
| **Extra hardware** | Yes (LoRa module) | No (built into ESP32) |
| **Power draw** | Very low | Low |
| **Max peers** | Practically unlimited | 250 |
| **Cost per node** | +50–100 SEK | 0 SEK |

---

*Created 2026-04-12 (Swedish original) · rewritten in English and refined
2026-06-10 · Status: multi-zone = concept (future PRD); the single-pump-node
decision is FINAL and reflected in the rev2 BOM and the migration PRD.*
