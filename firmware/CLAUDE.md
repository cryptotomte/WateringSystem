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

Pump enforcement logic is unit-tested natively, no ESP32 needed. The test
executable's exit code equals the Unity failure count (CI gate, job
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
│   │   └── include/interfaces/ # IActuator, IWaterPump, ITimeProvider
│   └── actuators/              # Pump drivers
│       ├── include/actuators/  # WaterPump (pure C++ logic), GpioWaterPump,
│       │                       # EspTimeProvider (esp32-only header),
│       │                       # testing/ (MockWaterPump, FakeTimeProvider)
│       └── src/                # GpioWaterPump.cpp excluded on linux target
└── test_apps/
    └── host/                   # Host test app (linux preview target, Unity)
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

## Board abstraction

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
- **Managed dependencies** are pinned exactly in `main/idf_component.yml`;
  `dependencies.lock` is committed, `managed_components/` is not.

## Testing strategy

Application logic must stay host-testable: controllers and business logic
depend only on the interfaces above, so they can be compiled and unit-tested
against mock implementations on the IDF **linux** target (test suite arrives
in phase 4 and runs in CI). Hardware-near code (drivers) is verified
hardware-in-the-loop on the rev1 bench rig.
