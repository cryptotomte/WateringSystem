/**
 * @file app_main.cpp
 * @brief WateringSystem firmware entry point.
 *
 * The very first action at boot is to force every pump output that exists
 * on this board to a safe OFF state. This fail-safe MUST stay first in
 * app_main in all future phases: pumps are always off after power-on,
 * watchdog reset and OTA restart, before any other initialization runs.
 * The pump set is capability-aware (feature 006): rev1 has two pumps,
 * rev2 is a single-pump node (BOARD_HAS_RESERVOIR_PUMP = 0) — the
 * invariant "every pump that exists is OFF first" is unchanged.
 *
 * Corollary: no translation unit in this firmware may contain non-trivial
 * static/global constructors — they run before app_main and would execute
 * ahead of (and thus bypass) this fail-safe. Keep all initialization
 * explicit, inside or after pumps_force_off().
 *
 * Hardware dependency: the pump MOSFET gate pull-down resistors must hold
 * both pumps off while the GPIOs are hi-Z, i.e. during boot ROM /
 * bootloader execution and after an abort() below. The software fail-safe
 * narrows the hi-Z window; the pull-downs cover it.
 */

#include "board/board.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "actuators/EspTimeProvider.h"
#include "actuators/GpioWaterPump.h"
#include "actuators/LockedWaterPump.h"
#include "sensors/Bme280Sensor.h"
#include "sensors/DebouncedLevelSensor.h"
#include "sensors/EspI2cBus.h"
#include "sensors/EspModbusClient.h"
#include "sensors/GpioLevelSensor.h"
#include "sensors/LockedEnvironmentalSensor.h"
#include "sensors/LockedLevelSensor.h"
#include "sensors/LockedSoilSensor.h"
#include "sensors/ModbusSoilSensor.h"
#if BOARD_HAS_INA226
// INA226 headers only on equipped boards: Ina226Sensor.cpp is not in the
// rev1 target build at all (sensors/CMakeLists.txt) — FR-011.
#include "sensors/Ina226Sensor.h"
#include "sensors/LockedPowerSensor.h"
#endif
#include "network/EspWifiDriver.h"
#include "network/ProvisioningPortal.h"
#include "network/WifiBootMode.h"
#include "network/WifiManager.h"
#include "network/WifiState.h"
#include "storage/LittleFsDataStorage.h"
#include "storage/LockedConfigStore.h"
#include "storage/LockedDataStorage.h"
#include "storage/NvsConfigStore.h"
#include "storage/StorageMount.h"

#include "diag_console.h"
#include "sensor_task.h"
#include "wifi_task.h"

static const char *TAG = "app_main";

// Config-button emergency-provisioning hold (feature 007, US3). Parity
// (docs/parity-checklist.md §7): the config button held >= 5 s during startup
// forces WiFi provisioning; the status LED blinks every 100 ms while the hold
// is being confirmed so the operator sees the reset registering (§7/§9). This
// 100 ms button-hold blink is distinct from the wifi task's 500 ms
// connect-attempt toggle (that one runs later, from wifi_task.cpp).
static constexpr uint32_t kConfigButtonHoldMs = 5000;   // hold to force prov.
static constexpr uint32_t kConfigButtonBlinkMs = 100;   // LED toggle interval

/**
 * @brief Drive every pump GPIO that exists on this board to a safe OFF
 * state (output, level 0).
 *
 * Capability-aware (feature 006, FR-007): on two-pump boards (rev1) both
 * the plant and the reservoir pump are forced off; on single-pump boards
 * (BOARD_HAS_RESERVOIR_PUMP == 0, rev2) exactly the plant pump is — the
 * reservoir pin does not exist there and any unguarded reference is a
 * compile error (board.h enforcement pattern).
 *
 * Pumps are switched by N-channel MOSFET gates and are active high,
 * so level 0 means pump off.
 *
 * Error handling is explicit (not ESP_ERROR_CHECK): this safety check must
 * fire regardless of the configured assertion level (ESP_ERROR_CHECK
 * degrades to a no-op when assertions are disabled via NDEBUG).
 */
static void pumps_force_off(void)
{
    uint64_t pump_mask = 1ULL << BOARD_PIN_MAIN_PUMP;
#if BOARD_HAS_RESERVOIR_PUMP
    pump_mask |= 1ULL << BOARD_PIN_RESERVOIR_PUMP;
#endif
    const gpio_config_t pump_cfg = {
        .pin_bit_mask = pump_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    // Set output levels to 0 BEFORE switching the pins to output mode, so
    // the pins never glitch high when the output driver is enabled.
    esp_err_t err = gpio_set_level(static_cast<gpio_num_t>(BOARD_PIN_MAIN_PUMP), 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FATAL: pump fail-safe init failed: %s", esp_err_to_name(err));
        abort();
    }
#if BOARD_HAS_RESERVOIR_PUMP
    err = gpio_set_level(static_cast<gpio_num_t>(BOARD_PIN_RESERVOIR_PUMP), 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FATAL: pump fail-safe init failed: %s", esp_err_to_name(err));
        abort();
    }
#endif
    err = gpio_config(&pump_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FATAL: pump fail-safe init failed: %s", esp_err_to_name(err));
        abort();
    }
}

/**
 * @brief Read the config button at boot and confirm a >= 5 s hold (feature
 * 007, US3/T024/T026).
 *
 * The config button (BOARD_PIN_BTN_CONFIG, GPIO18) is wired to GND and read
 * with an internal pull-up, so it is active LOW: held == logic 0 (parity: the
 * legacy INPUT_PULLUP idiom, same as the level-sensor inputs). Semantics:
 *
 *  - Not pressed at boot → return false immediately, no delay (the common,
 *    fast path: a normal boot must not stall).
 *  - Pressed at boot → enter a bounded hold-confirm loop of at most
 *    kConfigButtonHoldMs (5 s), sampling every kConfigButtonBlinkMs (100 ms)
 *    and toggling BOARD_PIN_STATUS_LED each sample so the operator sees the
 *    reset registering (parity §7/§9). Released before the window elapses →
 *    return false (treated as not held). Held for the whole window → return
 *    true (forced provisioning).
 *
 * The loop is bounded by kConfigButtonHoldMs and runs strictly AFTER
 * pumps_force_off() (pumps already safe) and before the WiFi/sensor tasks
 * start. Not safety-critical: a GPIO-config failure is logged and reported as
 * "released" so a stuck read can never wedge boot into provisioning.
 */
static bool config_button_held_at_boot(void)
{
    const gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << BOARD_PIN_BTN_CONFIG,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t err = gpio_config(&btn_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "config button gpio_config failed: %s (treated as released)",
                 esp_err_to_name(err));
        return false;
    }

    // Active LOW: a released button reads 1 (pulled up). Fast path — no delay
    // on a normal boot.
    if (gpio_get_level(static_cast<gpio_num_t>(BOARD_PIN_BTN_CONFIG)) != 0) {
        return false;
    }

    ESP_LOGI(TAG, "config button down at boot — hold %lu ms to force provisioning",
             static_cast<unsigned long>(kConfigButtonHoldMs));

    // Drive the status LED for the hold-confirm blink. Non-fatal: a failure
    // only costs the visual cue, never the hold decision.
    const gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << BOARD_PIN_STATUS_LED,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&led_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "status LED gpio_config failed during config-button hold");
    }

    // Bounded hold-confirm loop: at most kConfigButtonHoldMs, sampled every
    // kConfigButtonBlinkMs. Toggle the LED each sample (100 ms blink, parity
    // §7/§9). Any early release aborts.
    bool led_on = false;
    for (uint32_t elapsed_ms = 0; elapsed_ms < kConfigButtonHoldMs;
         elapsed_ms += kConfigButtonBlinkMs) {
        vTaskDelay(pdMS_TO_TICKS(kConfigButtonBlinkMs));
        if (gpio_get_level(static_cast<gpio_num_t>(BOARD_PIN_BTN_CONFIG)) != 0) {
            gpio_set_level(static_cast<gpio_num_t>(BOARD_PIN_STATUS_LED), 0);
            ESP_LOGI(TAG, "config button released early — not forcing provisioning");
            return false;
        }
        led_on = !led_on;
        gpio_set_level(static_cast<gpio_num_t>(BOARD_PIN_STATUS_LED),
                       led_on ? 1 : 0);
    }
    gpio_set_level(static_cast<gpio_num_t>(BOARD_PIN_STATUS_LED), 0);
    ESP_LOGI(TAG, "config button held >= %lu ms — forcing WiFi provisioning",
             static_cast<unsigned long>(kConfigButtonHoldMs));
    return true;
}

extern "C" void app_main(void)
{
    // Fail-safe first: every pump that exists on this board off before
    // anything else happens.
    pumps_force_off();

    const esp_app_desc_t *app_desc = esp_app_get_description();

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "WateringSystem");
    ESP_LOGI(TAG, "Project:     %s", app_desc->project_name);
    ESP_LOGI(TAG, "Version:     %s", app_desc->version);
    ESP_LOGI(TAG, "Board:       %s", BOARD_NAME);
    ESP_LOGI(TAG, "ESP-IDF:     %s", esp_get_idf_version());
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "Pumps forced OFF (fail-safe boot state)");

    // Pump driver instances — one per pump that exists on this board
    // (BOARD_HAS_RESERVOIR_PUMP, feature 006). Function-local statics (NOT
    // globals): they are constructed here, strictly after
    // pumps_force_off(), so no constructor can run ahead of the boot
    // fail-safe.
    //
    // Mutex-serializing wrappers: the pumps are touched by two tasks (this
    // main loop's update() and the esp_console REPL task's commands), so
    // EVERY access from here on goes through the wrappers — never through
    // the GpioWaterPump objects directly.
    static EspTimeProvider time_provider;
    static GpioWaterPump plant_pump(
        static_cast<gpio_num_t>(BOARD_PIN_MAIN_PUMP), "plant",
        time_provider);
    static LockedWaterPump plant(plant_pump);
#if BOARD_HAS_RESERVOIR_PUMP
    static GpioWaterPump reservoir_pump(
        static_cast<gpio_num_t>(BOARD_PIN_RESERVOIR_PUMP), "reservoir",
        time_provider);
    static LockedWaterPump reservoir(reservoir_pump);
#endif

    // initialize() re-asserts OFF (glitch-free) before arming the drivers.
    // Failure here is fatal: a pump whose output state is unknown must not
    // be left powered (same policy as pumps_force_off above).
    bool pumps_armed = plant.initialize();
#if BOARD_HAS_RESERVOIR_PUMP
    pumps_armed = pumps_armed && reservoir.initialize();
#endif
    if (!pumps_armed) {
        ESP_LOGE(TAG, "FATAL: pump driver initialization failed");
        abort();
    }

    // Persistent storage. Not safety-critical: any failure below is logged
    // and the system keeps running — config reads fall back to compiled-in
    // defaults and data-storage operations fail gracefully (the pump
    // safety loop never depends on storage).
    //
    // NVS init with the standard recovery: a full page-less partition or a
    // newer NVS format version is erased and re-initialized (factory-state
    // config, by design old-or-new never torn — research.md D5).
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s), recovering",
                 esp_err_to_name(nvs_err));
        nvs_err = nvs_flash_erase();
        if (nvs_err == ESP_OK) {
            nvs_err = nvs_flash_init();
        }
    }
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s (config falls back to defaults)",
                 esp_err_to_name(nvs_err));
    }

    // System network init (feature 007). The TCP/IP stack and the default
    // event loop must exist before any WiFi driver is constructed (US1/US2);
    // they are created once, here, after NVS (esp_wifi persists calibration
    // in NVS) and before any WiFi object. Not safety-critical: a failure is
    // logged and the system keeps running — WiFi is simply unavailable, and
    // the watering path never depends on the network (FR-014). The WiFi driver
    // and manager are constructed below, after this init.
    esp_err_t netif_err = esp_netif_init();
    if (netif_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s (WiFi unavailable)",
                 esp_err_to_name(netif_err));
    }
    esp_err_t event_err = esp_event_loop_create_default();
    if (event_err != ESP_OK) {
        ESP_LOGE(TAG, "default event loop create failed: %s (WiFi unavailable)",
                 esp_err_to_name(event_err));
    }

    // littlefs mount-or-format of the `storage` partition at /storage
    // (FR-007; a corrupted filesystem is reformatted, never bricks).
    const esp_err_t mount_err = StorageMount::mount();
    if (mount_err != ESP_OK) {
        ESP_LOGE(TAG, "storage mount failed: %s (data storage unavailable)",
                 esp_err_to_name(mount_err));
    }

    // Storage instances — function-local statics after pumps_force_off()
    // (boot fail-safe rule), wrapped in the mutex-serializing decorators:
    // accessed from this task and the console REPL task, so EVERY access
    // from here on goes through the wrappers (FR-013).
    static NvsConfigStore config_store;
    static LittleFsDataStorage data_storage(StorageMount::kBasePath,
                                            StorageMount::statsProvider());
    static LockedConfigStore config(config_store);
    static LockedDataStorage storage(data_storage);

    // One-line usage report (parity: storage usage in the serial status
    // block; FR-008).
    const StorageStats stats = storage.getStorageStats();
    ESP_LOGI(TAG, "Storage: %lu/%lu KiB used",
             static_cast<unsigned long>(stats.usedBytes / 1024),
             static_cast<unsigned long>(stats.totalBytes / 1024));

    // WiFi boot-mode decision (feature 007, US1 + US3). A missing stored SSID
    // (the factory/unconfigured state) OR a held config button forces
    // first-boot/recovery provisioning; a configured device otherwise comes up
    // in station mode. The config-button read (US3/T024) samples GPIO18 and
    // confirms a >= 5 s hold, blinking the status LED at 100 ms while it does
    // (T026); it runs here — strictly after pumps_force_off() (pumps already
    // safe) and before the WiFi/sensor tasks start — and is bounded by the
    // hold window. Credential VALUES are never logged: we only test whether an
    // SSID is present. WiFi never touches the watering path (FR-014);
    // everything below stays after the pump fail-safe.
    const bool wifi_credentials_present = !config.getWifiSsid().empty();
    const bool config_button_held = config_button_held_at_boot();
    const WifiBootMode wifi_boot_mode =
        decideBootMode(wifi_credentials_present, config_button_held);

    // The single WiFi driver (one hardware touchpoint) — a function-local
    // static after pumps_force_off() (boot fail-safe rule: the constructor is
    // trivial, all IDF work is in init()). Both boot modes use it: provisioning
    // brings up the SoftAP through it, station drives STA connect/reconnect.
    // init() creates the STA + AP netifs, esp_wifi_init and the event queue on
    // top of the netif/event-loop init done above; a failure is non-fatal
    // (logged) — WiFi is simply unavailable and the watering path is
    // unaffected (FR-014).
    static EspWifiDriver wifi_driver;
    const esp_err_t wifi_init_err = wifi_driver.init();
    if (wifi_init_err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi driver init failed: %s (WiFi unavailable)",
                 esp_err_to_name(wifi_init_err));
    }

    // Set later in the station branch; used both to start the wifi task and to
    // register the manager with the diag console below. Stays nullptr in
    // provisioning mode (the console `wifi` command then reports unavailable).
    WifiManager *wifi_manager = nullptr;

    if (wifi_boot_mode == WifiBootMode::Provisioning) {
        ESP_LOGI(TAG, "WiFi: provisioning mode (SoftAP setup portal)");
        // Emergency reset (US3/T025): when the config button forced
        // provisioning on an ALREADY-configured device, wipe the stored
        // credentials BEFORE the AP/portal comes up, so re-provisioning starts
        // from a clean unconfigured state and cannot silently keep the old
        // network (data-model.md boot rule). An unconfigured device has nothing
        // to clear. Credential VALUES are never logged (FR-004).
        if (shouldClearCredentialsOnBoot(wifi_credentials_present,
                                         config_button_held)) {
            ESP_LOGI(TAG, "config button forced provisioning on a configured "
                     "device — clearing stored WiFi credentials");
            if (!config.clearWifiCredentials()) {
                ESP_LOGW(TAG, "failed to clear WiFi credentials "
                         "(continuing to provisioning)");
            }
        }
        // Bring up the SoftAP radio BEFORE the portal starts serving so the
        // page is reachable over the air the moment it is registered (T018).
        // Credential VALUES are never logged (FR-004): the AP SSID is not a
        // secret, the WPA2 password is.
        if (!wifi_driver.apStart(CONFIG_WS_PROV_AP_SSID,
                                 CONFIG_WS_PROV_AP_PASSWORD)) {
            ESP_LOGE(TAG, "SoftAP failed to start (portal may be unreachable)");
        }
        // Function-local static (boot fail-safe rule: no non-trivial
        // static/global constructors). Constructed only on this branch, kept
        // alive for the program lifetime so it keeps serving.
        static ProvisioningPortal provisioning_portal(config);
        const esp_err_t prov_err = provisioning_portal.start();
        if (prov_err != ESP_OK) {
            ESP_LOGE(TAG, "provisioning portal failed to start: %s",
                     esp_err_to_name(prov_err));
        }
    } else {
        ESP_LOGI(TAG, "WiFi: station mode (stored credentials present)");
        // Reconnect timing from Kconfig (parity defaults, docs/parity-
        // checklist.md §7): 10 s retry, +60 s pause after 5 consecutive
        // failures, 5 s health monitor. The pure WifiManager owns all cadence
        // above the IWifiDriver seam.
        const ReconnectPolicy wifi_policy = {
            .retryIntervalMs =
                static_cast<uint32_t>(CONFIG_WS_WIFI_RETRY_INTERVAL_MS),
            .failuresBeforePause =
                static_cast<uint8_t>(CONFIG_WS_WIFI_FAILS_BEFORE_PAUSE),
            .pauseMs = static_cast<uint32_t>(CONFIG_WS_WIFI_PAUSE_MS),
            .monitorIntervalMs =
                static_cast<uint32_t>(CONFIG_WS_WIFI_MONITOR_INTERVAL_MS),
        };
        // Function-local static (boot fail-safe rule). Reuses the app_main
        // EspTimeProvider (the same monotonic clock the pump/level layer uses)
        // and the shared config store; holds NO watering/pump/sensor reference
        // (FR-014, structurally enforced by the constructor signature). begin()
        // issues the first STA connect; the wifi task then ticks it.
        static WifiManager wifi_manager_inst(wifi_driver, config, time_provider,
                                             wifi_policy);
        wifi_manager_inst.begin(WifiBootMode::Station);
        wifi_manager = &wifi_manager_inst;

        // Separate FreeRTOS task from the 10 Hz pump/level loop — no shared
        // mutex with watering (FR-014). Drives the status LED too (T021).
        wifi_task_start(wifi_manager_inst);
    }

    // RS485 Modbus soil sensor (feature 004). Not safety-critical: a failed
    // client init is logged and the system keeps running — the sensor layer
    // reports invalid data and recovers on later attempts (US2 semantics).
    // Function-local statics after pumps_force_off() (boot fail-safe rule),
    // sensor wrapped in the mutex-serializing decorator: accessed from the
    // console REPL task now and the main-loop controller in PR-11, so EVERY
    // sensor access goes through the wrapper. No periodic read task in
    // feature 004 (arrives with PR-11) — reads happen on console command
    // only.
    static EspModbusClient modbus_client;
    static ModbusSoilSensor soil_sensor_raw(modbus_client);
    static LockedSoilSensor soil_sensor(soil_sensor_raw);

    if (modbus_client.initialize()) {
        ESP_LOGI(TAG, "RS485 Modbus client up (UART%d)",
                 BOARD_RS485_UART_PORT);
    } else {
        ESP_LOGE(TAG, "RS485 Modbus client init failed (error %d) — "
                 "soil sensor unavailable until recovery",
                 modbus_client.getLastError());
    }

    // BME280 environmental sensor on the shared I2C bus (feature 005).
    // Not safety-critical: a failed init is logged and the system keeps
    // running — the sensor layer reports invalid data and the lazy re-init
    // recovers on later polls (US2 semantics). Function-local statics after
    // pumps_force_off() (boot fail-safe rule). The ONE EspI2cBus instance
    // is the shared bus owner — PR-05's INA226 driver receives this same
    // instance (FR-003); no second bus creation on these pins is permitted.
    // The sensor is wrapped in the mutex-serializing decorator: accessed
    // from the 5 s sensor task and the console REPL task, so EVERY sensor
    // access goes through the wrapper.
    static EspI2cBus i2c_bus;
    static Bme280Sensor env_sensor_raw(i2c_bus);
    static LockedEnvironmentalSensor env_sensor(env_sensor_raw);

    if (env_sensor.initialize()) {
        ESP_LOGI(TAG, "BME280 environmental sensor up");
    } else {
        ESP_LOGW(TAG, "BME280 init failed (error %d) — environmental "
                 "readings unavailable until recovery",
                 env_sensor.getLastError());
    }

#if BOARD_HAS_INA226
    // INA226 pump power monitor (feature 006, rev2 only). Rides the SAME
    // EspI2cBus instance as the BME280 — the bus-sharing contract from
    // PR-03: one bus owner, every I2C driver receives it, never a second
    // bus on these pins. Not safety-critical: a failed init is logged and
    // the system keeps running — lazy re-init recovers on later attempts
    // (US3 semantics). Function-local statics after pumps_force_off()
    // (boot fail-safe rule). Wrapped in the mutex-serializing decorator:
    // reached from the console REPL task only in this PR, but wrapped
    // already per the established rule (PR-09 web + PR-11 controller add
    // readers), so EVERY access goes through the wrapper.
    static Ina226Sensor power_sensor_raw(i2c_bus, BOARD_INA226_ADDR,
                                         CONFIG_WS_INA226_SHUNT_MILLIOHM);
    static LockedPowerSensor power_sensor(power_sensor_raw);

    if (power_sensor.initialize()) {
        ESP_LOGI(TAG, "INA226 power monitor up at 0x%02x (shunt %d mOhm)",
                 BOARD_INA226_ADDR, CONFIG_WS_INA226_SHUNT_MILLIOHM);
    } else {
        ESP_LOGW(TAG, "INA226 init failed (error %d) — power readings "
                 "unavailable until recovery",
                 power_sensor.getLastError());
    }
#endif

    // Reservoir level sensors (feature 006). Not safety-critical at boot:
    // a failed GPIO init is logged and the system keeps running — the
    // affected sensor is latched Faulted (markFaulted below), so it
    // reports not-yet-valid forever instead of debouncing a floating pin
    // into a "valid" reading, and PR-11's fail-safe treats invalid as
    // "do not act". Function-local statics
    // after pumps_force_off() (boot fail-safe rule). Split per research
    // R1: GpioLevelSensor is the raw pin read (input + pull-up, no logic);
    // DebouncedLevelSensor holds ALL policy — polarity (FW-5), debounce
    // and settle gating (FW-3) from the board macros. Wrapped in the
    // mutex-serializing decorators: updated from this main loop at 10 Hz
    // and read by the console REPL task (`level`), so EVERY access from
    // here on goes through the wrappers.
    static GpioLevelSensor level_low_input(BOARD_PIN_LEVEL_LOW);
    static GpioLevelSensor level_high_input(BOARD_PIN_LEVEL_HIGH);
    static DebouncedLevelSensor level_low_raw(
        level_low_input, time_provider, BOARD_LEVEL_ACTIVE_LOW != 0,
        BOARD_LEVEL_DEBOUNCE_MS, BOARD_LEVEL_SETTLE_MS);
    static DebouncedLevelSensor level_high_raw(
        level_high_input, time_provider, BOARD_LEVEL_ACTIVE_LOW != 0,
        BOARD_LEVEL_DEBOUNCE_MS, BOARD_LEVEL_SETTLE_MS);
    static LockedLevelSensor level_low(level_low_raw);
    static LockedLevelSensor level_high(level_high_raw);

    // Both inits run unconditionally (no short-circuit): a low-input
    // failure must never skip the high-input init.
    const bool level_low_ok = level_low_input.initialize();
    const bool level_high_ok = level_high_input.initialize();

    // FW-3: the sensor rail is on from power-up (rail *control* arrives in
    // PR-14) — arm the settle gate once at boot. On rev1 the settle window
    // is 0 ms, so this only re-affirms the construction-time gating.
    level_low.notifyPowerOn();
    level_high.notifyPowerOn();

    // A failed GPIO init leaves that pin unconfigured and floating: latch
    // the sensor Faulted so isValid() stays false — markFaulted() is on
    // the concrete DebouncedLevelSensor by design (not ILevelSensor), so
    // it is called here at the wiring site, on the raw objects; safe
    // because no other task touches the sensors yet (console registration
    // and the main loop come later). Ordered AFTER the boot
    // notifyPowerOn() above, which is the deliberate re-arm that clears a
    // fault (recovery: PR-14 rail control or an operator power cycle).
    if (!level_low_ok) {
        level_low_raw.markFaulted();
        ESP_LOGE(TAG, "level LOW sensor GPIO init failed (pin %d) — sensor "
                 "faulted, readings invalid until a power-on re-arm",
                 BOARD_PIN_LEVEL_LOW);
    }
    if (!level_high_ok) {
        level_high_raw.markFaulted();
        ESP_LOGE(TAG, "level HIGH sensor GPIO init failed (pin %d) — sensor "
                 "faulted, readings invalid until a power-on re-arm",
                 BOARD_PIN_LEVEL_HIGH);
    }

    // Serial diagnostic REPL (rig testing; contracts/serial-diagnostic.md).
    // Pump registration is capability-aware: single-pump boards register
    // exactly the plant pump — `pump reservoir` does not exist there
    // (compile-time absence, PR-14 contract).
#if BOARD_HAS_RESERVOIR_PUMP
    diag_console_register_pumps(plant, reservoir);
#else
    diag_console_register_pumps(plant);
#endif
    diag_console_register_storage(config, storage);
    diag_console_register_soil(soil_sensor, modbus_client);
    diag_console_register_env(env_sensor);
    diag_console_register_level(level_low, level_high);
#if BOARD_HAS_INA226
    diag_console_register_power(power_sensor);
#endif
    // nullptr in provisioning mode — the `wifi` command reports unavailable.
    diag_console_register_wifi(wifi_manager);
    esp_err_t err = diag_console_start();
    if (err != ESP_OK) {
        // Console is a diagnostic aid, not a safety function: log and keep
        // running so the pump safety loop below still executes.
        ESP_LOGE(TAG, "diag console failed to start: %s",
                 esp_err_to_name(err));
    }

    // 5 s environmental poll task (feature 005). Started even when the
    // sensor failed init above — lazy re-init recovers later (parity).
    sensor_task_start(env_sensor);

    // Main loop: poll pump enforcement (every pump that exists on this
    // board) and the level sensors at 10 Hz. Pump update() applies the
    // timed self-stop and the hard 300 s max-runtime cap; level update()
    // samples the raw pins and advances the settle/debounce state
    // machines (~3 samples per 300 ms window).
    while (true) {
        plant.update();
#if BOARD_HAS_RESERVOIR_PUMP
        reservoir.update();
#endif
        level_low.update();
        level_high.update();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
