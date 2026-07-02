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
store, data storage and the soil sensor decode/validation/calibration
(`test_soil_sensor.cpp`, real `ModbusSoilSensor` over `MockModbusClient`).
The test executable's exit code equals the Unity failure count (CI gate, job
`host-test`):

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
│   ├── Kconfig.projbuild       # Board revision choice
│   └── idf_component.yml       # Pinned managed deps (esp-modbus, littlefs)
├── components/
│   ├── board/                  # Board abstraction (header-only)
│   │   └── include/board/board.h
│   ├── interfaces/             # Header-only, NO IDF deps (host-includable)
│   │   └── include/interfaces/ # IActuator, IWaterPump, ITimeProvider,
│   │                           # IConfigStore, IDataStorage,
│   │                           # IModbusClient, ISoilSensor
│   ├── actuators/              # Pump drivers
│   │   ├── include/actuators/  # WaterPump (pure C++ logic), GpioWaterPump,
│   │   │                       # EspTimeProvider (esp32-only header),
│   │   │                       # testing/ (MockWaterPump, FakeTimeProvider)
│   │   └── src/                # GpioWaterPump.cpp excluded on linux target
│   ├── sensors/                # RS485 Modbus soil sensor (feature 004)
│   │   ├── include/sensors/    # ModbusSoilSensor (pure C++ logic),
│   │   │                       # EspModbusClient, LockedSoilSensor,
│   │   │                       # testing/ (MockModbusClient, MockSoilSensor)
│   │   └── src/                # EspModbusClient.cpp + esp-modbus dep
│   │                           # excluded on linux target
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
                                # soil sensor (test_soil_sensor.cpp) suites
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
pump status                              # both pumps
```

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
(`BOARD_PIN_*`, `BOARD_HAS_RS485_DE`, `BOARD_LEVEL_SENSOR_ACTIVE_LOW`,
`BOARD_HAS_INA226`, `BOARD_NAME`). Never hard-code GPIO numbers elsewhere.
Board-conditional code uses `#if CONFIG_BOARD_REV2` / `#if BOARD_HAS_INA226`.

## Partition layout (4MB flash)

nvs (0x9000, 16K) | otadata (0xd000, 8K) | phy_init (0xf000, 4K) |
ota_0 (0x10000, 1.5M) | ota_1 (0x190000, 1.5M) | storage/littlefs
(0x310000, 960K). A/B OTA with bootloader rollback enabled; app binaries
must fit in 1.5MB per slot.

## Code conventions

- **C++ with IDF-native APIs only.** No Arduino compatibility layers.
  IDF v6 defaults to gnu++26; stick to ~C++23 features.
- **`extern "C" void app_main(void)`** — app_main has C linkage.
- **Fail-safe first:** the first statements in `app_main` drive both pump
  GPIOs to OFF. This invariant must survive every future change.
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
