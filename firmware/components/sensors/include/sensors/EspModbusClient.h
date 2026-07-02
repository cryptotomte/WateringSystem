// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file EspModbusClient.h
 * @brief IModbusClient over esp-modbus 2.1.2 (Modbus RTU master, RS485).
 *
 * ESP32-ONLY: excluded from the linux-target build together with the
 * esp-modbus/driver dependencies (see this component's CMakeLists.txt).
 * This is the sensors component's single hardware touchpoint — all
 * business logic lives above IModbusClient (research.md R7).
 *
 * PRIV rule: esp-modbus and driver headers appear only in the .cpp, never
 * here — the esp-modbus master handle is held as an opaque pointer.
 *
 * UART port and pins come from board/board.h inside the .cpp; the
 * `#if BOARD_HAS_RS485_DE` direction-control difference between rev1
 * (RTS-driven DE) and rev2 (THVD1426 auto-direction) lives in exactly one
 * place there (research.md R2).
 *
 * Concurrency: unsynchronized, like every base implementation in this
 * codebase. As of feature 004 the client is only reached from the console
 * REPL task (directly by `rs485test` and through LockedSoilSensor by
 * `soil`); a second consumer task would need a locking wrapper. PR-11's
 * main-loop reader adds exactly that second task — `rs485test`'s raw access
 * must be routed through a locked client wrapper (or the sensor) then.
 */

#ifndef WATERINGSYSTEM_SENSORS_ESPMODBUSCLIENT_H
#define WATERINGSYSTEM_SENSORS_ESPMODBUSCLIENT_H

#include <cstdint>

#include "interfaces/IModbusClient.h"

/**
 * @brief Modbus RTU master on the board's RS485 UART (9600 8N1, parity).
 */
class EspModbusClient : public IModbusClient {
public:
    /// Parity response timeout (docs/parity-checklist.md §5).
    static constexpr uint32_t kDefaultTimeoutMs = 3000;

    EspModbusClient() = default;

    /// Tears down the esp-modbus master stack (mbc_master_delete).
    ~EspModbusClient() override;

    EspModbusClient(const EspModbusClient&) = delete;
    EspModbusClient& operator=(const EspModbusClient&) = delete;

    /**
     * @brief Bring up the esp-modbus master on the RS485 UART.
     *
     * create → uart_set_pin (RTS = DE pin iff BOARD_HAS_RS485_DE) → start →
     * uart_set_mode(RS485 half-duplex) → RX pull-up (FW-2). Idempotent;
     * on failure every partially-created resource is torn down again.
     */
    bool initialize() override;

    bool readHoldingRegisters(uint8_t deviceAddress, uint16_t startRegister,
                              uint16_t count, uint16_t* buffer) override;

    bool writeSingleRegister(uint8_t deviceAddress, uint16_t registerAddress,
                             uint16_t value) override;

    int getLastError() override;

    /**
     * @brief Set the response timeout for subsequent transfers.
     *
     * esp-modbus 2.1.2 exposes no documented runtime timeout setter, so the
     * value is applied at initialize() time only (research.md R5, risk
     * documented): calls after initialize() are stored but take effect only
     * on a future re-initialization. No caller changes the timeout at
     * runtime today.
     */
    void setTimeout(uint32_t timeoutMs) override;

    void getStatistics(uint32_t* successCount, uint32_t* errorCount) override;

private:
    /// One bus transaction via mbc_master_send_request + bookkeeping.
    bool sendRequest(uint8_t deviceAddress, uint8_t command,
                     uint16_t registerAddress, uint16_t count, void* data);

    void* mbHandle_ = nullptr;  ///< opaque esp-modbus master handle
    bool initialized_ = false;
    int lastError_ = 0;
    uint32_t timeoutMs_ = kDefaultTimeoutMs;
    uint32_t successCount_ = 0;
    uint32_t errorCount_ = 0;
};

#endif /* WATERINGSYSTEM_SENSORS_ESPMODBUSCLIENT_H */
