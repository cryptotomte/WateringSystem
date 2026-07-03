# CLAUDE.md — firmware/ (ESP-IDF)

Agent context for the ESP-IDF firmware. This directory is independent of the
frozen Arduino/PlatformIO code in the repository root — never mix the two.
Everything here (code, comments, commits, docs) is in **English** (public repo).

## Build commands

ESP-IDF is pinned to **v6.0.1**, chip target **esp32**. Build in docker — no
local toolchain is assumed. Run from `firmware/`:

```bash
# Default board (rev1_devkit)
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 idf.py build

# Explicit board selection
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.rev1_devkit" build
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.rev2" build
```

When switching boards, run `idf.py fullclean` (or delete `sdkconfig`) first —
overlays are only applied when `sdkconfig` is regenerated. CI
(`.github/workflows/firmware-build.yml`) builds both boards; the push trigger
is main-only, feature branches build via PR or manual dispatch. Both boards
must stay green.

### Host tests (linux preview target)

Logic is unit-tested natively, no ESP32 needed: pump enforcement, config
store, data storage, the soil sensor decode/validation/calibration
(`test_soil_sensor.cpp`, real `ModbusSoilSensor` over `MockModbusClient`),
the BME280 probe/calibration/compensation logic (`test_bme280.cpp`,
real `Bme280Sensor` over `MockI2cBus` — Bosch reference vectors, error
paths, address variants, sensor-task log policy, `MockEnvironmentalSensor`
consistency), the level-sensor state machine (`test_level_sensor.cpp`,
real `DebouncedLevelSensor` over a scripted input + `FakeTimeProvider` —
debounce/settle/polarity, per-board fail-direction truths,
`MockLevelSensor` coherence) and the INA226 driver (`test_ina226.cpp`,
real `Ina226Sensor` over `MockI2cBus` — datasheet scaling vectors,
identity/absent/recovery paths, 16-bit bus extension). Two
`test_board_contract_rev*.cpp` TUs compile the REAL `board.h` under each
board selector and static_assert the capability contract (passing =
compiling). The test executable's exit code equals the Unity failure
count (CI gate, job `host-test`):

```bash
cd firmware/test_apps/host
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 bash -c \
  "idf.py --preview set-target linux && idf.py build && ./build/pump_host_tests.elf"
```

## Directory structure

```
firmware/
├── CMakeLists.txt              # Top-level project (version from version.txt)
├── version.txt                 # Project version (3.0.0-dev)
├── partitions.csv              # Custom partition table (see below)
├── sdkconfig.defaults          # Base config (board-independent)
├── sdkconfig.board.rev1_devkit # Board overlay: CONFIG_BOARD_REV1_DEVKIT=y
├── sdkconfig.board.rev2        # Board overlay: CONFIG_BOARD_REV2=y
├── Dockerfile                  # Pins espressif/idf:v6.0.1
├── main/
│   ├── app_main.cpp            # Entry point — pumps forced OFF first, always
│   ├── diag_console.cpp/.h     # esp_console UART REPL (prompt "ws>")
│   ├── sensor_task.cpp/.h      # 5 s environmental poll task (feature 005)
│   ├── Kconfig.projbuild       # Board revision choice + WS_INA226_SHUNT_MILLIOHM
│   └── idf_component.yml       # Pinned managed deps (esp-modbus, littlefs)
├── components/
│   ├── board/                  # Board abstraction (header-only)
│   │   └── include/board/board.h
│   ├── interfaces/             # Header-only, NO IDF deps (host-includable)
│   │   └── include/interfaces/ # IActuator, IWaterPump, ITimeProvider,
│   │                           # IConfigStore, IDataStorage,
│   │                           # IModbusClient, ISoilSensor,
│   │                           # IEnvironmentalSensor, II2cBus,
│   │                           # IDigitalInput, ILevelSensor, IPowerSensor
│   ├── actuators/              # Pump drivers
│   │   ├── include/actuators/  # WaterPump (pure C++ logic), GpioWaterPump,
│   │   │                       # EspTimeProvider (esp32-only header),
│   │   │                       # testing/ (MockWaterPump, FakeTimeProvider)
│   │   └── src/                # GpioWaterPump.cpp excluded on linux target
│   ├── sensors/                # Soil sensor (004) + BME280 (005) +
│   │   │                       # level sensors + INA226 (006)
│   │   ├── include/sensors/    # ModbusSoilSensor, Bme280Sensor,
│   │   │                       # DebouncedLevelSensor, Ina226Sensor (pure
│   │   │                       # C++ logic), EspModbusClient, EspI2cBus,
│   │   │                       # GpioLevelSensor, LockedSoilSensor,
│   │   │                       # LockedEnvironmentalSensor,
│   │   │                       # LockedLevelSensor, LockedPowerSensor,
│   │   │                       # SensorTaskLogPolicy, testing/
│   │   │                       # (MockModbusClient, MockSoilSensor,
│   │   │                       # MockI2cBus, MockEnvironmentalSensor,
│   │   │                       # MockLevelSensor)
│   │   └── src/                # EspModbusClient.cpp + esp-modbus dep,
│   │                           # EspI2cBus.cpp + esp_driver_i2c dep and
│   │                           # GpioLevelSensor.cpp excluded on linux
│   │                           # target; Ina226Sensor.cpp on linux always
│   │                           # + on target only when CONFIG_BOARD_REV2
│   └── storage/                # Config + data persistence (feature 003)
│       ├── include/storage/    # NvsConfigStore, LittleFsDataStorage (POSIX,
│       │                       # host-runnable), StorageMount (esp32-only),
│       │                       # LockedConfigStore/LockedDataStorage,
│       │                       # testing/ (MockConfigStore, MockDataStorage)
│       └── src/                # StorageMount.cpp + littlefs REQUIRES excluded
│                               # on linux target (esp_littlefs has no port)
└── test_apps/
    └── host/                   # Host test app (linux preview target, Unity):
                                # pump + config store + data storage +
                                # soil sensor (test_soil_sensor.cpp) +
                                # BME280 (test_bme280.cpp) +
                                # level sensors (test_level_sensor.cpp) +
                                # INA226 (test_ina226.cpp) suites +
                                # board-contract TUs (compile-time)
```

Future components (drivers, controllers, web server) are added as siblings
under `components/`, one concern per component.

## Pump actuator layer

All timing/safety logic (timed runs, hard 300 s max-runtime cap, runtime
statistics, stop reasons) lives in the pure C++ `WaterPump` base class,
driven by an injected `ITimeProvider` and a polled `update()` (main loop,
10 Hz). The only hardware touchpoint is `applyOutput(bool)`, implemented by
`GpioWaterPump` (active-HIGH MOSFET gate, OFF re-asserted glitch-free at
init). Host-tested code must never call `esp_timer` directly — it is not
simulated on the linux target; inject time instead. Concurrency: `WaterPump`
is unsynchronized by design — any pump accessed from more than one task
(e.g. main loop + console REPL) must be wrapped in `LockedWaterPump` and
accessed only through the wrapper.

## Serial diagnostic console

`main/diag_console.cpp` starts an `esp_console` UART REPL (prompt `ws>`,
same UART as logs, 115200 baud). Command grammar and exact response formats
are normative in `specs/002-pump-gpio-board/contracts/serial-diagnostic.md`:

```
pump <plant|reservoir> start <seconds>   # timed run; 1..300
pump <plant|reservoir> stop
pump <plant|reservoir> status
pump status                              # every existing pump
```

The pump set is capability-aware (feature 006): on
`BOARD_HAS_RESERVOIR_PUMP=0` boards (rev2, single-pump node) the
`reservoir` word is compiled out — `pump reservoir ...` is a usage error
and `pump status` reports exactly one pump (PR-14 contract).

Feature 003 adds `config` and `storage` subcommands (HIL verification path; the
handlers are thin interface calls, no logic). Credential values are never echoed
(FR-004):

```
config get | set <item> <value> | wifi <ssid> <password> | wifi-clear | factory-reset
storage stats | log <metric> <value> | query <metric> [t0 t1] | event <category> <detail> | events [n]
```

Feature 004 adds the soil sensor commands (HIL verification path, same
thin-wrapper rule; a failed calibration-register write is reported as
non-fatal — legacy parity):

```
soil                                     # one read(); 7 values or error code
rs485test                                # raw 1-register Modbus probe + statistics
soil_cal_moisture | soil_cal_ph | soil_cal_ec <reference-value>
```

Feature 005 adds the environmental sensor command (HIL verification path,
one locked read; the failure hint distinguishes error 1 "sensor not found"
from error 2 "read failed" — SC-006):

```
env                                      # one read(); T/RH/P with units or ERROR <code> (<hint>)
```

Feature 006 adds the level-status command (both boards; the output
distinguishes `not_yet_valid` from `water`/`dry` — a settling sensor never
reads as an empty or full reservoir) and the power-telemetry command
(`BOARD_HAS_INA226` builds only — compile-time absent on rev1; the PR-14
bring-up path, same error-1-vs-2 hint convention as `env`):

```
level                                    # both sensors: logical + raw pin state
power                                    # one read(); bus V / current A / power W or ERROR <code> (<hint>)
```

## Storage (config + data persistence)

Feature 003 (PR-06). Two redesigned, host-includable interfaces in
`components/interfaces/`:

- **`IConfigStore`** → `NvsConfigStore`: typed configuration in the `nvs`
  partition (namespace `wscfg`, one entry per item), compiled-in factory
  defaults applied on missing/erased/out-of-range entries (FR-013), explicit
  `factoryReset()` (erases the partition). NVS runs natively on the linux
  target, so the real store is host-tested — no mock skew.
- **`IDataStorage`** → `LittleFsDataStorage`: bounded sensor history
  (per-metric 8-byte-record chunk files, ring eviction, ≥30-day retention, max
  10 metrics), a rotating event log (two 16 KiB files, newest always retained),
  and filesystem usage stats. Implemented over **POSIX stdio with an injectable
  base path**, so it builds and runs on the linux host; only `StorageMount`
  (mount-or-format of the `storage` partition at `/storage`, `esp_littlefs_info`
  stats) is esp32-only and excluded from the linux build via the component
  CMakeLists `if(NOT ${IDF_TARGET} STREQUAL "linux")` guard.

Concurrency: both base implementations are unsynchronized; anything accessed
from more than one task (main loop + console REPL) is wrapped in
`LockedConfigStore`/`LockedDataStorage` and accessed only through the wrapper —
the same pattern as `LockedWaterPump`. A committed seed directory
(`firmware/storage_image/`) feeds `littlefs_create_partition_image()`, which
emits `build/storage.bin` on every build (CI verifies it exists). No Arduino
data is migrated; on-disk formats diverge from legacy by design — see
`docs/parity-checklist.md` §6 "Deliberate divergences".

The diagnostic console (`ws>`) exposes `config` and `storage` subcommands for
the HIL verification path (see below).

## Soil sensor (RS485 Modbus)

Feature 004 (PR-08). `components/sensors/` splits the driver at the
`IModbusClient` interface: `ModbusSoilSensor` is pure C++
(decode/scaling/validation/calibration, host-tested against
`MockModbusClient`), `EspModbusClient` is the only hardware touchpoint
(esp-modbus RTU master, UART RS485 half-duplex on both boards, RX pull-up
for the rev2 SHDN̅/hi-Z case) and is excluded from the linux build. The
**esp-modbus dependency is pinned `==2.1.2`** in the component's
`idf_component.yml` and rule-gated to `target != linux`, keeping it out of
the host-test dependency graph. Cross-task access goes through
`LockedSoilSensor` (console REPL now, main-loop reader in PR-11) — same
pattern as the other Locked* wrappers.

Two board revisions exist, selected via Kconfig (`main/Kconfig.projbuild`):

- `CONFIG_BOARD_REV1_DEVKIT` — ESP32 devkit + breakout (TXS0108E + SP3485,
  manual RS485 DE pin, level sensors active HIGH).
- `CONFIG_BOARD_REV2` — custom PCB (THVD1426 auto-direction RS485 — no DE
  pin, INA226 current monitors, level sensors active LOW via 2N7002 inverter).
  Rev2 pins are provisional until hardware sync 1 (`TODO(SYNC1)` markers).

All pins and polarity/feature flags come from `board/board.h`
(`BOARD_PIN_*`, `BOARD_HAS_RS485_DE`, `BOARD_HAS_RESERVOIR_PUMP`,
`BOARD_LEVEL_ACTIVE_LOW`, `BOARD_HAS_INA226`, `BOARD_NAME`). Never
hard-code GPIO numbers elsewhere. Board-conditional code uses
`#if CONFIG_BOARD_REV2` / `#if BOARD_HAS_INA226`. Enforcement pattern: a
capability flag at 0 leaves its pin/address macro UNDEFINED
(`BOARD_PIN_RS485_DE`, `BOARD_PIN_RESERVOIR_PUMP`, `BOARD_INA226_ADDR`),
so an unguarded reference is a compile error, never a phantom GPIO.

## BME280 environmental sensor (I2C)

Feature 005 (PR-03). Same architecture split as the soil sensor, at the
`II2cBus` interface: `Bme280Sensor` is pure C++ and holds ALL policy —
0x76→0x77 address probing with chip-ID verification (0xD0 == 0x60,
rejects e.g. a BMP280), calibration readout/parsing (incl. the
split-nibble dig_H4/H5), the Bosch datasheet reference compensation
(int32 T / int64 P / int32 H via t_fine, transcribed exactly — do not
"clean up"), unit conversion (°C/%RH/hPa), error codes 0/1/2, lazy
re-init and uninitialize-on-bus-error recovery (re-probes BOTH
addresses). It is host-tested against `MockI2cBus`, including Bosch
reference vectors. `EspI2cBus` is the only hardware touchpoint (the new
`driver/i2c_master.h` API — never the legacy `driver/i2c.h` — 100 kHz,
pins from `board/board.h`) and is excluded from the linux build.

**Shared bus (PR-05):** `app_main` owns the single `EspI2cBus` instance
(function-local static); PR-05's INA226 driver must receive the SAME
instance — never create a second bus on these pins. Bus-level transaction
safety comes from the i2c_master driver's bus lock; snapshot consistency
comes from `LockedEnvironmentalSensor`, the mandatory wrapper for all
cross-task access (sensor task + console REPL now; web PR-09, controller
PR-11).

**Sampling profile is parity** (like-for-like HIL comparison against the
Arduino unit): NORMAL mode, oversampling T×2 / P×16 / H×1, IIR ×16,
standby 500 ms — ctrl_hum written before ctrl_meas, then config.

The `sensor_task` (main/, 4096 B stack, priority 1, `vTaskDelayUntil`
5000 ms — parity parameters) polls the locked sensor, starts even when
the sensor is absent (lazy re-init recovers later) and never exits. Its
WARN/INFO/silence decisions live in the pure `SensorTaskLogPolicy`
(host-tested): WARN once on the valid→invalid transition and on recovery,
bounded repeats every 12th consecutive failure. Deliberate divergences
from the legacy driver (address probing, last-good getters, live
availability probe, locked access) are recorded in
`docs/parity-checklist.md` §6.

## Reservoir level sensors (XKC-Y26)

Feature 006 (PR-05). Two independent `ILevelSensor` instances (low mark
GPIO 32, high mark GPIO 33) — PR-11's controller composes its reservoir
truth table from them; this layer never aggregates. Split at the
`IDigitalInput` seam: `DebouncedLevelSensor` is pure C++ and holds ALL
policy — the SETTLING → WARMUP → TRACKING state machine, the
stability-window debounce (`BOARD_LEVEL_DEBOUNCE_MS`, 300 ms; any raw flip
restarts the window) and the polarity mapping — host-tested against a
scripted input + `FakeTimeProvider`; `GpioLevelSensor` is the only
hardware touchpoint (input + internal pull-up on BOTH boards, one
`gpio_get_level`, no logic) and is excluded from the linux build.

**Polarity is board configuration (FW-5), never application `#ifdef`s:**
rev1 reads the XKC-Y26 directly (active HIGH), rev2 goes through a 2N7002
inverter (active LOW) — `BOARD_LEVEL_ACTIVE_LOW` is passed to the
constructor at the wiring site. **Settle gating (FW-3):** readings report
not-yet-valid for `BOARD_LEVEL_SETTLE_MS` after a power-on event (rev1 0,
rev2 500 ms); `notifyPowerOn()` re-arms the gate — app_main calls it once
at boot, real rail control (`SENS_PWR_EN`) arrives with PR-14. Consumers
MUST gate on `isValid()`: not-yet-valid is a distinct state, never wet or
dry. Fail direction (pinned by host tests, parity checklist line 97): a
disconnected input reads pulled-HIGH ⇒ rev1 "water present" (fill pump
stays off), rev2 "water absent" (drawing node does not pump) — both fail
safe for their pump topology. `update()` is polled from the 10 Hz main
loop; cross-task access (console `level`) goes through
`LockedLevelSensor`. The capability flag `BOARD_HAS_RESERVOIR_PUMP`
(rev1 1, rev2 0 — single-pump decision, master PRD FR4) gates ALL
reservoir-pump wiring: instance, boot force-OFF and console registration
exist only where the pump does; on rev2 the pin macro is removed so
unguarded references fail the build.

## INA226 pump power monitor (I2C, rev2 only)

Feature 006 (PR-05). `Ina226Sensor` is pure C++ over `II2cBus`
(`IPowerSensor`: bus V / signed current A / power W) and clones the
BME280 architecture: identity check at init (manufacturer 0xFE == 0x5449,
die 0xFF == 0x2260 — foreign devices rejected with error 1), error codes
0/1/2, last-good getters with NaN placeholders, lazy re-init and
uninitialize-on-bus-error recovery. Config (AVG ×16, 1.1 ms conversions,
continuous shunt+bus — value derivation in `Ina226Sensor.cpp`) and
calibration (`CAL = 0.00512 / (Current_LSB × R_shunt)`, Current_LSB fixed
0.5 mA) are written at init; the shunt comes from Kconfig
(`CONFIG_WS_INA226_SHUNT_MILLIOHM`, default 5 mΩ ⇒ CAL 2048).

**Shared-bus rule:** the driver receives app_main's ONE `EspI2cBus`
instance (the same the BME280 uses) — never a second bus on these pins.
Reads are on-demand (console `power` now, PR-09 API later; no periodic
task); cross-task access goes through `LockedPowerSensor`. Build gating
(FR-011): `Ina226Sensor.cpp` builds on linux always (host tests) and on
target only when `CONFIG_BOARD_REV2` — the rev1 binary contains no INA226
code. **Hardware validation is deferred to PR-14** (no INA226 on the rev1
rig): the driver is host-verified only, and the written config value
carries a `TODO(PR-14)` bench confirmation.

## Partition layout (4MB flash)

nvs (0x9000, 16K) | otadata (0xd000, 8K) | phy_init (0xf000, 4K) |
ota_0 (0x10000, 1.5M) | ota_1 (0x190000, 1.5M) | storage/littlefs
(0x310000, 960K). A/B OTA with bootloader rollback enabled; app binaries
must fit in 1.5MB per slot.

## Code conventions

- **C++ with IDF-native APIs only.** No Arduino compatibility layers.
  IDF v6 defaults to gnu++26; stick to ~C++23 features.
- **`extern "C" void app_main(void)`** — app_main has C linkage.
- **Fail-safe first:** the first statements in `app_main` drive every pump
  GPIO that exists on the board to OFF (capability-aware since feature
  006). This invariant must survive every future change.
- **No non-trivial static/global constructors.** They run before `app_main`
  and would execute ahead of (and thus bypass) the pump fail-safe. All
  initialization is explicit, inside or after `pumps_force_off()`.
- **Interface-based architecture** will be ported from the Arduino code in
  phase 1+ (`ISensor`, `IEnvironmentalSensor`, `ISoilSensor`, `IActuator`,
  `IWaterPump`, `IModbusClient`, `IDataStorage`). Drivers implement these
  interfaces against raw IDF APIs.
- **`std::string`**, never Arduino `String`.
- **RAII** for all resource management; avoid raw owning pointers and ad-hoc
  dynamic allocation.
- **ESP_LOG** with a per-component `static const char *TAG`; no printf
  logging. Check IDF errors with `ESP_ERROR_CHECK` or explicit handling.
- **Include guards:** `WATERINGSYSTEM_PATH_FILE_H`
  (e.g. `WATERINGSYSTEM_BOARD_BOARD_H`).
- **Managed dependencies** are pinned exactly in `main/idf_component.yml`
  AND `components/sensors/idf_component.yml` (esp-modbus is pinned `==2.1.2`
  in both — bump the two in lockstep or the resolver conflicts);
  `dependencies.lock` is committed, `managed_components/` is not.

## Testing strategy

Application logic must stay host-testable: controllers and business logic
depend only on the interfaces above, so they can be compiled and unit-tested
against mock implementations on the IDF **linux** target (test suite arrives
in phase 4 and runs in CI). Hardware-near code (drivers) is verified
hardware-in-the-loop on the rev1 bench rig.
