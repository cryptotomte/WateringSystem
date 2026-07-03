// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file EspI2cBus.h
 * @brief II2cBus over ESP-IDF's i2c_master driver (esp_driver_i2c).
 *
 * ESP32-ONLY: excluded from the linux-target build together with the
 * esp_driver_i2c dependency (see this component's CMakeLists.txt). This is
 * the I2C hardware touchpoint — all sensor policy lives above II2cBus
 * (research.md R6, same split as EspModbusClient).
 *
 * PRIV rule: driver/i2c_master.h appears only in the .cpp, never here —
 * the bus and device handles are held as opaque pointers. The NEW
 * i2c_master API is used exclusively; the deprecated legacy driver/i2c.h
 * is never included (FR-002, research.md R2).
 *
 * Bus sharing (FR-003): app_main constructs ONE EspI2cBus (function-local
 * static) and passes it to every I2C driver — PR-05's INA226 receives the
 * same instance. No second bus creation on these pins is permitted. Pins
 * come from board/board.h inside the .cpp (BOARD_PIN_I2C_SDA/SCL).
 *
 * Concurrency: transaction-level safety across tasks comes from the
 * i2c_master driver's per-transaction bus lock (research.md R3). The
 * device-handle bookkeeping (lazy bus creation + handle table) is
 * internally synchronized with a private mutex, so concurrent first use
 * from multiple tasks is safe. Reading-snapshot consistency above this
 * layer is LockedEnvironmentalSensor's job, not this class's.
 */

#ifndef WATERINGSYSTEM_SENSORS_ESPI2CBUS_H
#define WATERINGSYSTEM_SENSORS_ESPI2CBUS_H

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "interfaces/II2cBus.h"

/**
 * @brief I2C master on the board's SDA/SCL pins (100 kHz standard mode).
 *
 * The bus handle is created lazily on first use (no work in the
 * constructor — no error path there); per-address device handles are
 * created on first use at 100 kHz (FR-002) and cached. All transactions
 * use finite timeouts; failures are returned as false and logged —
 * transaction failures at debug level (expected NACKs) or warning level
 * (timeouts/unexpected errors); one-time infrastructure failures at error
 * level. Error classification is the sensor driver's job.
 */
class EspI2cBus : public II2cBus {
public:
    /// Standard-mode clock per device (FR-002).
    static constexpr uint32_t kSclSpeedHz = 100000;

    /// Finite per-transaction timeout — no infinite waits on a wedged bus.
    static constexpr int kTimeoutMs = 100;

    EspI2cBus() = default;

    /// Removes all device handles and deletes the master bus.
    ~EspI2cBus() override;

    EspI2cBus(const EspI2cBus&) = delete;
    EspI2cBus& operator=(const EspI2cBus&) = delete;

    // II2cBus
    bool probe(uint8_t address7) override;
    bool readRegisters(uint8_t address7, uint8_t startReg, uint8_t* buf,
                       size_t len) override;
    bool writeRegister(uint8_t address7, uint8_t reg, uint8_t value) override;
    bool writeRegister16(uint8_t address7, uint8_t reg,
                         uint16_t value) override;

private:
    /// Enough for BME280 (one of two addresses) + PR-05's INA226 devices.
    static constexpr size_t kMaxDevices = 8;

    struct Device {
        uint8_t address = 0;
        void* handle = nullptr;  ///< opaque i2c_master_dev_handle_t
    };

    /// Create the master bus on first use (board pins, port auto,
    /// internal pull-ups on). Returns false when creation fails.
    /// Caller must hold mutex_.
    bool ensureBus();

    /// Cached-or-created device handle for @p address7; nullptr on failure.
    /// Takes mutex_ internally (handle-table bookkeeping).
    void* deviceHandle(uint8_t address7);

    /// Guards busHandle_/devices_/deviceCount_ (lazy creation from
    /// multiple tasks). Transactions run outside this lock — the
    /// i2c_master driver's bus lock covers them.
    std::mutex mutex_;

    void* busHandle_ = nullptr;  ///< opaque i2c_master_bus_handle_t
    Device devices_[kMaxDevices] = {};
    size_t deviceCount_ = 0;
};

#endif /* WATERINGSYSTEM_SENSORS_ESPI2CBUS_H */
