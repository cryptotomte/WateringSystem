# PR-02: pump-gpio-board

> Phase 1 — drivers

## Goal

Port the pump actuator layer (`IActuator`/`IWaterPump`) to native ESP-IDF GPIO and
flesh out the `board` component into the single source of truth for pin mappings and
board feature flags.

## Scope

- `board` component completed for both targets:
  - **REV1_DEVKIT** pin table (source of truth: `docs/parity-checklist.md`, extracted
    from the running Arduino firmware `src/main.cpp` — note `docs/hardware.md` lists
    RS485 TX/RX swapped, see checklist QUIRK 6): I2C SDA 21 / SCL 22, RS485 TX 16 /
    RX 17 / DE 25, plant pump 26, reservoir pump 27, reservoir low level 32 / high
    level 33, status LED 2, buttons 5 / 18.
  - **REV2** provisional pin table + feature flags: `BOARD_HAS_RS485_DE` (rev1 only,
    rev2 THVD1426 is auto-direction), `BOARD_HAS_INA226` (rev2 only),
    `BOARD_LEVEL_ACTIVE_LOW` (rev2 only, 2N7002 inverter). Final rev2 pin
    numbers land at **SYNC 1**.
- `IActuator` / `IWaterPump` interfaces ported to pure C++ (no Arduino types,
  `std::string` instead of `String`), placed so host tests can include them without
  IDF hardware headers.
- `GpioWaterPump` implementation on `driver/gpio`:
  - MOSFET gate control (IRLZ44N, active HIGH), explicit OFF at construction.
  - Timed run support (`runFor(seconds)`) and max-runtime enforcement: reservoir pump
    capped at 300 s (parity with Arduino `RESERVOIR_PUMP_MAX_RUNTIME = 300000 ms`);
    manual plant pump runs capped at 300 s — **DELIBERATE behavior change, not
    parity**: the Arduino firmware runs manual/indefinite starts (e.g. legacy
    `/control` `start` with duration 0) uncapped (see `docs/parity-checklist.md` §4).
  - Runtime tracking (start time, accumulated run time) for status reporting.
- Two pump instances wired in `app_main` behind the interfaces; simple serial
  diagnostic to toggle pumps for rig testing. *(Decision update 2026-06-10: rev2
  is a single-pump node — `BOARD_HAS_RESERVOIR_PUMP` lands in PR-05 and makes the
  reservoir instance conditional; the two-pump wiring built here remains correct
  for rev1.)*
- Mock `IWaterPump` for host tests (foundation for PR-11).

## Out of scope

- Watering decision logic (PR-11), level-sensor-driven reservoir state machine (PR-11),
  INA226 current sensing (PR-05), web pump control (PR-09).

## Functional requirements covered

- FR4 (partially: GPIO/MOSFET control, max runtimes, off-at-boot; fail-safe stop on
  invalid sensor data in auto mode completes in PR-11).

## Dependencies

- PR-01 (skeleton, Kconfig board choice).
- SYNC 1 gates only the *final* rev2 pin numbers; PR-02 merges with the provisional
  table and a `TODO(SYNC1)` marker — does not block on the hardware track.

## Acceptance criteria

- [CI] Both board targets build; rev1/rev2 select different pin tables and feature
  flags verified by a compile-time check or host test.
- [CI] Host test: max-runtime enforcement and runtime tracking on mock time source.
- [HIL] Pumps are OFF at boot and after reset (scope/meter on GPIO 26/27 during boot).
- [HIL] Each pump can be started/stopped via serial diagnostic; timed run stops by
  itself at the configured limit.

## Estimated size

M
