// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file EspI2cBus.cpp
 * @brief i2c_master (esp_driver_i2c) implementation of II2cBus.
 *
 * Target-only translation unit (excluded from the linux host build). The
 * only file in the sensors component that touches the I2C driver. Uses the
 * NEW driver/i2c_master.h API exclusively — the deprecated legacy
 * driver/i2c.h must never be included anywhere in this firmware (FR-002,
 * research.md R2: IDF forbids mixing the two drivers on one port).
 */

#include "sensors/EspI2cBus.h"

#include "board/board.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "esp_i2c_bus";

EspI2cBus::~EspI2cBus()
{
    // RAII teardown, failures logged (never discarded). In practice the
    // app_main instance lives forever; this path serves tests/future use.
    for (size_t i = 0; i < deviceCount_; ++i) {
        const esp_err_t err = i2c_master_bus_rm_device(
            static_cast<i2c_master_dev_handle_t>(devices_[i].handle));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_master_bus_rm_device(0x%02x) failed: %s",
                     devices_[i].address, esp_err_to_name(err));
        }
    }
    if (busHandle_ != nullptr) {
        const esp_err_t err = i2c_del_master_bus(
            static_cast<i2c_master_bus_handle_t>(busHandle_));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_del_master_bus failed: %s",
                     esp_err_to_name(err));
        }
    }
}

bool EspI2cBus::ensureBus()
{
    if (busHandle_ != nullptr) {
        return true;
    }

    // Single shared bus (FR-003) on the board pins — never hard-coded
    // GPIOs. Port auto-select; internal pull-ups enabled (the breakout
    // modules carry their own — the internal ones are harmless
    // belt-and-braces, contracts/interfaces.md).
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = -1;  // auto-select
    bus_config.sda_io_num = static_cast<gpio_num_t>(BOARD_PIN_I2C_SDA);
    bus_config.scl_io_num = static_cast<gpio_num_t>(BOARD_PIN_I2C_SCL);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;  // driver-recommended default
    bus_config.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus = nullptr;
    const esp_err_t err = i2c_new_master_bus(&bus_config, &bus);
    if (err != ESP_OK) {
        // One-time infrastructure failure — worth more than a debug line.
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return false;
    }
    busHandle_ = bus;
    ESP_LOGI(TAG, "I2C master bus up (SDA=%d SCL=%d, %lu Hz per device)",
             BOARD_PIN_I2C_SDA, BOARD_PIN_I2C_SCL,
             static_cast<unsigned long>(kSclSpeedHz));
    return true;
}

void* EspI2cBus::deviceHandle(uint8_t address7)
{
    // Guards the lazy bus creation and the handle table against concurrent
    // first use from multiple tasks (sensor task + console REPL; INA226 in
    // PR-05). Transactions themselves are NOT under this lock — the
    // i2c_master driver's per-transaction bus lock covers those.
    std::lock_guard<std::mutex> lock(mutex_);

    if (!ensureBus()) {
        return nullptr;
    }

    for (size_t i = 0; i < deviceCount_; ++i) {
        if (devices_[i].address == address7) {
            return devices_[i].handle;
        }
    }

    if (deviceCount_ >= kMaxDevices) {
        ESP_LOGE(TAG, "device table full (%u) — cannot add 0x%02x",
                 static_cast<unsigned>(kMaxDevices), address7);
        return nullptr;
    }

    // Created on first use, 100 kHz standard mode per device (FR-002).
    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = address7;
    dev_config.scl_speed_hz = kSclSpeedHz;

    i2c_master_dev_handle_t dev = nullptr;
    const esp_err_t err = i2c_master_bus_add_device(
        static_cast<i2c_master_bus_handle_t>(busHandle_), &dev_config, &dev);
    if (err != ESP_OK) {
        // Infrastructure fault (never an expected runtime condition —
        // same class as ensureBus()/table-full above).
        ESP_LOGE(TAG, "i2c_master_bus_add_device(0x%02x) failed: %s",
                 address7, esp_err_to_name(err));
        return nullptr;
    }
    devices_[deviceCount_++] = {address7, dev};
    return dev;
}

bool EspI2cBus::probe(uint8_t address7)
{
    {
        // ensureBus() mutates busHandle_ — same lock as deviceHandle().
        // Once created the handle never changes, so the transaction below
        // can safely run outside the lock (driver bus lock covers it).
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ensureBus()) {
            return false;
        }
    }
    const esp_err_t err = i2c_master_probe(
        static_cast<i2c_master_bus_handle_t>(busHandle_), address7,
        kTimeoutMs);
    if (err == ESP_ERR_NOT_FOUND) {
        // NACK — expected for absent devices, debug level (classification
        // is the sensor driver's job, contracts/interfaces.md).
        ESP_LOGD(TAG, "probe(0x%02x): %s", address7, esp_err_to_name(err));
        return false;
    }
    if (err != ESP_OK) {
        // Timeout/bus fault — a wedged bus must be distinguishable from a
        // device-absent NACK in the field logs.
        ESP_LOGW(TAG, "probe(0x%02x): %s", address7, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool EspI2cBus::readRegisters(uint8_t address7, uint8_t startReg,
                              uint8_t* buf, size_t len)
{
    i2c_master_dev_handle_t dev =
        static_cast<i2c_master_dev_handle_t>(deviceHandle(address7));
    if (dev == nullptr) {
        return false;
    }
    // Register-pointer write + N-byte read in ONE transaction with a
    // repeated start (i2c_master_transmit_receive) — required for correct
    // BME280 burst reads (II2cBus contract).
    const esp_err_t err =
        i2c_master_transmit_receive(dev, &startReg, 1, buf, len, kTimeoutMs);
    if (err != ESP_OK) {
        // NACK (device unplugged) stays at debug; a timeout/unexpected
        // error means a wedged bus and must be visible at default level.
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGD(TAG, "readRegisters(0x%02x, 0x%02x, %u): %s", address7,
                     startReg, static_cast<unsigned>(len),
                     esp_err_to_name(err));
        } else {
            ESP_LOGW(TAG, "readRegisters(0x%02x, 0x%02x, %u): %s", address7,
                     startReg, static_cast<unsigned>(len),
                     esp_err_to_name(err));
        }
        return false;
    }
    return true;
}

bool EspI2cBus::writeRegister(uint8_t address7, uint8_t reg, uint8_t value)
{
    i2c_master_dev_handle_t dev =
        static_cast<i2c_master_dev_handle_t>(deviceHandle(address7));
    if (dev == nullptr) {
        return false;
    }
    const uint8_t payload[2] = {reg, value};
    const esp_err_t err =
        i2c_master_transmit(dev, payload, sizeof(payload), kTimeoutMs);
    if (err != ESP_OK) {
        // Same classification as readRegisters(): expected NACK at debug,
        // timeout/unexpected errors at warning.
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGD(TAG, "writeRegister(0x%02x, 0x%02x, 0x%02x): %s",
                     address7, reg, value, esp_err_to_name(err));
        } else {
            ESP_LOGW(TAG, "writeRegister(0x%02x, 0x%02x, 0x%02x): %s",
                     address7, reg, value, esp_err_to_name(err));
        }
        return false;
    }
    return true;
}
