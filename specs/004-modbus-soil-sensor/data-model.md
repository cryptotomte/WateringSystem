# Data Model: Modbus Soil Sensor over RS485 (004)

**Date**: 2026-07-02 | **Spec**: [spec.md](spec.md) | **Research**: [research.md](research.md)

## SoilReading (value object)

Produced atomically from one 9-register transaction; either fully valid or an error.

| Field | Type | Source | Validation (read-fails-if) |
|---|---|---|---|
| moisture | float % | reg 0x0000 ÷10 (moisture factor NOT applied on read — legacy parity; `calibrateMoisture` stores/writes the factor but the read path never uses it) | outside 0–100 |
| temperature | float °C | reg 0x0001 as **int16_t** ÷10 | outside −40–80 |
| ec | float µS/cm | reg 0x0002 ×1, × EC factor | not enforced (parity) |
| ph | float | reg 0x0003 ÷10, × pH factor | outside 3–9 |
| nitrogen | float mg/kg | reg 0x0004 ×1 | not enforced (parity) |
| phosphorus | float mg/kg | reg 0x0005 ×1 | not enforced (parity) |
| potassium | float mg/kg | reg 0x0006 ×1 | not enforced (parity) |
| (salinity) | — | reg 0x0007 read, not exposed | — |
| (tds) | — | reg 0x0008 read, not exposed | — |

Humidity ≡ moisture for this sensor (`getHumidity()` returns moisture, parity).

**Invariant**: a failed read (bus error, timeout, exception, out-of-range) leaves
the last-good values untouched but flags the sensor invalid; getters after a failed
read must not be presented as fresh (validity flag + error code carry the truth —
consumers in PR-11 gate on validity, never on value plausibility).

## Error codes (ISoilSensor / IModbusClient contract)

| Code | Meaning | Parity anchor |
|---|---|---|
| 0 | OK | — |
| 1 | Not initialized | legacy |
| 2 | Bus/communication error (CRC, framing, truncated) | legacy |
| 3 | Timeout (no response within 3000 ms) | legacy |
| 5 | Range validation failed | legacy error 5 |
| 100+n | Modbus slave exception n (when surfaced by esp-modbus; else single generic exception code, documented divergence) | legacy 100+n |

## CalibrationFactor (per quantity: moisture, pH, EC)

| Field | Type | Notes |
|---|---|---|
| factor | float, default 1.0 | `reference / rawReading` at calibration time |
| sensor register | 0x0100 / 0x0101 / 0x0102 | best-effort write ×100 via fn 0x06; write failure non-fatal |

Lifecycle: set by `calibrate*(reference)`; applied on every subsequent read; RAM
only in this PR (persistence → PR-09/PR-11).

## TransactionStatistics

`successCount`, `errorCount` (uint32), one increment per `IModbusClient` call
outcome; exposed via `getStatistics()` and the `rs485test` console dump.

## RS485 board profile (existing, consumed not defined)

From `firmware/components/board/board.h`: `BOARD_PIN_RS485_TX/RX`,
`BOARD_HAS_RS485_DE` (+ `BOARD_PIN_RS485_DE` iff 1). UART port number is a
`sensors`-component Kconfig/board fact (UART2, parity).

## State transitions (sensor availability)

```
UNINITIALIZED --initialize() ok--> READY
READY --read ok--> READY (valid data, successCount++)
READY --read fail--> READY (invalid flag + error code, errorCount++)   [no retry]
any --isAvailable()--> performs real 1-register bus read (parity), no cached state
```

No permanent failure state: recovery is implicit in the next read (US2).
