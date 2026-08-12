# rev2 hardware → firmware handoff notes

Hardware-driven requirements on the ESP-IDF firmware that are **not** visible in the
schematic or the parity checklist. Discovered during rev2 design work; each item
references its hardware source. Consume these in the board-support / driver PRs
(the rev2 Kconfig board profile).

| # | Item | GPIO / net | Source |
|---|---|---|---|
| FW-1 | **`VBAT_SENSE` ADC nonlinearity above ~2.45 V.** Divider 470 k/100 k puts 14.6 V (LiFePO4 max charge) at 2.56 V — slightly above the ESP32 ADC1 11 dB-attenuation linear range (~2.45 V). Readings near top-of-charge compress. Use esp_adc calibration + accept reduced accuracy above ~14 V, or add a firmware curve correction. Telemetry + soft-UVLO only — no safety impact. | IO34 (ADC1_CH6) | `01-power.md` §1.3; review 2026-07-02 A4 |
| FW-2 | **Enable IO17 internal pull-up whenever the sensor domain is off.** THVD1426 SHDN̅ is tied to `SENS_PWR_EN`; with the domain off, RO goes hi-Z and `RS485_RX` floats → garbage bytes/noise on UART2. Enable the internal pull-up on IO17 (permanently is fine — RO drives through it). | IO17 (`RS485_RX`) | `04-rs485.md` §4.2; review 2026-07-02 A5 |
| FW-3 | **Level reads valid only ≥ 500 ms after `SENS_PWR_EN` assert.** XKC-Y26 response time; an early sample reads "water absent". Keep the rail on for the duration of a watering run (mid-run depletion detection). | IO25 → IO32/IO33 | `06-level-sensors.md` §6.2/§6.4-4 (review D5) |
| FW-4 | **UART2 hears its own TX (echo).** THVD1426 RE̅ is grounded (receiver always on, auto-direction) — every transmitted Modbus frame comes back on RX. Discard the echo before parsing the reply. | UART2 (IO16/IO17) | `04-rs485.md` §4.2-3 |
| FW-5 | **Level-sensor polarity on rev2 = active-LOW** (2N7002 inverter: water present → GPIO LOW). Opposite of the raw XKC polarity; board-configured via the Kconfig board profile (CLAUDE.md FR5). | IO32/IO33 | `06-level-sensors.md` §6.3 |
| FW-6 | **`PWR_PG` is open-drain, externally pulled up, on an input-only pin** — configure IO35 as plain input (no internal pull available on IO34–39). HIGH = 3V3 in regulation. | IO35 | `01-power.md` §1.2 |

Full second-opinion review: `docs/checkpoints/rev2-design-review-fable-2026-07-02.md`.
