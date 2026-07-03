# Feature Parity Checklist — Arduino v2.3 → ESP-IDF

**Status:** Draft (Phase 0, work package B1)
**Source of truth:** the Arduino firmware in `src/` + `include/` at branch `001-phase0-foundation` (every item cites `file:line`)
**Related:** `docs/PRD-esp-idf-migration.md` (FR1–FR13), `docs/partition-plan.md`

## Purpose and usage

This checklist is the **contract for "feature parity reached"** (PRD success criterion: "Paritetschecklista 100% avbockad"). It enumerates every observable behavior of the Arduino v2.3 firmware, extracted directly from the source code — not from older documentation, which is wrong in several places (see notes inline).

- **Reviewed by:** Paul (Phase 0 sign-off of the list itself).
- **Checked off:** Phase 4, on the bench rig (rev1 devkit + RS485 + soil sensor). Host-testable items must also be green in the CI host test suite before rig sign-off.
- **Parity policy:** parity means *behavioral* parity, not bug-for-bug parity. Where the Arduino code contains a known bug or quirk, the item states the **correct target behavior** and the quirk is documented in the "Known Arduino quirks" section at the end. Each such item is marked **(QUIRK n)**.

### Legend

| Tag | Meaning |
|---|---|
| `[HOST]` | Verifiable on host (IDF Linux target) against mock interfaces; covered by CI test suite |
| `[HIL]` | Requires hardware-in-the-loop verification on the bench rig |

Items tagged `[HOST]` are *also* spot-checked on the rig during Phase 4 sign-off.

---

## 1. Watering logic (WateringController)

Defaults (`src/WateringController.cpp:16-21`):

- [ ] `[HOST]` Default low moisture threshold = **30.0 %** (`src/WateringController.cpp:19`)
- [ ] `[HOST]` Default high moisture threshold = **55.0 %** (`src/WateringController.cpp:20`)
- [ ] `[HOST]` Default watering duration = **20 s** (`src/WateringController.cpp:21`)
- [ ] `[HOST]` Default minimum watering interval = **300 s** (`src/WateringController.cpp:18`; comment notes it was reduced from 6 h for testing)
- [ ] `[HOST]` Default sensor read interval = **5000 ms** (`src/WateringController.cpp:16`)
- [ ] `[HOST]` Default data log interval = **300000 ms** (5 min) (`src/WateringController.cpp:17`)

Sensor read cadence:

- [ ] `[HOST]` Sensors are read by a dedicated FreeRTOS task every `sensorReadInterval` (5 s default), decoupled from the main loop (`src/WateringController.cpp:485-534`; task created with 4096 B stack, priority 1, `src/WateringController.cpp:549-556`)
- [ ] `[HOST]` Sensor task reads BME280 *and* soil sensor each cycle and signals "new data available" to the control path via mutex-protected shared state (`src/WateringController.cpp:493-525`)
- [ ] `[HOST]` Sensor task starts even when soil sensor init fails, so the system can recover a flaky sensor later (`src/WateringController.cpp:134-139`)

Automatic-mode decision logic (`src/WateringController.cpp:273-330`):

- [ ] `[HOST]` Watering starts when: watering enabled AND pump not running AND moisture ≤ low threshold; pump is started for the configured duration (`src/WateringController.cpp:303-313`)
- [ ] `[HOST]` The minimum watering interval is **not enforced** in the Arduino decision logic — "if it's dry, water immediately" (`src/WateringController.cpp:303` comment). The value is configurable and persisted but unused as a gate. **[RESOLVED by Paul 2026-06-10: the ESP-IDF port MUST enforce it as a soak/absorption pause — a DELIBERATE behavior change, not parity. Watering happens in bursts with enforced pauses so the soil can absorb (continuous watering pools on the surface while the moisture sensor lags). Burst duration and pause are both configurable; values tuned empirically. Full model in `docs/prd/PR-11-watering-controller-host-tests.md`.]**
- [ ] `[HOST]` Watering stops early when moisture ≥ high threshold while the pump runs (`src/WateringController.cpp:315-321`)
- [ ] `[HOST]` A timed run also stops by itself when the configured duration elapses (`src/actuators/WaterPump.cpp:148-171`)
- [ ] `[HOST]` Enabling/disabling automatic watering is persisted immediately (`src/WateringController.cpp:370-374`)
- [ ] `[HOST]` Configuration setters validate input: thresholds clamped to 0–100 % (`src/WateringController.cpp:445-459`), watering duration 1–300 s (max 5 min, `src/WateringController.cpp:461-467`), min interval > 0 (`src/WateringController.cpp:469-475`); every accepted change is persisted
- [ ] `[HOST]` Configuration is loaded from storage at init (key `watering_config`, JSON with keys `sensorReadInterval`, `dataLogInterval`, `minWateringInterval`, `moistureThresholdLow`, `moistureThresholdHigh`, `wateringDuration`, `wateringEnabled`) (`src/WateringController.cpp:152-214`)
- [ ] `[HOST]` Sensor data is logged to storage every data-log interval: env temperature/humidity/pressure + soil moisture/temperature/ph/ec, NPK only when ≥ 0 (`src/WateringController.cpp:266-270`, `332-363`)

Manual override semantics:

- [ ] `[HOST]` Manual watering with duration > 0 runs the pump as a timed run flagged *manual mode*; duration 0 starts an indefinite run (`src/WateringController.cpp:381-415`, `src/actuators/WaterPump.cpp:104-125`)
- [ ] `[HOST]` Manual-mode pump operation is exempt from sensor-failure safety stops (it must keep running when sensors fail) (`src/WateringController.cpp:237-239`)
- [ ] `[HOST]` `stop` always stops the pump regardless of mode and clears the manual-mode flag (`src/WateringController.cpp:417-425`, `src/actuators/WaterPump.cpp:83-102`)
- [ ] `[HIL]` Manual button (GPIO 5, active LOW, 50 ms debounce) toggles: pump running → stop; pump stopped → manual watering for 20 s (`src/main.cpp:387-408`)
- [ ] `[HIL]` Config button (GPIO 18, active LOW) toggles automatic watering enable/disable at runtime (`src/main.cpp:411-420`)
- [ ] `[HOST]` **(QUIRK 1)** Mode-flag semantics target: pump runs started *by the automatic controller* must count as automatic (subject to all fail-safe stops); runs started *by the user* (button/web) count as manual. The Arduino code instead flags **every timed run as manual** — including auto-watering — and every indefinite run as automatic, which inverts the intended safety semantics. Target = intended behavior, host-tested. See QUIRK 1 below.

## 2. Safety / fail-safe

- [ ] `[HOST]` Pump GPIO is driven LOW (off) when the pump driver initializes (`src/actuators/WaterPump.cpp:38-40`)
- [ ] `[HOST]` **(QUIRK 2)** Target: **both** pump GPIOs (26 main, 27 reservoir) are actively driven LOW at boot, after watchdog reset and after OTA restart (PRD NFR "pumpar alltid av vid boot"). In the Arduino code only the plant pump is initialized at boot; the reservoir pump GPIO is configured lazily on first `start()` and floats until then (`src/main.cpp:744-750` initializes the controller/plant pump only; `src/actuators/WaterPump.cpp:62-68` lazy init). See QUIRK 2.
- [ ] `[HOST]` Sensor failure in automatic mode = immediate pump stop: when the soil sensor reports unavailable while the pump runs in automatic mode, the pump is stopped at once ("EMERGENCY PUMP STOP") (`src/WateringController.cpp:229-243`)
- [ ] `[HOST]` Manual mode bypasses that stop: pump in manual mode continues despite sensor failure, with an informational log (`src/WateringController.cpp:237-239`)
- [ ] `[HOST]` Stale-data stop: pump running in automatic mode with no valid sensor data for > **30000 ms** (or never) is emergency-stopped — checked both periodically in `update()` and on each processing pass (`src/WateringController.cpp:255-264`, `275-284`)
- [ ] `[HOST]` Invalid-reading stop: moisture outside 0–100 % while pump runs in automatic mode → emergency stop and no watering decision is taken (`src/WateringController.cpp:292-301`)
- [ ] `[HOST]` Watering duration is capped at 300 s (5 min) at the configuration level (`src/WateringController.cpp:463`)
- [ ] `[HOST]` Reservoir pump max runtime = **300000 ms** (5 min) hard safety timeout, applies to both manual and automatic filling (`src/main.cpp:81`, `519-522`)
- [ ] `[HOST]` Reservoir pump stops immediately when the high-level sensor trips, in both manual and automatic filling (`src/main.cpp:512-516`)
- [ ] `[HOST]` Pump objects stop the pump on destruction; controller stops the pump on teardown (`src/actuators/WaterPump.cpp:24-30`, `src/WateringController.cpp:62-66`)
- [ ] `[HIL]` **(QUIRK 3)** Watchdog: system recovers automatically from a hung task. Target implementation: `esp_task_wdt` on all critical tasks (PRD NFR). The Arduino "software watchdog" (30 s, `src/main.cpp:107-110`, `899-917`) is fed and checked back-to-back in the same loop iteration and can never actually fire. See QUIRK 3.
- [ ] `[HIL]` **(QUIRK 4)** Brownout: the Arduino firmware disables the brownout detector at boot to survive WiFi power spikes (`src/main.cpp:723-727`). Target: keep brownout protection **enabled** on ESP-IDF/rev2 unless bring-up shows the same symptom; decision documented at Phase 1. See QUIRK 4.
- [ ] `[HOST]` Graceful degradation: controller initializes and offers manual operation even when one or both sensors fail to initialize, as long as pump + storage are OK (`src/WateringController.cpp:75-149`)
- [ ] `[HIL]` WiFi loss never affects watering behavior (control loop independent of network — PRD NFR; verified by pulling WiFi on the rig during an active watering cycle)

## 3. Reservoir control

State machine (`src/main.cpp:492-552`, called every main-loop pass):

- [ ] `[HOST]` Feature gate: when the reservoir feature is disabled, the pump is forced off and all control logic is skipped (`src/main.cpp:493-502`, `604-615`)
- [ ] `[HOST]` Running-pump safety checks evaluated every pass: stop on high level reached; stop on max-runtime timeout (`src/main.cpp:508-528`)
- [ ] `[HOST]` Automatic level control (separately enabled, `src/main.cpp:621-624`), evaluated only when the pump is not running:
  - both sensors wet → reservoir full, stay off (`src/main.cpp:533-537`)
  - low wet + high dry → sufficient water, stay off (`src/main.cpp:538-541`)
  - both dry → start pump, record start time (`src/main.cpp:542-547`)
  - low dry + high wet → physically invalid state, no action (`src/main.cpp:549-550`)
- [ ] `[HOST]` Manual fill: refused when feature disabled or high level already reached (`src/main.cpp:559-571`); duration 0 = run until high level/timeout; duration > 0 is implemented by back-dating the start time against the 5-min max runtime, so the effective manual duration cap is 300 s (`src/main.cpp:578-584`)
- [ ] `[HOST]` Manual stop stops the pump if running (`src/main.cpp:592-598`)
- [ ] `[HOST]` Disabling the feature while the pump runs stops the pump (`src/main.cpp:604-615`)
- [ ] `[HOST]` Status query reads the level pins live and reports low/high/running (`src/main.cpp:649-665`)
- [ ] `[HIL]` Level sensor inputs: low = GPIO 32, high = GPIO 33, configured with internal pull-ups (`src/main.cpp:37-38`, `231-233`)
- [ ] `[HIL]` **Level sensor polarity — verify by measurement, not by parity.** The Arduino code *as it stands today* reads the sensors as active HIGH (water = HIGH: `src/main.cpp:504-506`, `567`, `651-656`). Note: PRD FR5 states the Arduino code reads active LOW — that described an earlier revision; the fix from the 2026-04-12 fix-branch is already merged on this branch. Polarity facts per FR5: XKC-Y26 OUT is active HIGH; rev1 (direct via TXS0108E, non-inverting) ⇒ GPIO active HIGH; rev2 (via 2N7002 inverter) ⇒ GPIO active LOW. **The checklist target is CORRECT behavior driven by board configuration (`BOARD_REV1_DEVKIT`/`BOARD_REV2` Kconfig), not bug-for-bug parity. Final polarity must be verified by measurement on the bench rig in Phase 1 and the result recorded here.**
  > **REV1 BENCH MEASUREMENT RECORD (feature 006 HIL, checklist item A —
  > DO NOT fill in from code or docs; measured values only):**
  > - Water present at sensor → GPIO level measured: `____` (expected HIGH)
  > - No water at sensor → GPIO level measured: `____` (expected LOW)
  > - `level` console output matched the physical state: `____` (yes/no)
  > - Measured by / date: `____`
- [ ] `[HIL]` Pull-up + active-HIGH consequence on rev1: a disconnected/floating sensor reads HIGH = "water present", which keeps the fill pump off (fails toward "do not pump"). Confirm equivalent fail-direction on rev2 (inverted polarity ⇒ pull state must be re-chosen per board config). (`src/main.cpp:231-233`)

## 4. Web API

Route list extracted from `src/communication/WateringSystemWebServer.cpp:83-321`. The duplicated `/api/...` and prefix-less routes are intentional legacy compatibility and both must be covered by the new firmware's behavior (the *new* contract is `/api/v1/` per FR7 — coverage, not URL-for-URL parity; the legacy frontend is minimally adapted per FR8).

**GET endpoints:**

- [ ] `[HIL]` `GET /api/sensor-data`, `/api/sensors`, `/sensors` (3 aliases) → JSON with `environmental` {temperature, humidity, pressure} and `soil` {moisture, temperature, humidity, ph, ec, + nitrogen/phosphorus/potassium only when ≥ 0}, per-section `success`/`error` fields, top-level `timestamp` (epoch) (`WateringSystemWebServer.cpp:99-117`, `372-422`). Serves cached task-read values; must not block on a fresh sensor read (see QUIRK 5 for the Arduino violation of this)
- [ ] `[HIL]` `GET /api/status`, `/status` → JSON: `pumpRunning`, `wateringEnabled`, `runTime`/`remainingTime` while running, `reservoir` {enabled, lowLevelDetected, highLevelDetected, pumpRunning, runTime, autoLevelControlEnabled}, `config` {moistureThresholdLow/High, wateringDuration, minWateringInterval}, `storage` {totalKB, usedKB, percentUsed}, `network` (STA: mode/ip/rssi/ssid/connected; AP: mode/ip/ssid/stationCount), `timestamp` (`WateringSystemWebServer.cpp:119-128`, `424-512`)
- [ ] `[HIL]` `GET /api/history`, `/history`, `/api/historical-data` (3 aliases) → query params `sensor`|`sensorId` (default `env`), `reading`|`readingType` (default `temperature`), `range` ∈ {1h, 6h, 24h, 7d, 30d} or explicit `startTime`/`endTime` (default last 24 h); response: `timestamps[]`, `values[]`, plus echo of sensorId/readingType/startTime/endTime/count (`WateringSystemWebServer.cpp:242-256`, `603-680`)
- [ ] `[HIL]` `GET /api/wifi/scan`, `/wifi/scan` → scans networks, max 20 returned, each {ssid, rssi, encryption(bool)}, plus `count`, `success`, `apMode` (`WateringSystemWebServer.cpp:270-279`, `682-709`)

**POST endpoints (form-encoded parameters — deliberately not JSON bodies, for reliability):**

- [ ] `[HIL]` `POST /api/control/water/start`, `/control/water/start` → form param `duration` (s), **default 20 s when omitted**; starts manual watering; JSON {success, message, duration} (`WateringSystemWebServer.cpp:130-189`)
- [ ] `[HIL]` `POST /api/control/water/stop`, `/control/water/stop` → stops watering; JSON {success, message} (`WateringSystemWebServer.cpp:191-208`)
- [ ] `[HIL]` `POST /api/control/auto`, `/control/auto` → form param `enabled` (or legacy `enable`), values `true`/`1`; sets automatic watering; error JSON when param missing (`WateringSystemWebServer.cpp:210-218`, `326-370`)
- [ ] `[HIL]` `POST /api/control`, `/control` → legacy command interface: `command` = `start` (optional `duration`, **default 0 = indefinite** — note: different default from `/control/water/start`) | `stop` | `enable` | `disable`; JSON {success, message} (`WateringSystemWebServer.cpp:221-230`, `514-556`)
- [ ] `[HIL]` `POST /api/config`, `/config` → form params `moistureThresholdLow`, `moistureThresholdHigh`, `wateringDuration`, `minWateringInterval` (any subset); applies via controller setters (which validate + persist); JSON {success, message} (`WateringSystemWebServer.cpp:231-240`, `558-601`)
- [ ] `[HIL]` `POST /api/reservoir`, `/reservoir` → `command` = `enable` | `disable` | `start` (optional `duration`, default 0) | `stop` | `enable-auto-level` | `disable-auto-level` | `status`; start/stop/enable-auto-level require the feature to be enabled; `status` returns nested {enabled, lowLevelDetected, highLevelDetected, pumpRunning, autoLevelControlEnabled} (`WateringSystemWebServer.cpp:258-267`, `782-909`)
- [ ] `[HIL]` `POST /api/wifi/config`, `/wifi/config` → form params `ssid` (1–32 chars), `password` (empty or ≥ 8 chars); **only functional in AP mode** (rejected otherwise); on success persists config, responds `restartRequired: true` and the device restarts ~3 s later (`WateringSystemWebServer.cpp:281-290`, `711-780`; restart scheduling `src/main.cpp:319-328`, `813-818`)

**Static files and misc:**

- [ ] `[HIL]` Static file serving from LittleFS root; default file = `index.html` in STA mode, `wifi_setup.html` in AP mode; explicit routes with cache control for index.html/wifi_setup.html/script.js/styles.css (max-age=3600) and favicon.ico (max-age=86400); pre-gzipped `.gz` variants served when the client accepts gzip (`WateringSystemWebServer.cpp:304-320`)
- [ ] `[HIL]` API routes are matched **before** the static handler (`WateringSystemWebServer.cpp:95-97`, `304-305`)
- [ ] `[HIL]` 404 handling: JSON `{"success":false,"message":"API endpoint not found"}` for `/api/*` paths, plain-text "Not found" otherwise (`WateringSystemWebServer.cpp:291-303`)
- [ ] `[HIL]` Server listens on port 80 (`src/main.cpp:49`, `64`)

> **Explicit non-item — OTA `/update`:** there is **no** `/update` (or any OTA) endpoint in the current Arduino code — the full route registration is `WateringSystemWebServer.cpp:83-321` and contains none, despite `CLAUDE.md` and older docs claiming a "/update endpoint". OTA is a **new capability** of the ESP-IDF firmware (FR11), not a parity item.

## 5. Sensors

### BME280 (environmental, I2C)

- [ ] `[HIL]` I2C address **0x77**, on I2C bus SDA=21/SCL=22 (`src/main.cpp:30-31`, `45`, `234-235`)
- [ ] `[HIL]` Sampling config: NORMAL mode, temp ×2, pressure ×16, humidity ×1, IIR filter ×16, standby 500 ms (`src/sensors/BME280Sensor.cpp:41-46`)
- [ ] `[HOST]` Reads temperature (°C), humidity (%RH), pressure (Pa→hPa, ÷100); a NaN in any value fails the read with error code (`src/sensors/BME280Sensor.cpp:53-73`)
- [ ] `[HOST]` Lazy re-init: failed sensor retries initialization on next read/availability check (`src/sensors/BME280Sensor.cpp:55-59`, `75-81`)

### Modbus soil sensor (RS485)

- [ ] `[HIL]` Device address **0x01**, Modbus RTU over UART2 at **9600 baud 8N1**, pins TX=GPIO16, RX=GPIO17, DE=GPIO25 (`src/main.cpp:32-34`, `44`, `57-58`, `238`; `include/hardware/RS485Config.h:36-39`). Note: TX/RX in `src/main.cpp:32-33` are the source of truth — `CLAUDE.md`/`docs/hardware.md` list them swapped (and `RS485Config.h:29-30` says so explicitly)
- [ ] `[HOST]` Register map as used by the code (function 0x03, one read of **9 registers starting at 0x0000**, `src/sensors/ModbusSoilSensor.cpp:83-87`; map at `include/sensors/ModbusSoilSensor.h:60-68`):
  | Reg | Meaning | Scaling applied |
  |---|---|---|
  | 0x0000 | Humidity/moisture | ÷10 → % (moisture ≡ humidity for this sensor, `ModbusSoilSensor.cpp:118-119`) |
  | 0x0001 | Temperature (signed) | ÷10 → °C (`ModbusSoilSensor.cpp:97-99`) |
  | 0x0002 | EC/conductivity | ×1 → µS/cm, × calibration factor (`ModbusSoilSensor.cpp:101-103`) |
  | 0x0003 | pH | ÷10, × calibration factor (`ModbusSoilSensor.cpp:105-107`) |
  | 0x0004–0x0006 | N, P, K | ×1 → mg/kg (`ModbusSoilSensor.cpp:109-116`) |
  | 0x0007, 0x0008 | Salinity, TDS | read but unused (`ModbusSoilSensor.h:67-68`) |
- [ ] `[HOST]` Reading validation: moisture/humidity 0–100, temperature −40–80, pH 3–9 — out-of-range fails the read (error 5); EC/NPK ranges defined but not enforced on read (`src/sensors/ModbusSoilSensor.cpp:31-39`, `121-128`)
- [ ] `[HOST]` Calibration commands for moisture/pH/EC: compute local factor from a reference value and best-effort write to sensor registers 0x0100–0x0102 via function 0x06 (write failure is non-fatal) (`src/sensors/ModbusSoilSensor.cpp:207-322`, `ModbusSoilSensor.h:74-76`)
- [ ] `[HOST]` Availability probe performs an actual 1-register bus read (`src/sensors/ModbusSoilSensor.cpp:134-145`)
- [ ] `[HOST]` Modbus client behavior (`src/communication/SP3485ModbusClient.cpp`):
  - CRC16-MODBUS (poly 0xA001, init 0xFFFF), CRC transmitted low byte first (`:74-97`, `128-131`)
  - response timeout **3000 ms** default, configurable (`RS485Config.h:52`, `SP3485ModbusClient.cpp:362-365`)
  - timeout is extended while bytes are arriving (`:153-162`)
  - tolerant response parsing: scans the first 3 received bytes for the addr+0x03 pattern to absorb timing-offset garbage bytes (`:163-203`)
  - Modbus exception responses (0x83) map to error codes 100+exception (`:213-229`)
  - write-single-register verifies the 8-byte echo (`:266-355`)
  - success/error statistics counters (`:367-378`)
  - **No automatic retry on failure** — one attempt per call; recovery comes from the 5 s read cadence. ("Retry mechanisms" in older docs overstate this.)
- [ ] `[HIL]` RS485 DE timing (rev1, TXS0108E + manual DE): DE asserted 50 µs before TX (assert delay applied twice = 100 µs effective, `SP3485ModbusClient.cpp:62-66` + `:136-137`), `flush()` then 50 µs before DE release (`:139-143`; constants `RS485Config.h:48-49`). On rev2 (THVD1426 auto-direction) DE handling is removed per board config — behavior to verify on rig (Phase 1/6): frames still complete without truncation at 9600 baud

## 6. Storage (LittleFS)

- [ ] `[HOST]` Filesystem mounted with format-on-failure (corrupted FS is auto-formatted rather than bricking the unit) (`src/main.cpp:671-681`, `src/storage/LittleFSStorage.cpp:46-50`)
- [ ] `[HOST]` Application config: single JSON object file **`/config.json`** of string key→value pairs; only key used today is `watering_config`, whose value is a JSON string with the 7 watering keys (see section 1) (`src/storage/LittleFSStorage.cpp:14-23`, `113-225`; written by `src/WateringController.cpp:195-214`)
- [ ] `[HOST]` WiFi config: separate file **`/wifi_config.json`** = `{"ssid": "...", "password": "..."}`; sentinel SSID `CONFIGURE_ME` (created as default) means unconfigured → AP mode (`src/main.cpp:50-53`, `251-329`)
- [ ] `[HOST]` Sensor history: one file per metric, **`/data/<sensorId>_<readingType>.json`** (sensorId ∈ {env, soil}), each a JSON array of `{"timestamp": <epoch>, "value": <float>}` appended every log interval (`src/storage/LittleFSStorage.cpp:102-111`, `227-276`)
- [ ] `[HOST]` History queries filter by [startTime, endTime] and return a JSON array; missing file/parse error → `[]` (`src/storage/LittleFSStorage.cpp:278-324`)
- [ ] `[HOST]` Persist-across-reboot set: watering configuration (incl. enabled flag), WiFi credentials, sensor history. **Not persisted** (resets at boot): reservoir feature enable, reservoir auto-level-control enable, manual states (`src/main.cpp:75-79` — plain globals)
- [ ] `[HOST]` Pruning of old readings exists in the storage API but is **never called** by the application — history grows until reads start failing on the 16 KB JSON parse buffer (`src/storage/LittleFSStorage.cpp:374-440`, buffer `:237`, `:298`). Target: equivalent or better retention behavior with explicit bounding (new storage design may differ internally; behavior coverage = history endpoint keeps working over time)
- [ ] `[HOST]` Storage stats (total/used bytes) available and reported in `/status` and serial status (`src/storage/LittleFSStorage.cpp:442-459`)

### Deliberate divergences in the ESP-IDF port (feature 003, PR-06)

These are intentional behavior/format changes from the Arduino storage layer,
not parity targets. The master PRD authorizes a clean redesign (no migration);
each item below is the new contract behavior, host-tested in
`firmware/test_apps/host/`.

- [ ] `[HOST]` **Configuration moves from `/config.json` to NVS** (namespace
  `wscfg`, one typed entry per item). The legacy string-keyed JSON blob is
  gone; defaults are compiled in and applied on missing/erased/out-of-range
  entries (FR-002/FR-013). Factory reset = erase the `nvs` partition. The seven
  watering items keep their legacy defaults and ranges (section 1); divergence:
  `sensorReadInterval`/`dataLogInterval` are now first-class settable items
  (legacy persisted them but exposed no setter) with new lower bounds (≥1 s /
  ≥1 min) to prevent log-storm misconfiguration.
- [ ] `[HOST]` **WiFi "unconfigured" representation changes**: legacy used the
  sentinel SSID `CONFIGURE_ME` in `/wifi_config.json`; the port stores
  credentials in NVS and represents unconfigured as an **empty SSID string**
  (factory state). PR-07 reads this for its AP-fallback decision; the legacy
  password is never reused.
- [ ] `[HOST]` **Sensor history format redesigned and explicitly bounded**:
  per-metric append-only chunk files of fixed 8-byte records
  (`/storage/hist/<metric>/<first_epoch>.dat`, 8 KiB chunks, max 10 chunks per
  metric, oldest-chunk eviction), replacing the unbounded JSON arrays. Resolves
  the legacy defect at line 172 (history grew until the 16 KB parse buffer
  failed). Retention target ≥30 days for all metrics at the default log
  interval; max 10 distinct metrics (sized to the legacy metric set), an 11th
  is rejected. Range-query behavior (inclusive filter, empty result on
  no-data/error) is preserved.
- [ ] `[HOST]` **Event log is new surface** (no legacy equivalent): rotating
  two-file log (`/storage/events/0.log`+`1.log`, 16 KiB each, oldest-half
  rotation, newest always retained) for pump/fail-safe/connectivity/OTA/reset
  events. Satisfies the constitution's "safety-relevant events MUST be
  persisted"; producers are wired in PR-08.
- [ ] `[HOST]` **Interface split**: the legacy single `IDataStorage`
  (config + history + stats, Arduino `String`) is redesigned into `IConfigStore`
  + `IDataStorage` (history + events + stats, `std::string`). The two
  never-called legacy methods (`getLastSensorReading`, `pruneOldReadings`) are
  dropped; bounded retention is an internal guarantee, not a caller obligation.
- [ ] `[HOST]` Reservoir feature flags remain **not persisted** in this PR
  (unchanged from legacy, line 171); the NVS-persistence decision is deferred to
  PR-05 (reservoir board flag). The config store is extensible per-key so PR-05
  can add them without a contract change.

### Deliberate divergences in the ESP-IDF port (feature 005, PR-03)

Intentional behavior changes from the Arduino BME280 driver (section 5), not
parity targets; each is the new contract behavior, host-tested in
`firmware/test_apps/host/main/test_bme280.cpp`.

- [ ] `[HOST]` **I2C address probing 0x76→0x77 with chip-identity check**:
  legacy hard-codes address 0x77 and never verifies the chip ID; the port
  probes both addresses, accepts only a device whose register 0xD0 reads 0x60
  (a BMP280/foreign device is logged distinctly and rejected), and re-probes
  BOTH addresses on recovery after loss — a module swapped to the other
  address while unplugged is picked up transparently (spec 005 US3/FR-004).
- [ ] `[HOST]` **Last-good getter values after a failed read**: legacy left
  NaN in the getters after a failed read; the port keeps the previous good
  values and consumers gate on the read() result / getLastError() — aligned
  with the soil-sensor contract (spec 005 FR-001/FR-007).
- [ ] `[HOST]` **`isAvailable()` is a live chip-ID probe**: legacy caches
  "available" forever after the first successful init and can report a dead
  sensor as alive; the port performs a real chip-ID read on every call, so
  the unplug/replug HIL criterion is observable (spec 005 FR-009).
- [ ] `[HOST]` **Synchronized cross-task access via `LockedEnvironmentalSensor`**:
  legacy has two unsynchronized readers on the same I2C device (main loop +
  web server); the port serializes every interface call through the mutex
  decorator — same pattern as the other Locked* wrappers (spec 005 FR-010).

### Deliberate divergences in the ESP-IDF port (feature 006, PR-05)

Intentional behavior changes from the Arduino level-sensor handling
(section 3) plus the new single-pump capability and INA226 surface; each is
the new contract behavior, host-tested in
`firmware/test_apps/host/main/test_level_sensor.cpp` /
`test_ina226.cpp` (contracts:
`specs/006-level-sensors-ina226/contracts/interfaces.md`).

- [ ] `[HOST]` **Stability-window debounce on the level inputs**: legacy
  reads the bare pins every main-loop pass (`src/main.cpp:504-506`); the
  port changes the reported logical state only after the raw input has held
  a new value for `BOARD_LEVEL_DEBOUNCE_MS` (300 ms) — any flip restarts
  the window, so chatter at the water line collapses to a single transition
  (spec 006 FR-003/SC-005).
- [ ] `[HOST]` **Explicit not-yet-valid state**: legacy has no validity
  concept — a just-booted or just-powered sensor is read as-is; the port
  gates readings behind settle time (FW-3: rev2 500 ms after power-on,
  rev1 0) plus a debounce warm-up, and reports a DISTINCT not-yet-valid
  state that consumers must never conflate with wet or dry (spec 006
  FR-001/FR-004/SC-005; PR-11 treats invalid as "do not act").
- [ ] `[HOST]` **INA226 identity check**: new capability, no legacy INA226
  at all (section 9) — the driver verifies manufacturer ID (0xFE ==
  0x5449) and die ID (0xFF == 0x2260) at initialization and rejects
  foreign devices with a distinct error, mirroring the BME280 chip-ID
  divergence above (spec 006 FR-009).
- [ ] `[HOST]` **Reservoir pump capability flag**: legacy always compiles
  both pumps; the port gates ALL reservoir-pump wiring (instance, boot
  force-OFF, console registration) behind `BOARD_HAS_RESERVOIR_PUMP`
  (rev1 = 1, rev2 = 0 — single-pump decision, master PRD FR4, final
  2026-06-10). On rev2 the pump pin macro is REMOVED so unguarded
  references fail the build; the boot invariant becomes "every pump that
  EXISTS is forced OFF first" (spec 006 FR-006/FR-007; the reservoir
  parity items in sections 2–3 remain rev1-only per section 9).

## 7. WiFi / network / time

- [ ] `[HIL]` STA connect at boot using saved credentials: STA mode, auto-reconnect off (handled manually), WiFi modem sleep disabled, clean disconnect first, **60 s** connect timeout with LED toggling every 500 ms (`src/main.cpp:46`, `168-212`)
- [ ] `[HIL]` Connection monitoring (skipped in AP mode): check every **5 s**; on loss, reconnect attempts every **10 s**; after **5** failed attempts wait an extra **60 s** before the next round; disconnect/reconnect counters and "stable" flag tracked; weak-signal warning at < −80 dBm (`src/main.cpp:102-105`, `922-968`). (Older docs call this "exponential backoff" — it is fixed-interval with a long pause, as coded.)
- [ ] `[HIL]` WiFi diagnostics printed every 10 s when in STA mode: status, SSID, IP, gateway, subnet, RSSI + quality label, channel, MAC, uptime, disconnect stats (`src/main.cpp:124-163`)
- [ ] `[HIL]` AP fallback mode: entered at boot when no/default WiFi config exists — SSID `WateringSystem-Setup`, IP **192.168.4.1** (ESP32 softAP default), serving `wifi_setup.html`; AP password is hardcoded in source (`src/main.cpp:51-52`, `334-344`, `753-769`). **Note:** hardcoded AP password is flagged in the PRD as a pre-publication security issue; provisioning method itself is an FR9 decision (Phase 2) — the *parity* requirement is: unconfigured device exposes a reachable provisioning UI at a known SSID/IP
- [ ] `[HIL]` Emergency WiFi reset: config button (GPIO 18) held **≥ 5 s during startup** (LED blinking) deletes the WiFi config, writes defaults and restarts into AP mode (`src/main.cpp:732-733`, `860-897`)
- [ ] `[HIL]` Saving WiFi config (via web) schedules a restart ~3 s later so the HTTP response can be delivered (`src/main.cpp:319-328`, `813-818`)
- [ ] `[HIL]` NTP sync after STA connect: server `0.se.pool.ntp.org`, up to 9 × 1 s wait for a plausible time (year ≥ 2020; the `++retry < 10` pre-increment loop yields at most 9 one-second waits), failure is non-fatal (`src/main.cpp:47`, `357-382`, `762-765`)
- [ ] `[HOST]` Timestamps (history, API `timestamp` fields) are epoch seconds; the Arduino firmware runs in **UTC** (no TZ configured, `src/main.cpp:359`). FR10 upgrades this to CET/CEST tz handling — coverage item: timestamps remain epoch-based and monotonic across the migration

## 8. Diagnostics

Serial commands (115200 baud, newline-terminated, case-insensitive, `src/main.cpp:973-1093`):

- [ ] `[HIL]` `soil` / `sensor` → one-shot soil sensor read printing moisture, temperature, pH, EC, NPK, or the error code (`src/main.cpp:979-990`)
- [ ] `[HIL]` `rs485test` / `test` → RS485 diagnostic: DE pin toggle test, prints the raw 8-byte Modbus request frame, manual TX with raw RX byte dump (3 s window), troubleshooting hints when silent, then a normal client read of 9 registers with per-register dump (`src/main.cpp:991-1079`)
- [ ] `[HIL]` `help` → command list; unknown commands get an error + help hint (`src/main.cpp:1080-1091`)
- [ ] `[HIL]` Periodic status block every **5 s** on serial: env readings, soil readings (NPK when ≥ 0), pump status, automatic-watering state, storage usage, WiFi state (`src/main.cpp:48`, `426-487`)

LED status patterns (status LED GPIO 2, `src/main.cpp:39`):

- [ ] `[HIL]` Solid ON during initialization, OFF when system ready (`src/main.cpp:224-225`, `791-792`)
- [ ] `[HIL]` AP mode: fast blink (toggle every 200 ms) (`src/main.cpp:835-837`)
- [ ] `[HIL]` Pump running: medium blink (toggle every 500 ms) (`src/main.cpp:838-840`)
- [ ] `[HIL]` Normal idle: 100 ms pulse every 3 s heartbeat (`src/main.cpp:841-843`)
- [ ] `[HIL]` During WiFi connect: toggle every 500 ms; during emergency-reset button hold: blink every 100 ms (`src/main.cpp:190-195`, `887-891`)

FR12 additionally requires the diagnostics to be reachable via the API — new capability, scope defined in Phase 3; the parity bar is the serial behavior above.

## 9. Out of scope for parity (new capabilities, tracked elsewhere)

- **INA226 pump current/voltage/power measurement** — rev2-only hardware, new driver (FR6); no Arduino counterpart.
- **`/api/v1/` redesign** — new API contract (FR7). Parity requires *behavior coverage* of everything in section 4, not URL-for-URL equality; the legacy frontend is adapted to the new API (FR8).
- **OTA** (A/B partitions, rollback, GitHub Releases, manual upload) — entirely new (FR11). The Arduino firmware has no OTA endpoint (see section 4 note).
- **WiFi provisioning method** — FR9 decision in Phase 2 (own SoftAP portal vs IDF `wifi_provisioning`); only the *outcome* (unconfigured device is provisionable) is a parity item (section 7).
- **Rev2 has no local reservoir pump** (FR4 decision 2026-06-10: single-pump node; refill moves to a future central reservoir unit, see `docs/feature-ideas.md`). The reservoir-control parity items in sections 2–3 are verified on the **rev1 bench rig** and remain in the firmware behind `BOARD_HAS_RESERVOIR_PUMP`; on rev2 the level sensors report status only, and the greenhouse reservoir is refilled manually until the central unit exists.

---

## Known Arduino quirks (documented, NOT parity targets)

These were found during code extraction. The ESP-IDF firmware implements the *correct* behavior; each quirk needs Paul's ack at checklist review.

1. **Auto-watering runs flagged as "manual mode"** — `WateringController::processReadings()` starts automatic watering via `waterPump->runFor()` (`src/WateringController.cpp:311`), and `runFor()` unconditionally sets `manualMode = true` (`src/actuators/WaterPump.cpp:118-119`). Consequently the sensor-failure/stale-data/invalid-reading emergency stops — all gated on `!isManualMode()` — do **not** protect an in-progress automatic watering; conversely, a user-initiated indefinite run (`command=start` without duration → `start()`, `src/actuators/WaterPump.cpp:75-76`) is flagged *automatic* and gets killed by sensor failures. In practice the 20 s timed run limits exposure. Target: mode flag follows the *initiator* (controller = automatic, user = manual).
2. **Reservoir pump GPIO not initialized at boot** — `reservoirPump.initialize()` is never called in `setup()`; GPIO 27 stays unconfigured (floating, relying on the gate pull-down resistor) until the first start command (`src/main.cpp:744-787`, `src/actuators/WaterPump.cpp:62-68`). Target: both pumps actively driven off at boot.
3. **Ineffective software watchdog** — `feedWatchdog()` then `checkWatchdog()` run consecutively in the same loop iteration (`src/main.cpp:847-851`), so the 30 s timeout can never elapse; a hung `loop()` also hangs the checker. Target: hardware `esp_task_wdt` on critical tasks.
4. **Brownout detector disabled at boot** (`src/main.cpp:726`) to mask WiFi power-spike resets on the rev1 devkit. Target: keep brownout enabled; revisit only if reproduced on the rig (rev2 has a proper power design).
5. **`/api/sensors` blocks on the Modbus bus** — despite the "use cached data, don't read" comment, `handleSensorDataRequest()` calls `soilSensor->isAvailable()` (`WateringSystemWebServer.cpp:393`) which performs a synchronous 1-register Modbus read with up to 3 s timeout (`src/sensors/ModbusSoilSensor.cpp:134-145`) in the HTTP handler context. Target: status/sensor endpoints never block on field-bus I/O.
6. **Documentation drift** (for the record, code wins): RS485 TX/RX pins are 16/17 in code but listed as 17/16 in `CLAUDE.md`/`docs/hardware.md`; "5kV optical isolation" claims refer to abandoned rev0 hardware (current design is non-isolated TXS0108E level shifting, `include/hardware/RS485Config.h:42-44`, `70`); the claimed `/update` OTA endpoint does not exist; "exponential backoff" is fixed-interval reconnect.
