# Data Model: board profile capability matrix

**Feature**: 012-rev2-pin-map · the "data" is compile-time facts.

## Capability flags and pins per board (target state)

| Capability flag | rev1 | rev1 pin | rev2 | rev2 pin | Change |
|---|---|---|---|---|---|
| `BOARD_HAS_RS485_DE` | 1 | 25 | 0 | — | none (existing) |
| `BOARD_HAS_RESERVOIR_PUMP` | 1 | 27 | 0 | — | none (existing) |
| `BOARD_HAS_INA226` | 0 | — | 1 | I2C 0x40 | none (existing) |
| `BOARD_HAS_BTN_MANUAL` | **1** | 5 | **0** | — | **new flag; rev2 pin removed** |
| `BOARD_HAS_BTN_CONFIG` | **1** | 18 | **0** | — | **new flag; rev2 pin removed** |
| `BOARD_HAS_VBAT_SENSE` | **0** | — | **1** | 34 (ADC1, input-only) | **new** |
| `BOARD_HAS_PWR_PG` | **0** | — | **1** | 35 (input-only, ext. pull-up) | **new** |
| `BOARD_HAS_SENS_PWR_EN` | **0** | — | **1** | 25 (output, rail OFF default) | **new** |

Unconditional pins (both boards, unchanged): I2C 21/22 · RS485 TX/RX 16/17 ·
MAIN_PUMP 26 · LEVEL_LOW/HIGH 32/33 · STATUS_LED 2. rev2-only constants
(unchanged): `BOARD_LEVEL_ACTIVE_LOW 1`, debounce 300 / settle 500 ms,
`BOARD_RS485_UART_PORT 2`.

## Invariants (compile-time enforced)

1. Flag = 1 ⟺ pin macro defined; flag = 0 ⟺ pin macro undefined
   (per flag, both boards — sanity `#error` pair each).
2. rev2: no defined `BOARD_PIN_*` ∈ expansion set {18, 19, 23, 4, 27}.
3. Note: rev1 legitimately violates (2) — BTN_CONFIG=18, RESERVOIR_PUMP=27 —
   the expansion reservation is a **rev2-only** invariant; the check must be
   inside the rev2 conditional.
4. All pin-distinctness checks already in the sanity section remain and must
   still pass with the new definitions (25 appears on rev1 as RS485_DE and on
   rev2 as SENS_PWR_EN — never both in one target).
