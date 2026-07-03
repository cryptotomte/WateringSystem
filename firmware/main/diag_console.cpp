// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file diag_console.cpp
 * @brief Serial diagnostic REPL implementation.
 *
 * Command grammar and response formats are normative, defined in
 * specs/002-pump-gpio-board/contracts/serial-diagnostic.md:
 *
 *   pump <plant|reservoir> start <seconds>   # timed run; 1..300
 *   pump <plant|reservoir> stop
 *   pump <plant|reservoir> status
 *   pump status                              # every existing pump
 *
 * The pump set is capability-aware (feature 006, FR-007): on boards with
 * BOARD_HAS_RESERVOIR_PUMP == 0 (rev2, single-pump node) the reservoir
 * slot is compiled out — `pump reservoir ...` is a usage error and
 * `pump status` reports exactly one pump (PR-14 contract).
 *
 * Storage commands (HIL verification path for feature 003, quickstart.md
 * steps 2-4; thin wrappers — every handler is a direct interface call):
 *
 *   config get                          # all items; credentials shown
 *                                       # only as (un)configured (FR-004)
 *   config set <item> <value>           # item = NVS key (data-model.md)
 *   config wifi <ssid> <password>       # values never echoed (FR-004)
 *   config wifi-clear
 *   config factory-reset
 *   storage stats
 *   storage log <metric> <value>        # reading at the current epoch
 *   storage query <metric> [t0 t1]      # count + newest records in range
 *   storage event <category> <detail>   # category = u8 (1..255)
 *   storage events [n]                  # newest-first, default 10
 *
 * Soil sensor commands (HIL verification path for feature 004; console
 * contract in specs/004-modbus-soil-sensor/contracts/interfaces.md — the
 * calibration commands print no factor value: no factor getter exists,
 * see cmd_soil_calibrate):
 *
 *   soil                                # one read(); 7 values or error
 *   rs485test                           # raw 1-register probe + statistics
 *   soil_cal_moisture <reference>       # calibrate against a reference
 *   soil_cal_ph <reference>             #   value; a failed calibration-
 *   soil_cal_ec <reference>             #   register write is NON-FATAL
 *
 * Environmental sensor command (HIL verification path for feature 005;
 * console contract in specs/005-bme280-i2c/contracts/interfaces.md — the
 * failure output distinguishes error 1, sensor not found, from error 2,
 * read failed — SC-006):
 *
 *   env                                 # one read(); T/RH/P or ERROR <code>
 *
 * Level sensor command (HIL verification path for feature 006; console
 * contract in specs/006-level-sensors-ina226/contracts/interfaces.md — the
 * output distinguishes "not_yet_valid" from wet/dry, FR-001/FR-003/FR-012,
 * and shows the logical + raw state per sensor):
 *
 *   level                               # both sensors: logical + raw + validity
 *
 * Power telemetry command (feature 006, BOARD_HAS_INA226 builds only; the
 * PR-14 bring-up path — the failure output distinguishes error 1, sensor
 * not found, from error 2, read failed):
 *
 *   power                               # one read(); V/I/P or ERROR <code>
 *
 * Handler exit codes follow the esp_console convention: 0 on OK, 1 on ERR.
 *
 * State is plain pointers/PODs set from app_main — no non-trivial static
 * constructors (they would run before the boot pump fail-safe).
 */

#include "diag_console.h"

#include <cmath>
#include <ctime>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "esp_console.h"

#include "board/board.h"
#include "interfaces/IConfigStore.h"
#include "interfaces/IDataStorage.h"
#include "interfaces/IEnvironmentalSensor.h"
#include "interfaces/ILevelSensor.h"
#include "interfaces/IModbusClient.h"
#include "interfaces/IPowerSensor.h"
#include "interfaces/ISoilSensor.h"
#include "interfaces/IWaterPump.h"

namespace {

/// Console-side bookkeeping per pump (interface has no duration getter;
/// the requested duration is recorded here on every accepted start).
struct PumpSlot {
    const char *name;
    IWaterPump *pump;
    int lastStartedDurationS;
};

// One slot per pump that EXISTS on this board (feature 006 capability
// flag): single-pump boards compile the reservoir slot out entirely, so
// `pump reservoir ...` is rejected as usage (the name matches no slot) and
// `pump status` iterates exactly the existing pumps. Trivially
// initialized — safe before app_main (no static constructors).
PumpSlot s_slots[] = {
    {"plant", nullptr, 0},
#if BOARD_HAS_RESERVOIR_PUMP
    {"reservoir", nullptr, 0},
#endif
};

/// Pump command grammar for this board (usage + help text stay truthful:
/// a rev2 operator is never offered a `reservoir` word that cannot exist).
#if BOARD_HAS_RESERVOIR_PUMP
constexpr char kPumpHelp[] =
    "pump <plant|reservoir> <start <seconds>|stop|status> | pump status";
#else
constexpr char kPumpHelp[] =
    "pump <plant> <start <seconds>|stop|status> | pump status";
#endif

// Storage instances (set from app_main; expected to be the Locked*
// decorators — the handlers run on the REPL task, FR-013). Trivially
// initialized pointers, same rule as s_slots.
IConfigStore *s_config = nullptr;
IDataStorage *s_storage = nullptr;

// Soil sensor + Modbus client (set from app_main; the sensor is expected
// to be the LockedSoilSensor decorator). Same trivial-initialization rule.
ISoilSensor *s_soil = nullptr;
IModbusClient *s_modbus = nullptr;

// Environmental sensor (set from app_main; expected to be the
// LockedEnvironmentalSensor decorator). Same trivial-initialization rule.
IEnvironmentalSensor *s_env = nullptr;

// Level sensors (set from app_main; expected to be the LockedLevelSensor
// decorators — the handler runs on the REPL task, concurrently with the
// main-loop update()). Same trivial-initialization rule.
ILevelSensor *s_level_low = nullptr;
ILevelSensor *s_level_high = nullptr;

#if BOARD_HAS_INA226
// Power sensor (set from app_main; expected to be the LockedPowerSensor
// decorator). Same trivial-initialization rule. Compiled out together
// with the `power` command on boards without an INA226.
IPowerSensor *s_power = nullptr;
#endif

const char *stop_reason_str(StopReason reason)
{
    switch (reason) {
    case StopReason::Commanded:
        return "commanded";
    case StopReason::DurationElapsed:
        return "duration_elapsed";
    case StopReason::MaxRuntimeForced:
        return "max_runtime_forced";
    case StopReason::None:
    default:
        return "none";
    }
}

void print_status(const PumpSlot &slot)
{
    if (slot.pump->isRunning()) {
        printf("%s: running %.1f/%.1f s\n", slot.name,
               static_cast<double>(slot.pump->getCurrentRunTimeMs()) / 1000.0,
               static_cast<double>(slot.lastStartedDurationS));
    } else {
        printf("%s: stopped, last stop=%s, total runtime %.1f s\n", slot.name,
               stop_reason_str(slot.pump->getLastStopReason()),
               static_cast<double>(slot.pump->getAccumulatedRunTimeMs()) /
                   1000.0);
    }
}

int print_usage(void)
{
    printf("ERR usage: %s\n", kPumpHelp);
    return 1;
}

int cmd_start(PumpSlot &slot, const char *secondsArg)
{
    char *end = nullptr;
    const long seconds = strtol(secondsArg, &end, 10);
    if (end == secondsArg || *end != '\0' || seconds < 1 || seconds > 300) {
        printf("ERR duration must be 1..300 s\n");
        return 1;
    }
    if (slot.pump->isRunning()) {
        printf("ERR %s already running (%lld s elapsed)\n", slot.name,
               static_cast<long long>(slot.pump->getCurrentRunTimeMs() /
                                      1000));
        return 1;
    }
    if (!slot.pump->runFor(static_cast<int>(seconds))) {
        // Range and running state are pre-checked above; reaching this
        // means a driver-level failure.
        printf("ERR %s failed to start\n", slot.name);
        return 1;
    }
    slot.lastStartedDurationS = static_cast<int>(seconds);
    printf("OK %s running for %d s\n", slot.name, static_cast<int>(seconds));
    return 0;
}

int cmd_stop(PumpSlot &slot)
{
    if (!slot.pump->isRunning()) {
        printf("OK %s already stopped\n", slot.name);
        return 0;
    }
    const int64_t ranMs = slot.pump->getCurrentRunTimeMs();
    slot.pump->stop();
    printf("OK %s stopped (reason=commanded, ran %.1f s)\n", slot.name,
           static_cast<double>(ranMs) / 1000.0);
    return 0;
}

int pump_cmd(int argc, char **argv)
{
    // "pump status" — both pumps.
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        for (PumpSlot &slot : s_slots) {
            if (slot.pump != nullptr) {
                print_status(slot);
            }
        }
        return 0;
    }
    if (argc < 3) {
        return print_usage();
    }

    PumpSlot *slot = nullptr;
    for (PumpSlot &candidate : s_slots) {
        if (strcmp(argv[1], candidate.name) == 0) {
            slot = &candidate;
            break;
        }
    }
    if (slot == nullptr || slot->pump == nullptr) {
        return print_usage();
    }

    if (strcmp(argv[2], "start") == 0 && argc == 4) {
        return cmd_start(*slot, argv[3]);
    }
    if (strcmp(argv[2], "stop") == 0 && argc == 3) {
        return cmd_stop(*slot);
    }
    if (strcmp(argv[2], "status") == 0 && argc == 3) {
        print_status(*slot);
        return 0;
    }
    return print_usage();
}

// --- config / storage commands (feature 003 HIL verification path) ------

/// Strict decimal u32 parse; false on garbage/overflow.
bool parse_u32(const char *arg, uint32_t &out)
{
    char *end = nullptr;
    const unsigned long value = strtoul(arg, &end, 10);
    if (end == arg || *end != '\0' || value > UINT32_MAX) {
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

/// Strict float parse; false on garbage or non-finite input — nan/inf never
/// reach a float consumer (finite range checks are the consumer's job).
bool parse_float(const char *arg, float &out)
{
    char *end = nullptr;
    out = strtof(arg, &end);
    return end != arg && *end == '\0' && std::isfinite(out);
}

void print_config(const IConfigStore &config)
{
    printf("moist_low=%.2f %%\n",
           static_cast<double>(config.getMoistureThresholdLow()));
    printf("moist_high=%.2f %%\n",
           static_cast<double>(config.getMoistureThresholdHigh()));
    printf("water_dur=%lu s\n",
           static_cast<unsigned long>(config.getWateringDurationS()));
    printf("soak_pause=%lu s\n",
           static_cast<unsigned long>(config.getMinWateringIntervalS()));
    printf("water_en=%d\n", config.getWateringEnabled() ? 1 : 0);
    printf("read_iv=%lu ms\n",
           static_cast<unsigned long>(config.getSensorReadIntervalMs()));
    printf("log_iv=%lu ms\n",
           static_cast<unsigned long>(config.getDataLogIntervalMs()));
    // Credential VALUES never appear in diagnostic output (FR-004).
    printf("wifi=%s\n",
           config.getWifiSsid().empty() ? "unconfigured" : "configured");
}

int print_config_usage(void)
{
    printf("ERR usage: config <get|set <item> <value>|wifi <ssid> <password>"
           "|wifi-clear|factory-reset>\n");
    return 1;
}

/// `config set <item> <value>`: items are the NVS keys (data-model.md);
/// the store validates and persists — rejection means out-of-range input
/// or a persistence failure.
int cmd_config_set(IConfigStore &config, const char *item, const char *value)
{
    bool ok = false;
    if (strcmp(item, "moist_low") == 0 || strcmp(item, "moist_high") == 0) {
        float parsed = 0.0f;
        if (!parse_float(value, parsed)) {
            printf("ERR %s: not a number\n", item);
            return 1;
        }
        ok = (strcmp(item, "moist_low") == 0)
                 ? config.setMoistureThresholdLow(parsed)
                 : config.setMoistureThresholdHigh(parsed);
    } else if (strcmp(item, "water_en") == 0) {
        if (strcmp(value, "0") != 0 && strcmp(value, "1") != 0) {
            printf("ERR water_en: value must be 0 or 1\n");
            return 1;
        }
        ok = config.setWateringEnabled(strcmp(value, "1") == 0);
    } else {
        uint32_t parsed = 0;
        if (!parse_u32(value, parsed)) {
            printf("ERR %s: not an unsigned integer\n", item);
            return 1;
        }
        if (strcmp(item, "water_dur") == 0) {
            ok = config.setWateringDurationS(parsed);
        } else if (strcmp(item, "soak_pause") == 0) {
            ok = config.setMinWateringIntervalS(parsed);
        } else if (strcmp(item, "read_iv") == 0) {
            ok = config.setSensorReadIntervalMs(parsed);
        } else if (strcmp(item, "log_iv") == 0) {
            ok = config.setDataLogIntervalMs(parsed);
        } else {
            printf("ERR unknown item '%s' (moist_low moist_high water_dur "
                   "soak_pause water_en read_iv log_iv)\n",
                   item);
            return 1;
        }
    }
    if (!ok) {
        printf("ERR %s rejected (out of range or storage failure)\n", item);
        return 1;
    }
    printf("OK %s=%s\n", item, value);
    return 0;
}

int config_cmd(int argc, char **argv)
{
    if (s_config == nullptr) {
        printf("ERR config store not available\n");
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "get") == 0) {
        print_config(*s_config);
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "set") == 0) {
        return cmd_config_set(*s_config, argv[2], argv[3]);
    }
    if (argc == 4 && strcmp(argv[1], "wifi") == 0) {
        // Never echo the values back (FR-004).
        if (!s_config->setWifiCredentials(argv[2], argv[3])) {
            printf("ERR credentials rejected (ssid <= 32, password <= 64 "
                   "bytes, or storage failure)\n");
            return 1;
        }
        printf("OK wifi credentials stored\n");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "wifi-clear") == 0) {
        if (!s_config->clearWifiCredentials()) {
            printf("ERR wifi clear failed\n");
            return 1;
        }
        printf("OK wifi credentials cleared\n");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "factory-reset") == 0) {
        if (!s_config->factoryReset()) {
            printf("ERR factory reset failed\n");
            return 1;
        }
        printf("OK factory defaults restored\n");
        return 0;
    }
    return print_config_usage();
}

int print_storage_usage(void)
{
    printf("ERR usage: storage <stats|log <metric> <value>|query <metric> "
           "[t0 t1]|event <category> <detail>|events [n]>\n");
    return 1;
}

int storage_cmd(int argc, char **argv)
{
    if (s_storage == nullptr) {
        printf("ERR data storage not available\n");
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "stats") == 0) {
        const StorageStats stats = s_storage->getStorageStats();
        printf("OK total=%lu B used=%lu B (%lu%%)\n",
               static_cast<unsigned long>(stats.totalBytes),
               static_cast<unsigned long>(stats.usedBytes),
               static_cast<unsigned long>(
                   stats.totalBytes != 0
                       ? (100ULL * stats.usedBytes) / stats.totalBytes
                       : 0));
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "log") == 0) {
        float value = 0.0f;
        if (!parse_float(argv[3], value)) {
            printf("ERR value: not a number\n");
            return 1;
        }
        const uint32_t epoch = static_cast<uint32_t>(time(nullptr));
        if (!s_storage->storeSensorReading(argv[2], epoch, value)) {
            printf("ERR reading rejected (metric cap or storage failure)\n");
            return 1;
        }
        printf("OK %s %lu %.3f\n", argv[2], static_cast<unsigned long>(epoch),
               static_cast<double>(value));
        return 0;
    }
    if ((argc == 3 || argc == 5) && strcmp(argv[1], "query") == 0) {
        uint32_t t0 = 0;
        uint32_t t1 = UINT32_MAX;
        if (argc == 5 &&
            (!parse_u32(argv[3], t0) || !parse_u32(argv[4], t1))) {
            printf("ERR t0/t1: not unsigned integers\n");
            return 1;
        }
        const std::vector<SensorReading> readings =
            s_storage->getSensorReadings(argv[2], t0, t1);
        printf("OK %u readings\n", static_cast<unsigned>(readings.size()));
        // Bounded output: the newest 10 are enough for the HIL checks.
        const std::size_t first =
            readings.size() > 10 ? readings.size() - 10 : 0;
        for (std::size_t i = first; i < readings.size(); ++i) {
            printf("%lu %.3f\n",
                   static_cast<unsigned long>(readings[i].epoch),
                   static_cast<double>(readings[i].value));
        }
        return 0;
    }
    if (argc >= 4 && strcmp(argv[1], "event") == 0) {
        uint32_t category = 0;
        if (!parse_u32(argv[2], category) || category == 0 ||
            category > UINT8_MAX) {
            printf("ERR category must be 1..255\n");
            return 1;
        }
        // Re-join the detail words (the console splits on spaces).
        std::string detail;
        for (int i = 3; i < argc; ++i) {
            if (!detail.empty()) {
                detail += ' ';
            }
            detail += argv[i];
        }
        const uint32_t epoch = static_cast<uint32_t>(time(nullptr));
        if (!s_storage->storeEvent(epoch, static_cast<uint8_t>(category),
                                   detail)) {
            printf("ERR event rejected (storage failure)\n");
            return 1;
        }
        printf("OK event %lu cat=%lu\n", static_cast<unsigned long>(epoch),
               static_cast<unsigned long>(category));
        return 0;
    }
    if ((argc == 2 || argc == 3) && strcmp(argv[1], "events") == 0) {
        uint32_t maxCount = 10;
        if (argc == 3 && (!parse_u32(argv[2], maxCount) || maxCount == 0)) {
            printf("ERR n must be a positive integer\n");
            return 1;
        }
        const std::vector<EventRecord> events = s_storage->getEvents(maxCount);
        printf("OK %u events (newest first)\n",
               static_cast<unsigned>(events.size()));
        for (const EventRecord &event : events) {
            printf("%lu cat=%u %s\n",
                   static_cast<unsigned long>(event.epoch),
                   static_cast<unsigned>(event.category),
                   event.detail.c_str());
        }
        return 0;
    }
    return print_storage_usage();
}

// --- soil / rs485test commands (feature 004 HIL verification path) ------

/// Error-code names per the data-model.md error table (feature 004).
const char *soil_error_str(int error)
{
    switch (error) {
    case 0:
        return "ok";
    case 1:
        return "not_initialized";
    case 2:
        return "bus_error";
    case 3:
        return "timeout";
    case 5:
        return "range_validation";
    default:
        return error >= 100 ? "slave_exception" : "unknown";
    }
}

/// `soil`: one read() through the locked sensor; all 7 values or the error.
int soil_cmd(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("ERR usage: soil\n");
        return 1;
    }
    if (s_soil == nullptr) {
        printf("ERR soil sensor not available\n");
        return 1;
    }
    if (!s_soil->read()) {
        const int error = s_soil->getLastError();
        printf("ERR read failed: error %d (%s)\n", error,
               soil_error_str(error));
        return 1;
    }
    printf("OK moisture=%.1f %% temp=%.1f C ec=%.0f uS/cm ph=%.1f "
           "n=%.0f mg/kg p=%.0f mg/kg k=%.0f mg/kg\n",
           static_cast<double>(s_soil->getMoisture()),
           static_cast<double>(s_soil->getTemperature()),
           static_cast<double>(s_soil->getEC()),
           static_cast<double>(s_soil->getPH()),
           static_cast<double>(s_soil->getNitrogen()),
           static_cast<double>(s_soil->getPhosphorus()),
           static_cast<double>(s_soil->getPotassium()));
    return 0;
}

/// `rs485test`: one raw 1-register probe (slave 0x01, register 0x0000 —
/// the parity availability probe) + cumulative transaction statistics.
///
/// TODO(PR-11): this drives the raw, unsynchronized EspModbusClient BELOW
/// LockedSoilSensor's mutex; once PR-11 adds the main-loop reader this
/// becomes a cross-task race — route it through a locked client wrapper
/// (or through the sensor) then.
int rs485test_cmd(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("ERR usage: rs485test\n");
        return 1;
    }
    if (s_modbus == nullptr) {
        printf("ERR modbus client not available\n");
        return 1;
    }
    uint16_t value = 0;
    const bool ok = s_modbus->readHoldingRegisters(0x01, 0x0000, 1, &value);
    uint32_t successCount = 0;
    uint32_t errorCount = 0;
    s_modbus->getStatistics(&successCount, &errorCount);
    if (ok) {
        printf("OK reg 0x0000 = %u, stats: success=%lu error=%lu\n",
               static_cast<unsigned>(value),
               static_cast<unsigned long>(successCount),
               static_cast<unsigned long>(errorCount));
        return 0;
    }
    const int error = s_modbus->getLastError();
    printf("ERR probe failed: error %d (%s), stats: success=%lu error=%lu\n",
           error, soil_error_str(error),
           static_cast<unsigned long>(successCount),
           static_cast<unsigned long>(errorCount));
    return 1;
}

/// Shared `soil_cal_*` flow: one calibrate*() call through the locked
/// sensor. ModbusSoilSensor exposes no factor getter (factors are private,
/// RAM-only for now), so the output reports success/failure plus the
/// legacy write-result semantics: a true return with a non-zero last error
/// means the factor is applied locally but the best-effort write to the
/// sensor's calibration register failed (NON-FATAL, parity).
int cmd_soil_calibrate(int argc, char **argv, const char *name,
                       bool (ISoilSensor::*calibrate)(float))
{
    if (s_soil == nullptr) {
        printf("ERR soil sensor not available\n");
        return 1;
    }
    if (argc != 2) {
        printf("ERR usage: %s <reference-value>\n", name);
        return 1;
    }
    float reference = 0.0f;
    if (!parse_float(argv[1], reference)) {
        printf("ERR reference-value: not a number\n");
        return 1;
    }
    if (!(s_soil->*calibrate)(reference)) {
        const int error = s_soil->getLastError();
        printf("ERR calibration failed: error %d (%s)\n", error,
               soil_error_str(error));
        return 1;
    }
    const int error = s_soil->getLastError();
    if (error != 0) {
        printf("OK calibration applied (sensor register write failed, "
               "non-fatal: error %d (%s))\n",
               error, soil_error_str(error));
    } else {
        printf("OK calibration applied\n");
    }
    return 0;
}

int soil_cal_moisture_cmd(int argc, char **argv)
{
    return cmd_soil_calibrate(argc, argv, "soil_cal_moisture",
                              &ISoilSensor::calibrateMoisture);
}

int soil_cal_ph_cmd(int argc, char **argv)
{
    return cmd_soil_calibrate(argc, argv, "soil_cal_ph",
                              &ISoilSensor::calibratePH);
}

int soil_cal_ec_cmd(int argc, char **argv)
{
    return cmd_soil_calibrate(argc, argv, "soil_cal_ec",
                              &ISoilSensor::calibrateEC);
}

// --- env command (feature 005 HIL verification path) ---------------------

/// `env`: one read() through the locked sensor; thin wrapper, no logic.
/// Failure output is the binding contract format `ERROR <code>` with a
/// hint distinguishing error 1 (sensor not found — check wiring/address)
/// from error 2 (read failed — sensor present but communication or data
/// broke) — SC-006.
int env_cmd(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("ERR usage: env\n");
        return 1;
    }
    if (s_env == nullptr) {
        printf("ERR environmental sensor not available\n");
        return 1;
    }
    if (!s_env->read()) {
        // read() and getLastError() are separate locked calls; a sensor-
        // task poll interleaving between them can succeed and reset the
        // error to 0 (benign cross-lock race, TODO(PR-11) snapshot helper
        // in LockedEnvironmentalSensor.h) — hint accordingly.
        const int error = s_env->getLastError();
        const char *hint = (error == 1)   ? "sensor not found"
                           : (error == 2) ? "read failed"
                           : (error == 0)
                               ? "state changed concurrently - retry"
                               : "unknown error";
        printf("ERROR %d (%s)\n", error, hint);
        return 1;
    }
    printf("OK temperature=%.1f C humidity=%.1f %%RH pressure=%.1f hPa\n",
           static_cast<double>(s_env->getTemperature()),
           static_cast<double>(s_env->getHumidity()),
           static_cast<double>(s_env->getPressure()));
    return 0;
}

// --- level command (feature 006 HIL verification path) -------------------

/// One sensor's console word: "not_yet_valid" is a DISTINCT state, never
/// conflated with wet/dry (FR-001/FR-003/FR-012 — a settling or warming-up
/// sensor must not read as an empty or full reservoir).
const char *level_state_str(ILevelSensor &sensor)
{
    // isValid() and isWaterPresent() are separate locked calls; a main-loop
    // update() interleaving between them can only make a just-valid reading
    // report not_yet_valid, or a reading that lost validity in the gap
    // print "dry" — benign either way, because isWaterPresent() returns
    // false whenever invalid (ILevelSensor contract): never a stale or
    // phantom "water". One-poll diagnostic glitch only; TODO(PR-11):
    // migrate to the snapshot helper in LockedLevelSensor.h (PR-14 keeps
    // relying on this console path).
    if (!sensor.isValid()) {
        return "not_yet_valid";
    }
    return sensor.isWaterPresent() ? "water" : "dry";
}

/// `level`: both sensors' logical + raw + validity; thin wrapper, no logic.
/// The raw pin level is diagnostics (polarity NOT absorbed — see board.h).
int level_cmd(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("ERR usage: level\n");
        return 1;
    }
    if (s_level_low == nullptr || s_level_high == nullptr) {
        printf("ERR level sensors not available\n");
        return 1;
    }
    printf("OK low=%s (raw=%d) high=%s (raw=%d)\n",
           level_state_str(*s_level_low), s_level_low->rawState() ? 1 : 0,
           level_state_str(*s_level_high), s_level_high->rawState() ? 1 : 0);
    return 0;
}

#if BOARD_HAS_INA226

// --- power command (feature 006, PR-14 bring-up path) ---------------------

/// `power`: one read() through the locked sensor; thin wrapper, no logic.
/// Failure output is the binding contract format `ERROR <code>` with a
/// hint distinguishing error 1 (sensor not found — no ACK at 0x40 or a
/// foreign device failed the identity check) from error 2 (read failed —
/// device identified but communication broke).
int power_cmd(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("ERR usage: power\n");
        return 1;
    }
    if (s_power == nullptr) {
        printf("ERR power sensor not available\n");
        return 1;
    }
    if (!s_power->read()) {
        // read() and getLastError() are separate locked calls; when
        // PR-09/PR-11 add readers an interleaving read can reset the
        // error to 0 between them (benign cross-lock race, TODO(PR-11)
        // snapshot helper in LockedPowerSensor.h) — hint accordingly.
        const int error = s_power->getLastError();
        const char *hint = (error == 1)   ? "sensor not found"
                           : (error == 2) ? "read failed"
                           : (error == 0)
                               ? "state changed concurrently - retry"
                               : "unknown error";
        printf("ERROR %d (%s)\n", error, hint);
        return 1;
    }
    printf("OK bus=%.3f V current=%.3f A power=%.3f W\n",
           static_cast<double>(s_power->getBusVoltage()),
           static_cast<double>(s_power->getCurrent()),
           static_cast<double>(s_power->getPower()));
    return 0;
}

#endif  // BOARD_HAS_INA226

}  // namespace

#if BOARD_HAS_RESERVOIR_PUMP
void diag_console_register_pumps(IWaterPump& plant, IWaterPump& reservoir)
{
    s_slots[0].pump = &plant;
    s_slots[1].pump = &reservoir;
}
#else
void diag_console_register_pumps(IWaterPump& plant)
{
    s_slots[0].pump = &plant;
}
#endif

void diag_console_register_storage(IConfigStore& config, IDataStorage& storage)
{
    s_config = &config;
    s_storage = &storage;
}

void diag_console_register_soil(ISoilSensor& sensor, IModbusClient& client)
{
    s_soil = &sensor;
    s_modbus = &client;
}

void diag_console_register_env(IEnvironmentalSensor& sensor)
{
    s_env = &sensor;
}

void diag_console_register_level(ILevelSensor& low, ILevelSensor& high)
{
    s_level_low = &low;
    s_level_high = &high;
}

#if BOARD_HAS_INA226
void diag_console_register_power(IPowerSensor& sensor)
{
    s_power = &sensor;
}
#endif

esp_err_t diag_console_start(void)
{
    esp_console_repl_t *repl = nullptr;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "ws>";

    esp_console_dev_uart_config_t uart_config =
        ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    esp_err_t err =
        esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
    if (err != ESP_OK) {
        return err;
    }

    const esp_console_cmd_t cmd = {
        .command = "pump",
        .help = kPumpHelp,
        .hint = nullptr,
        .func = &pump_cmd,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    err = esp_console_cmd_register(&cmd);
    if (err != ESP_OK) {
        return err;
    }

    const esp_console_cmd_t cmd_config = {
        .command = "config",
        .help = "config <get|set <item> <value>|wifi <ssid> <password>"
                "|wifi-clear|factory-reset>",
        .hint = nullptr,
        .func = &config_cmd,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    err = esp_console_cmd_register(&cmd_config);
    if (err != ESP_OK) {
        return err;
    }

    const esp_console_cmd_t cmd_storage = {
        .command = "storage",
        .help = "storage <stats|log <metric> <value>|query <metric> [t0 t1]"
                "|event <category> <detail>|events [n]>",
        .hint = nullptr,
        .func = &storage_cmd,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    err = esp_console_cmd_register(&cmd_storage);
    if (err != ESP_OK) {
        return err;
    }

    const esp_console_cmd_t cmd_soil = {
        .command = "soil",
        .help = "soil — one soil sensor read (7 values or error code)",
        .hint = nullptr,
        .func = &soil_cmd,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    err = esp_console_cmd_register(&cmd_soil);
    if (err != ESP_OK) {
        return err;
    }

    const esp_console_cmd_t cmd_rs485test = {
        .command = "rs485test",
        .help = "rs485test — raw 1-register Modbus probe + statistics",
        .hint = nullptr,
        .func = &rs485test_cmd,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    err = esp_console_cmd_register(&cmd_rs485test);
    if (err != ESP_OK) {
        return err;
    }

    const esp_console_cmd_t cmd_soil_cal_moisture = {
        .command = "soil_cal_moisture",
        .help = "soil_cal_moisture <reference-value> — calibrate moisture "
                "against a reference in %",
        .hint = nullptr,
        .func = &soil_cal_moisture_cmd,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    err = esp_console_cmd_register(&cmd_soil_cal_moisture);
    if (err != ESP_OK) {
        return err;
    }

    const esp_console_cmd_t cmd_soil_cal_ph = {
        .command = "soil_cal_ph",
        .help = "soil_cal_ph <reference-value> — calibrate pH against a "
                "reference",
        .hint = nullptr,
        .func = &soil_cal_ph_cmd,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    err = esp_console_cmd_register(&cmd_soil_cal_ph);
    if (err != ESP_OK) {
        return err;
    }

    const esp_console_cmd_t cmd_soil_cal_ec = {
        .command = "soil_cal_ec",
        .help = "soil_cal_ec <reference-value> — calibrate EC against a "
                "reference in uS/cm",
        .hint = nullptr,
        .func = &soil_cal_ec_cmd,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    err = esp_console_cmd_register(&cmd_soil_cal_ec);
    if (err != ESP_OK) {
        return err;
    }

    const esp_console_cmd_t cmd_env = {
        .command = "env",
        .help = "env — one environmental sensor read (T/RH/P or error code)",
        .hint = nullptr,
        .func = &env_cmd,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    err = esp_console_cmd_register(&cmd_env);
    if (err != ESP_OK) {
        return err;
    }

    const esp_console_cmd_t cmd_level = {
        .command = "level",
        .help = "level — both level sensors: logical + raw state "
                "(not_yet_valid distinct from water/dry)",
        .hint = nullptr,
        .func = &level_cmd,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    err = esp_console_cmd_register(&cmd_level);
    if (err != ESP_OK) {
        return err;
    }

#if BOARD_HAS_INA226
    const esp_console_cmd_t cmd_power = {
        .command = "power",
        .help = "power — one INA226 read (bus V / current A / power W or "
                "error code)",
        .hint = nullptr,
        .func = &power_cmd,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    err = esp_console_cmd_register(&cmd_power);
    if (err != ESP_OK) {
        return err;
    }
#endif

    return esp_console_start_repl(repl);
}
