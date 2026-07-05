// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file LockedModbusClient.h
 * @brief Mutex-serializing IModbusClient decorator (header-only).
 *
 * WHY THIS EXISTS: the RS485 Modbus bus is driven from more than one
 * FreeRTOS task. PR-11's watering task issues periodic soil reads
 * (LockedSoilSensor -> ModbusSoilSensor -> this client) while the diag
 * console REPL task runs the rs485test command, which probes the same
 * client directly. LockedSoilSensor's mutex only serializes ISoilSensor
 * calls; it does NOT cover a direct modbus-client call, so rs485test could
 * overlap a soil read's bus transaction and corrupt the shared UART.
 *
 * This decorator wraps an IModbusClient and takes a mutex around every
 * interface call, so BOTH the soil sensor's transactions AND rs485test
 * serialize on the SAME mutex. Each readHoldingRegisters/writeSingleRegister
 * is a complete request/response transaction; serializing per-transaction is
 * sufficient (interleaving complete transactions on the bus is safe, only
 * overlapping ones corrupt it).
 *
 * LOCK ORDERING: the soil path acquires LockedSoilSensor's mutex first, then
 * (inside ModbusSoilSensor) this client's mutex; rs485test acquires ONLY
 * this client's mutex. This decorator never calls back into the soil sensor,
 * so the order is always (SoilSensor mutex -> Modbus mutex) or (Modbus mutex
 * alone) — no inversion, no deadlock.
 *
 * USAGE RULE: once a client is wrapped, the underlying client must ONLY be
 * accessed through the wrapper — every call site (boot init, soil sensor,
 * console rs485test) goes through the LockedModbusClient, never through the
 * wrapped object directly.
 *
 * Pure C++ (<mutex> is available via pthread on ESP-IDF and on the linux
 * preview target), so the decorator is host-includable.
 */

#ifndef WATERINGSYSTEM_SENSORS_LOCKEDMODBUSCLIENT_H
#define WATERINGSYSTEM_SENSORS_LOCKEDMODBUSCLIENT_H

#include <cstdint>
#include <mutex>

#include "interfaces/IModbusClient.h"

/**
 * @brief IModbusClient decorator that serializes every call with a mutex.
 *
 * Composition, not inheritance from a concrete client: the base client stays
 * pure (no locking) and the host tests are unchanged. The wrapped client
 * must outlive this object.
 */
class LockedModbusClient : public IModbusClient {
public:
    /// Wrap @p client; the wrapped client must outlive this object.
    explicit LockedModbusClient(IModbusClient& client) : client_(client) {}

    LockedModbusClient(const LockedModbusClient&) = delete;
    LockedModbusClient& operator=(const LockedModbusClient&) = delete;

    bool initialize() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return client_.initialize();
    }

    bool readHoldingRegisters(uint8_t deviceAddress, uint16_t startRegister,
                              uint16_t count, uint16_t* buffer) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return client_.readHoldingRegisters(deviceAddress, startRegister, count,
                                            buffer);
    }

    bool writeSingleRegister(uint8_t deviceAddress, uint16_t registerAddress,
                             uint16_t value) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return client_.writeSingleRegister(deviceAddress, registerAddress, value);
    }

    int getLastError() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return client_.getLastError();
    }

    void setTimeout(uint32_t timeoutMs) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        client_.setTimeout(timeoutMs);
    }

    void getStatistics(uint32_t* successCount, uint32_t* errorCount) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        client_.getStatistics(successCount, errorCount);
    }

private:
    IModbusClient& client_;
    mutable std::mutex mutex_;
};

#endif /* WATERINGSYSTEM_SENSORS_LOCKEDMODBUSCLIENT_H */
