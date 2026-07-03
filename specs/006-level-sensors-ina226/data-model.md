# Data Model: Level Sensors, Capability Flag and INA226

## Level reading (per sensor: low mark GPIO 32, high mark GPIO 33)

| Field | Type | Meaning |
|-------|------|---------|
| logical water-present | bool | polarity-mapped, debounced state (board owns polarity: rev1 active HIGH, rev2 active LOW — FW-5) |
| raw input | bool | current pin level, undebounced (diagnostics only) |
| valid | bool | false during debounce warm-up (before first stable window) and settle gating (FW-3) |

State machine (`DebouncedLevelSensor`):

```
POWER_ON/CONSTRUCT ──▶ SETTLING (until settle_ms elapsed; rev1: 0)
SETTLING ──▶ WARMUP (raw sampled; valid=false until raw stable for debounce_ms)
WARMUP ──▶ TRACKING (valid=true; state = stable raw ⊕ polarity)
TRACKING: raw flip starts a stability window; reported state changes only after
          debounce_ms of stability; every flip restarts the window.
notifyPowerOn() from any state ──▶ SETTLING (readings invalid again).
```

Time from injected `ITimeProvider` (host-testable with `FakeTimeProvider`).

## Board capability set (board.h)

| Macro | rev1 | rev2 | Notes |
|-------|------|------|-------|
| `BOARD_HAS_RESERVOIR_PUMP` | 1 | 0 | NEW — single-pump decision (master PRD FR4, final 2026-06-10); flag 0 ⇒ `BOARD_PIN_RESERVOIR_PUMP` deliberately undefined (compile-error enforcement, RS485-DE pattern) |
| `BOARD_PIN_LEVEL_LOW` / `BOARD_PIN_LEVEL_HIGH` | 32 / 33 | 32 / 33 | parity checklist line 95 (rev1); rev2 provisional until SYNC1 |
| `BOARD_LEVEL_ACTIVE_LOW` | 0 | 1 | FW-5 (2N7002 inverter) |
| `BOARD_LEVEL_DEBOUNCE_MS` | 300 | 300 | deliberate divergence (legacy: none) |
| `BOARD_LEVEL_SETTLE_MS` | 0 | 500 | FW-3 (XKC-Y26 response after rail power-on) |
| `BOARD_HAS_INA226` | 0 | 1 | existing |
| `BOARD_INA226_ADDR` | — | 0x40 | pump monitor, A0=A1=GND; 0x41 reserved (DNP solar), 0x76/0x77 BME280 — address map comment |

Sanity checks: flag/pin consistency asserts (reservoir pump, level pins distinct
from each other and from I2C/RS485 pins), `#if BOARD_HAS_RESERVOIR_PUMP` guards on
existing reservoir-pin asserts.

## Power reading (INA226, rev2)

| Field | Type | Derivation |
|-------|------|-----------|
| bus voltage | float V | reg 0x02 (u16) × 1.25 mV |
| current | float A | reg 0x04 (s16) × Current_LSB (0.5 mA) — signed, never wrapped |
| power | float W | reg 0x03 (u16) × 25 × Current_LSB |

Validity: established contract — gate on read(), NaN placeholders before first
success, last-good after failure; errors 0 = OK, 1 = not found (no ACK at 0x40 or
identity mismatch), 2 = read/communication failure after identification.

## INA226 register map (used subset — 16-bit big-endian; verify against TI SBOS547 at implementation)

| Reg | Name | Use |
|-----|------|-----|
| 0x00 | Configuration | RST; AVG; VBUSCT; VSHCT; MODE=0b111 (continuous shunt+bus). POR default 0x4127 |
| 0x02 | Bus Voltage | LSB 1.25 mV |
| 0x03 | Power | LSB 25 × Current_LSB |
| 0x04 | Current | signed, LSB = Current_LSB |
| 0x05 | Calibration | CAL = 0.00512 / (Current_LSB × R_shunt) ⇒ 2048 @ 0.5 mA, 5 mΩ |
| 0xFE | Manufacturer ID | must read 0x5449 ("TI") |
| 0xFF | Die ID | must read 0x2260 |

Registers 0x01 (shunt voltage), 0x06/0x07 (Mask/Enable, Alert) unused — ALERT pin
NC on rev2 (design notes §5.2.2).

Kconfig: `CONFIG_WS_INA226_SHUNT_MILLIOHM` int default 5 (rev2 BOM R1 = 5 mΩ 2512).

## I2C seam extension (from PR-03's II2cBus)

- `writeRegister16(addr7, reg, uint16_t)` — NEW virtual: pointer + 2 data bytes
  big-endian in ONE transaction (INA226 config/calibration writes; not composable
  from single-byte writes).
- `readRegister16(addr7, reg, uint16_t&)` — non-virtual convenience over
  `readRegisters(addr, reg, buf, 2)` with big-endian decode.
- `EspI2cBus`: implements the virtual via a 3-byte `i2c_master_transmit`;
  `MockI2cBus`: 16-bit writes recorded into the same per-address byte map
  (big-endian) so byte-level assertions stay valid.

## Concurrency

Level sensors and the power sensor are unsynchronized by design; cross-task access
through `LockedLevelSensor`/`LockedPowerSensor` decorators (console REPL + main
loop now; PR-09/PR-11 later). Level `update()` is called from the main loop at
10 Hz; INA226 reads are on-demand (console; PR-09 API later) — bus transactions
serialize at the i2c_master driver layer (PR-03 research R3) plus EspI2cBus's
mutex-guarded handle table.
