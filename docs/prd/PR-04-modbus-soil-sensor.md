# PR-04: modbus-soil-sensor

> Phase 1 — drivers

## Goal

Replace the hand-rolled SP3485 Modbus client with `esp-modbus` (pinned 2.1.2) behind
`IModbusClient`/`ISoilSensor`, supporting both rev1 (manual DE) and rev2 (DE-less
auto-direction) RS485 hardware.

## Scope

- `IModbusClient` / `ISoilSensor` interfaces ported to pure C++, host-includable.
- `EspModbusClient` on `espressif/esp-modbus==2.1.2` master RTU:
  - UART per board component (rev1: TX 16, RX 17, DE/RE on GPIO 25 via TXS0108E),
    9600 baud 8N1. Pin source of truth: `docs/parity-checklist.md` (extracted from
    `src/main.cpp`; `docs/hardware.md` lists TX/RX swapped — checklist QUIRK 6).
  - DE handling switched by `BOARD_HAS_RS485_DE`: rev1 uses RTS-pin direction control
    (esp-modbus drives GPIO 25; verify the 50 µs TXS0108E switching margin still
    holds), rev2 (THVD1426 auto-direction via D pin) configures **no** DE pin.
  - Timeout: 3000 ms response timeout (parity). **No retry exists in the current
    client** — exactly one attempt per call; recovery comes from the 5 s read cadence
    (`docs/parity-checklist.md` §5). If the new driver adds retries, that is a
    deliberate improvement and must be marked as such, not as parity.
- `ModbusSoilSensor` reading slave address 0x01, function 0x03, one read of
  **9 holding registers 0x0000–0x0008** (map per `docs/parity-checklist.md` §5 /
  `include/sensors/ModbusSoilSensor.h:59-68`, "corrected according to NPK manual
  PDF"):

  | Reg | Value | Unit / format / scaling |
  |---|---|---|
  | 0x0000 | Soil moisture (≡ humidity for this sensor) | 0.1 %, u16 (÷10 → %) |
  | 0x0001 | Soil temperature | 0.1 °C, **s16** (÷10 → °C) |
  | 0x0002 | EC / conductivity | µS/cm, u16 (×1, × calibration factor) |
  | 0x0003 | pH | 0.1 pH, u16 (÷10, × calibration factor) |
  | 0x0004–0x0006 | N / P / K | mg/kg, u16 (×1) |
  | 0x0007 | Salinity | read but unused |
  | 0x0008 | TDS | read but unused |

- Range validation (e.g. pH 3–9, moisture 0–100 %) feeding the validity flag that the
  fail-safe logic (PR-11) consumes.
- Serial diagnostics equivalent to today's `rs485test` / `soil` commands.
- Mock `ISoilSensor` for host tests.

## Out of scope

- Watering decisions on the data (PR-11), API exposure (PR-09), THVD1426-specific
  validation at 9600 baud on real rev2 hardware (PR-14).

## Functional requirements covered

- FR2 (Modbus RTU via esp-modbus); FR12 (partially: RS485/soil serial diagnostics).

## Dependencies

- PR-02 (board component: UART pins, `BOARD_HAS_RS485_DE` flag).

## Acceptance criteria

- [CI] Both board targets build — rev1 with DE pin configured, rev2 without.
- [CI] Host test: register decode (incl. signed temperature), range validation,
  invalid-on-timeout behavior against a mock Modbus client.
- [HIL] Rig reads all 9 registers from the real soil sensor at 9600 8N1; values match
  the Arduino unit's readings.
- [HIL] Disconnecting the RS485 A/B pair yields invalid soil data + logged errors, no
  crash; reconnect recovers automatically on a subsequent read cycle.
- [HIL] `rs485test`/`soil` serial diagnostics produce useful output on the rig.

## Estimated size

M
