# Contract: task watchdog (`task_watchdog` helper)

`firmware/main/task_watchdog.{h,cpp}`. Thin wrappers over `esp_task_wdt`; the configuration lives in
Kconfig/sdkconfig. Target-only (no host test — HIL-verified); the *policy* (which tasks, timeout margin) is
documented and reviewed.

## API

| Function | Contract |
|---|---|
| `esp_err_t watchdog_init()` | Ensure the task WDT is initialized with `CONFIG_WS_TASK_WDT_TIMEOUT_S`, panic-on-timeout enabled. Idempotent if IDF already inited it (`ESP_ERR_INVALID_STATE` treated as OK). Called once early in `app_main`. |
| `void watchdog_subscribe_current_task()` | `esp_task_wdt_add(NULL)` for the calling task; logs on failure (non-fatal). |
| `void watchdog_feed()` | `esp_task_wdt_reset()` for the calling task; called once per loop iteration. |

## Subscription policy (reviewed)

| Task | Subscribed? | Feeds where | Rationale |
|---|---|---|---|
| `app_main` 10 Hz main loop | **YES** | each loop iteration (100 ms) | drives pump/level `update()` — watering-critical |
| `sensor_task` | **YES** | each 5 s cycle | environmental read — critical; timeout must exceed 5 s |
| `wifi_task` | **NO** | — | a network stall must NOT reboot the device (PR-07 isolation, FR-014) |
| esp_console REPL | **NO** | — | blocks on UART by design; not watering-critical |
| PR-11 watering/reservoir tasks | later | via this helper | registered when they land (scope note) |

## Behavioral contract

- **Timeout** `CONFIG_WS_TASK_WDT_TIMEOUT_S` has a safe margin over the slowest subscribed cadence (5 s
  sensor task) to avoid false positives; default chosen accordingly (e.g. 15–30 s).
- **Action = reboot**: on a non-servicing subscribed task the WDT panics → reboot. At the next boot
  `pumps_force_off()` runs first (unchanged), and `esp_reset_reason()==ESP_RST_TASK_WDT` is logged
  (FR-008/FR-009).
- The IDF idle-task watchdog default must not be left misconfigured such that normal idle triggers it;
  align `CONFIG_ESP_TASK_WDT_*` in `sdkconfig.defaults` with the subscribed-task model.

## HIL verification (`checklists/hil.md`)

- Deliberately starve a subscribed task (e.g. a `while(true){}` injected behind a debug command or build
  flag on the rig) → device reboots within the timeout → pumps measured OFF immediately after reset →
  `storage events` shows a `reset=TASK_WDT` entry.
- Normal operation over an extended run triggers no spurious reboot.
