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
 *   pump status                              # both pumps
 *
 * Handler exit codes follow the esp_console convention: 0 on OK, 1 on ERR.
 *
 * State is plain pointers/PODs set from app_main — no non-trivial static
 * constructors (they would run before the boot pump fail-safe).
 */

#include "diag_console.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_console.h"

#include "interfaces/IWaterPump.h"

namespace {

/// Console-side bookkeeping per pump (interface has no duration getter;
/// the requested duration is recorded here on every accepted start).
struct PumpSlot {
    const char *name;
    IWaterPump *pump;
    int lastStartedDurationS;
};

// Trivially initialized — safe before app_main (no static constructors).
PumpSlot s_slots[2] = {
    {"plant", nullptr, 0},
    {"reservoir", nullptr, 0},
};

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
    printf("ERR usage: pump <plant|reservoir> <start <seconds>|stop|status> "
           "| pump status\n");
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

}  // namespace

void diag_console_register_pumps(IWaterPump& plant, IWaterPump& reservoir)
{
    s_slots[0].pump = &plant;
    s_slots[1].pump = &reservoir;
}

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
        .help = "pump <plant|reservoir> <start <seconds>|stop|status> "
                "| pump status",
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

    return esp_console_start_repl(repl);
}
