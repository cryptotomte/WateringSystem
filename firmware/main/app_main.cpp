/**
 * @file app_main.cpp
 * @brief WateringSystem firmware entry point (phase 0 skeleton).
 *
 * The very first action at boot is to force both pump outputs to a safe
 * OFF state. This fail-safe MUST stay first in app_main in all future
 * phases: pumps are always off after power-on, watchdog reset and OTA
 * restart, before any other initialization runs.
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
#include "esp_idf_version.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "app_main";

/**
 * @brief Drive both pump GPIOs to a safe OFF state (output, level 0).
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
    const gpio_config_t pump_cfg = {
        .pin_bit_mask = (1ULL << BOARD_PIN_MAIN_PUMP) |
                        (1ULL << BOARD_PIN_RESERVOIR_PUMP),
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
    err = gpio_set_level(static_cast<gpio_num_t>(BOARD_PIN_RESERVOIR_PUMP), 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FATAL: pump fail-safe init failed: %s", esp_err_to_name(err));
        abort();
    }
    err = gpio_config(&pump_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FATAL: pump fail-safe init failed: %s", esp_err_to_name(err));
        abort();
    }
}

extern "C" void app_main(void)
{
    // Fail-safe first: both pumps off before anything else happens.
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
}
