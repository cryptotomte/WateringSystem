# Contract: board profile macro surface (feature 012)

The board profile's consumers are other firmware components; the contract is
the macro surface below, pinned by
`test_apps/host/main/test_board_contract_rev{1,2}.cpp`.

## Guaranteed by this feature

### Both boards
- `BOARD_HAS_BTN_MANUAL`, `BOARD_HAS_BTN_CONFIG`, `BOARD_HAS_VBAT_SENSE`,
  `BOARD_HAS_PWR_PG`, `BOARD_HAS_SENS_PWR_EN` are **always defined** (0 or 1).
- For every flag: value 1 ⟺ corresponding `BOARD_PIN_*` defined.
  Unguarded reference on a flag-0 board = compile error.

### rev1 (`CONFIG_BOARD_REV1_DEVKIT`)
- `BOARD_PIN_BTN_MANUAL == 5`, `BOARD_PIN_BTN_CONFIG == 18` (unchanged).
- New signal flags all 0; their pins undefined.
- Every pre-existing macro value unchanged (behavioral no-op).

### rev2 (`CONFIG_BOARD_REV2`)
- Button flags 0; button pins undefined.
- `BOARD_PIN_VBAT_SENSE == 34`, `BOARD_PIN_PWR_PG == 35`,
  `BOARD_PIN_SENS_PWR_EN == 25`; flags 1.
- No defined `BOARD_PIN_*` in {18, 19, 23, 4, 27} (expansion reservation).
- Zero `TODO(SYNC1)` markers; pin groups cite `02-mcu.md §2.2` frozen map.

## Explicitly NOT in this contract (PR-14 scope)
- Drivers/consumers for VBAT_SENSE (ADC calibration, FW-1), PWR_PG
  monitoring, SENS_PWR_EN sequencing (FW-3 settle), solar INA226 (0x41).
- Any physical re-provisioning trigger on rev2 (BOOT-button reuse idea).
