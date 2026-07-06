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
dry. Fail direction (pinned by host tests, parity checklist §3, "Pull-up +
active-HIGH consequence" item): a
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

## WiFi provisioning & station management

Feature 007 (PR-07). **FR9 decision: a custom SoftAP provisioning portal** (AP
at 192.168.4.1, WPA2, a minimal self-contained setup page + one POST handler on
a standalone `esp_http_server`), chosen over IDF `wifi_provisioning` — full spec
and rationale in `specs/007-wifi-provisioning/` (`spec.md` + research decision
**D1**; `wifi_provisioning` is the documented re-escalation fallback if the
portal proves unreliable at HIL, research R1). The `network` component splits at
the `IWifiDriver` seam, same pattern as the sensor drivers:

- **Pure, host-tested** (`components/network/include/network/`): the
  `WifiManager` state machine (Provisioning / Connecting / Connected /
  Reconnecting / ReconnectPaused, parity reconnect cadence — 10 s retry, +60 s
  pause after 5 consecutive failures, 5 s health monitor; **never reboots** —
  FR-013 no boot loop), `validateWifiCredentials` (SSID 1–32, password
  empty-or-8..64), `decideBootMode` + `shouldClearCredentialsOnBoot`
  (`WifiBootMode.h`). Driven over `MockWifiDriver` + `FakeTimeProvider` in
  `test_apps/host/main/test_wifi.cpp`.
- **Hardware touchpoints** (target-only, excluded from the linux build):
  `EspWifiDriver` (STA + AP netifs, `esp_event` → `WifiEvent` queue) and
  `ProvisioningPortal` (`esp_http_server`).

**Isolation (FR-014):** `WifiManager`'s constructor takes only
`IWifiDriver&`/`IConfigStore&`/`ITimeProvider&`/`ReconnectPolicy` — no
watering/pump/sensor reference — and the wifi task is a SEPARATE FreeRTOS task
from the 10 Hz pump/level loop with no shared mutex; WiFi outages never touch
watering. Credentials come from PR-06's `IConfigStore` (never logged, FR-004).

**Boot flow in `app_main`** (all strictly after `pumps_force_off()`): read the
config button (`BOARD_PIN_BTN_CONFIG`, GPIO18, active LOW, >= 5 s hold, 100 ms
LED blink) → `decideBootMode` → provisioning (button-forced on a configured
device clears credentials first, per the data-model boot rule) or station
(`begin(Station)` + `wifi_task_start`). Kconfig: `WS_PROV_AP_SSID`,
`WS_PROV_AP_PASSWORD`, `WS_WIFI_*` reconnect constants. LED scope (parity
§7/§9): 500 ms connect-attempt toggle (wifi task) + 100 ms config-button-hold
blink (app_main); HIL checklist in `specs/007-wifi-provisioning/checklists/hil.md`.

## SNTP time, task watchdog & event logging (feature 008)

Feature 008 (PR-08) adds two small components plus target-side glue in `main/`.

**`time` component** — pure `TimeService` (epoch plausibility + Swedish
local-time formatting via `<ctime>`, host-tested), the `IWallClock` target
implementation `SystemWallClock` (`time(nullptr)`) and the `SntpClient` starter
(`esp_netif_sntp` against `CONFIG_WS_SNTP_SERVER`, TZ `CET-1CEST,M3.5.0,M10.5.0/3`).
`SntpClient.cpp` is the only genuine IDF touchpoint; `SystemWallClock.cpp` is
pure POSIX (`time(nullptr)`), kept target-side by convention (the host injects
`FakeWallClock`). Both are excluded from the linux build (same mechanism as
storage/sensors/network). `app_main`
constructs one `SntpClient`, calls `applyTimezone()` once at init, and the
`SystemObserver` starts the SNTP service **once, on the first
`WifiState::Connected` transition** (SNTP needs an IP) — never in
provisioning/headless mode. `start()` is idempotent and non-fatal: an
unreachable server is retried by the SNTP service, never a boot/watering
failure. The diag console `time` command reads `SystemWallClock` + the
`SntpClient`'s `SyncStatus`.

**Time-not-set contract (for PR-11):** until the first successful sync the wall
clock is implausible (`isPlausibleEpoch(0)` is false); consumers MUST treat a
not-set clock as "no timestamp" rather than 1970. The event log records events
regardless (with whatever the clock reports); PR-11's scheduler must gate any
time-of-day watering decision on a plausible clock.

**`events` component** — the pure, host-tested `EventLogger` (categories
`reset`/`wifi`/`pump`; producers `logReset`/`logWifi`/`logPumpStart`/`logPumpStop`;
a failed store increments a dropped counter, never throws — logging never blocks
or crashes watering, FR-014). It writes through the shared `LockedDataStorage`.
The target-side `SystemObserver` (`main/system_observer.*`) edge-detects WiFi
state changes and pump start/stop from the 10 Hz loop and forwards them. **Reset
reason:** at boot, exactly once, `app_main` calls `esp_reset_reason()` →
`event_logger.logReset(...)` (before `watchdog_init()`), so a prior watchdog
reboot appears in `storage events` as `reset=TASK_WDT`. The pump fail-safe still
runs first (top of `app_main`); this only observes the cause.

**Task watchdog** (`main/task_watchdog.*`, thin wrappers over `esp_task_wdt`).
`CONFIG_ESP_TASK_WDT_INIT=y` + `CONFIG_ESP_TASK_WDT_PANIC=y` (sdkconfig.defaults)
init the WDT at boot; `watchdog_init()` **reconfigures** it to
`CONFIG_WS_TASK_WDT_TIMEOUT_S` (default 20 s, panic→reboot) —
`esp_task_wdt_reconfigure()`, with an `esp_task_wdt_init()` fallback if it was
not inited. **Subscription policy (contracts/task-watchdog.md):** ONLY the two
watering-critical tasks subscribe (`esp_task_wdt_add(NULL)`) and feed
(`esp_task_wdt_reset()`) — the 10 Hz main loop (feeds each 100 ms iteration) and
the 5 s sensor task (feeds each cycle). The **WiFi task is deliberately NOT
subscribed** (a network stall must never reboot the device — FR-014 isolation)
and neither is the esp_console REPL (it blocks on UART by design). The 20 s
default keeps a safe margin over the slowest subscribed cadence (5 s) so a
healthy task is never falsely tripped. PR-11's watering/reservoir tasks register
through the same helper when they land. HIL checklist:
`specs/008-sntp-watchdog-logging/checklists/hil.md`.

## HTTP API v1 (feature 009)

Feature 009 (PR-09) adds the `api` component: a versioned `/api/v1/` REST/JSON
server split into a pure, host-tested core and a thin target-only HTTP shell.

- **Pure layer** (`components/api/include/api/` + `src/`, builds on linux, host-
  tested): `ApiSerialize` (DTO → JSON success bodies), `ApiRequests`
  (`parseConfigSet`/`parsePumpCommand`/`namedRangeToWindow`, all-or-nothing
  validation against the `IConfigStore` range constants), `ApiEnvelope`
  (`successBody`/`errorBody`/`notFoundBody`), `ApiRoutes` (the static route table
  + `matchRoute`), and the POD `ApiDtos`. JSON is built with **cJSON** via the
  managed `espressif/cjson` component (it links on the linux preview target — no
  esp_http_server dependency in the pure layer). A host test asserts `matchRoute`
  resolves the route set; the route table, the ApiServer's own `httpd_uri_t[]`
  registration and the frozen `docs/api/openapi.yaml` are kept in lockstep BY
  HAND (FR-004).
- **Target-only shell** (`ApiServer.*`, excluded from the linux build, same PRIV
  rule as `ProvisioningPortal`/`EspI2cBus`): `esp_http_server.h` appears ONLY in
  the `.cpp`; the handle is an opaque `void*` in the header and the route
  handlers are file-local functions. Each handler reads the injected `Locked*`
  decorators into a DTO, calls a pure serializer, and does the `httpd_resp_*`
  plumbing. Unknown routes answer the JSON 404 envelope via a registered
  `httpd_register_err_handler`.

**Contract** — the endpoints are `GET status/sensors/history/pumps/config/power/
events` and `POST pumps/{name}`, `config`, `selftest`, `ota`. The single source
of truth is `docs/api/openapi.yaml` (OpenAPI 3.0), **frozen at merge** — contract
changes require a version bump (a new `/api/vN` prefix), never an in-place edit.
Errors use the shared envelope: 400 (malformed/out-of-range), 404 (unknown route
or pump name), 409 (start on a running pump), 501 (OTA stub); the wifi password
is never serialized in any DTO or field (FR-004).

**Non-blocking rule (QUIRK 5):** handlers use ONLY the non-blocking cached
getters through the `Locked*` wrappers — never `read()`/`isAvailable()` — so a
slow or flooding client can never stall the I2C/RS485 bus or delay the 10 Hz
pump/level loop. **The one exception is `POST /api/v1/selftest`**, a bounded,
on-demand diagnostic that deliberately issues a real `read()` on the
environmental and soil sensors (the soil read is the RS485/Modbus round-trip
test); it runs on the httpd task off the watering critical path and is serialized
with the other bus users through the same `Locked*` wrappers. The server is NOT
subscribed to the task watchdog and shares no mutex with the watering loop beyond
those wrappers (FR-015 isolation).

**Semantics worth remembering:** `mode` is derived from `wateringEnabled`
(`automatic`/`manual`); `soil` and (rev2) `power` report `valid=false` with
last-good/NaN placeholders until PR-11 wires their periodic readers; `power` is a
board-capability field (INA226 object on rev2, `available:false`/`null` on rev1);
pumps are capability-enumerated (`BOARD_HAS_RESERVOIR_PUMP` — rev2 is
single-pump); `/history` windows resolve from a named `range` else explicit
`start`/`end` else the last 24 h, and an empty window is a 200 with empty arrays;
`/events` is newest-first and count-bounded (default 50, cap 200). The server
makes NO watering decision — the pump's own `runFor()`/`stop()` enforce the 300 s
cap and no-restart rule. Wifi state is read via `WifiManager::snapshot()` (an
unsynchronized single-writer by-value copy, acceptable for status display —
mirrors the PR-08 SyncStatus decision); **v1 has no authentication** (trusted
LAN); `POST /api/v1/ota` is a 501 stub until PR-13.
The server is constructed in `app_main` and `start()`ed on the first
`WifiState::Connected` transition (an IP is required to bind on the STA
interface); `start()`/`stop()` are idempotent and non-fatal. HIL checklist:
`specs/009-http-server-api-v1/checklists/hil.md`.

## Watering controller (feature 011)

Feature 011 (PR-11) adds the `control` component — the automatic watering
logic, 100 % host-tested pure C++ over the interfaces (the master-PRD
criterion). No IDF/`esp_*` includes; all time is injected (`ITimeProvider`
monotonic for gates/staleness, `IWallClock` epoch for log timestamps) and every
threshold/duration is read from `IConfigStore` each tick (runtime-tunable). Two
classes, both driven over mocks + `FakeTimeProvider`/`FakeWallClock` in
`test_apps/host/main/test_watering_controller.cpp` + `test_reservoir.cpp`:

- **`WateringController`** — pulsed automatic watering. `tick()` order is
  safety-first: `plant.update()` (self-stop + 300 s cap) → burst-end detection →
  single soil read → periodic data-log → manual-override branch → enabled gate →
  **fail-safe** → gate-on-read → watering decision. Start at moisture ≤ low when
  the soak pause has elapsed; stop at ≥ high. **Fail-safe (soil unavailable /
  stale > 30 000 ms / moisture out of 0–100 %) is checked BEFORE the soak gate
  and is never delayed by it** — a pending soak pause never postpones a safety
  stop. Automatic decisions gate on a successful in-range read, never on
  placeholder/last-good values.
- **Soak-pause divergence (parity):** the min-watering-interval soak is measured
  from the burst **END** (true absorption), and it is ENFORCED — no new burst
  starts until it elapses even while the soil still reads dry. The burst-end
  edge is detected whether the pump self-stopped (duration/cap) or was stopped
  at the high threshold.
- **Manual override:** `startManual(int)` clamps to 1..300 s, runs the plant
  pump and sets a flag that exempts the run from the automatic fail-safe;
  `stop()` clears it; a pump self-stop clears it on the next tick. Manual is
  `wateringEnabled == false` at the API layer — the controller never calls an
  `isManualMode()` on the pump.
- **`ReservoirController`** (rev1 only, but host-tested regardless of board) —
  the level truth table over two `ILevelSensor` snapshots (invalid or
  implausible dry+wet rows → no action; dry+dry → fill; high-wet → stop),
  running-safety stop-on-high, the pump's 300 s cap for the max-fill abort, and
  a **post-abort cooldown** (`kReservoirRefillCooldownMs`, ~60 s): after a
  `MaxRuntimeForced` abort no new automatic fill starts until the cooldown
  elapses even if still low-dry — guards a stuck high sensor / empty source. A
  normal high-wet stop does not arm the cooldown; manual fill bypasses it. This
  cooldown is a deliberate divergence from parity (`docs/parity-checklist.md`).
- **Logging (FR-014):** every `dataLogInterval` the controller logs env
  (`env_temperature/humidity/pressure`) + soil (`soil_moisture/temperature/ph/
  ec`, plus NPK only when ≥ 0), stamped with `IWallClock::nowEpoch()` and gated
  on `isTimeSet()` (never a bogus 1970). `soil_humidity` is intentionally NOT
  logged — `ISoilSensor::getHumidity()` is identical to `getMoisture()` (parity,
  register 0x0000), so the max distinct-metric set is exactly 10 = the
  `IDataStorage` `kMaxMetrics` cap, no data loss. Fail-safe events go through
  `EventLogger::logFailsafe`; pump start/stop transitions stay owned by
  `SystemObserver` (no double-log).

**Snapshot helpers:** `LockedSoilSensor::snapshot()` / `LockedEnvironmentalSensor`
/ `LockedLevelSensor` return `{Soil,Env,Level}Snapshot` — all values + validity
(plus the error code for `SoilSnapshot`) copied out under ONE lock, closing the
read()-then-getter cross-call gap without a fresh (blocking) bus read (QUIRK 5).

**On-target wiring:** the pure logic runs on `main/watering_task.cpp`, a
watchdog-subscribed FreeRTOS task ticking at `config.getSensorReadIntervalMs()`
(floored at 1 s). The controller is the **periodic soil reader** (controller-as-
reader): its per-tick `read()` is the blocking Modbus transaction that refreshes
the `LockedSoilSensor` cache the API `/sensors` endpoint serves — so there is no
separate soil-reader task, and the blocking bus I/O is isolated off the 10 Hz
safety loop (which still owns precise pump-timing enforcement + `observer.poll()`).
The API mode flag reaches the controller purely through `config`
(`getWateringEnabled()`, read each tick) — no direct ApiServer↔controller call
(FR-017 isolation). Reservoir (rev1): `tick(true, getWateringEnabled())` — the
pump always exists so `enabled` is always true; auto level control is gated by
the same mode flag (manual mode suspends auto-fill; manual API fills still work).
There is deliberately no dedicated auto-level config flag.

**RS485 race fix (T016):** `LockedModbusClient` (header-only `IModbusClient`
decorator) wraps the `EspModbusClient` in `app_main`; the `ModbusSoilSensor` and
the console `rs485test` command now share one mutex, so the periodic reader and
the diagnostic probe can no longer overlap bus transactions. Lock order is always
(soil mutex → modbus mutex) or (modbus mutex alone) — no deadlock. HIL checklist:
`specs/011-watering-controller-host-tests/checklists/hil.md`.

## Frontend from littlefs (feature 010)

Feature 010 (PR-10) serves the web dashboard from the littlefs `storage`
partition over the SAME `esp_http_server` that exposes `/api/v1/`, minimally
adapted to the frozen contract — it is the end-to-end test client. No redesign,
no framework (that is a separate PRD).

- **Source of truth: `firmware/web/`** — adapted COPIES of the frozen `data/`
  frontend (`index.html`/`script.js`/`styles.css`/`favicon.ico`); `data/` stays
  read-only, `wifi_setup.html` is not carried over (its `/api/wifi/*` endpoints
  do not exist in v1). `firmware/web/vendor/` holds the third-party libs vendored
  locally so the greenhouse client needs NO internet: Chart.js 4.4.3,
  chartjs-adapter-date-fns 3.0.0, and a PURGED Tailwind 3.4.17 CSS. See
  `firmware/web/README.md` for pinned versions + the manual Tailwind regen step.
- **Build pipeline:** `firmware/tools/gzip_assets.py` deterministically
  (`mtime=0`, level 9) gzips `firmware/web/` into a build staging dir merged with
  the `storage_image/` seed; `firmware/main/CMakeLists.txt` stages it and points
  `littlefs_create_partition_image` at it (assets land at `/storage/*.gz`,
  coexisting with runtime history/event files). NO committed `.gz`, NO
  node/Tailwind toolchain in CI — only Python gzip (Constitution III). `.md`
  files are excluded from the image.
- **Static serving:** a `GET /*` catch-all in `ApiServer` (target-only),
  registered LAST so the exact `/api/v1/` routes match first; it sanitizes the
  path (`ApiStatic` — pure, host-tested: `sanitizeAssetPath` rejects `..`/NUL/
  backslash/absolute-escape, maps `/`→`index.html`; `contentTypeForPath`), opens
  `<StorageMount base>/<path>.gz`, and streams it with `Content-Encoding: gzip`
  + content-type + `Cache-Control`. Missing/rejected → the JSON 404 envelope.
  GET-only (POST API routes unaffected); file I/O only, off the watering buses
  (isolation class of `/history`); no second server/port/task.
- **JS adaptation (`firmware/web/script.js`):** `ENDPOINT=/api/v1`; reads
  `environmental/soil .valid` (null-safe — soil `valid:false` until PR-11);
  status remapped (wifi not network, storage in bytes, `mode` string) with pump
  state from `GET /pumps` and config from `GET /config`; JSON POST bodies for
  mode/config/pump-run-stop with 409/4xx handling. Reservoir UI reduced to manual
  start/stop + level display (v1 has no enable/auto-level endpoint) and hidden on
  single-pump rev2. History by `?metric=&range=`. OTA button hits the `/ota` 501
  stub (real OTA is PR-13). The frozen `/api/v1/` contract is unchanged — any
  genuine contract gap is escalated as a PR-09 amendment, never patched here.

HIL checklist: `specs/010-frontend-littlefs-assets/checklists/hil.md`.

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
