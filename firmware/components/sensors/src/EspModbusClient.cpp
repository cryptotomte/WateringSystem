// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file EspModbusClient.cpp
 * @brief esp-modbus 2.1.2 Modbus RTU master implementation.
 *
 * Target-only translation unit (excluded from the linux host build). The
 * only file in the sensors component that touches esp-modbus, UART and
 * GPIO APIs. Setup sequence and RS485 half-duplex rationale: research.md
 * R1/R2/R3/R4.
 */

#include "sensors/EspModbusClient.h"

#include "board/board.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

// esp-modbus umbrella header (managed component, pinned ==2.1.2).
#include "mbcontroller.h"

static const char *TAG = "esp_modbus_client";

namespace {

/**
 * @brief Map an esp-modbus/esp_err_t result onto the IModbusClient error
 * table (data-model.md, research.md R6).
 *
 * Kept as ONE helper so the mapping can be refined in a single place.
 *
 * parity divergence R6: esp-modbus 2.1.2's mbc_master_send_request does not
 * surface the Modbus slave exception code to the caller — exceptions and
 * other invalid responses come back as generic esp_err_t failures. The
 * legacy 100+n exception granularity is therefore collapsed onto error 2
 * (bus/communication error) here. The binding FR-010 requirement — distinct
 * from timeout — still holds (timeout maps to 3).
 */
int map_esp_err(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return 0;
    case ESP_ERR_TIMEOUT:
        return 3;  // No response within the configured timeout.
    case ESP_ERR_INVALID_STATE:
        return 1;  // Stack not initialized/started.
    default:
        return 2;  // CRC/framing/invalid response/slave exception class.
    }
}

}  // namespace

EspModbusClient::~EspModbusClient()
{
    if (mbHandle_ != nullptr) {
        const esp_err_t err = mbc_master_delete(mbHandle_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "mbc_master_delete failed: %s",
                     esp_err_to_name(err));
        }
        mbHandle_ = nullptr;
    }
}

bool EspModbusClient::initialize()
{
    if (initialized_) {
        return true;
    }

    // R1: esp-modbus 2.x serial master, Modbus RTU 9600 8N1, parity
    // response timeout (3000 ms default, member set via setTimeout()).
    mb_communication_info_t comm_info = {};
    comm_info.ser_opts.port = static_cast<uart_port_t>(BOARD_RS485_UART_PORT);
    comm_info.ser_opts.mode = MB_RTU;
    comm_info.ser_opts.baudrate = 9600;
    comm_info.ser_opts.data_bits = UART_DATA_8_BITS;
    comm_info.ser_opts.stop_bits = UART_STOP_BITS_1;
    comm_info.ser_opts.parity = MB_PARITY_NONE;
    comm_info.ser_opts.uid = 0;  // master
    comm_info.ser_opts.response_tout_ms = timeoutMs_;

    esp_err_t err = mbc_master_create_serial(&comm_info, &mbHandle_);
    if (err != ESP_OK || mbHandle_ == nullptr) {
        ESP_LOGE(TAG, "mbc_master_create_serial failed: %s",
                 esp_err_to_name(err));
        mbHandle_ = nullptr;
        lastError_ = 1;
        return false;
    }

    // R2: UART pins from board.h. rev1 drives the transceiver DE via the
    // UART RTS line (hardware-timed around each frame); rev2 has no DE pin
    // (THVD1426 auto-direction) so RTS stays unrouted. This #if is the one
    // place the board direction-control difference exists.
    err = uart_set_pin(static_cast<uart_port_t>(BOARD_RS485_UART_PORT),
                       BOARD_PIN_RS485_TX, BOARD_PIN_RS485_RX,
#if BOARD_HAS_RS485_DE
                       BOARD_PIN_RS485_DE,
#else
                       UART_PIN_NO_CHANGE,
#endif
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        mbc_master_delete(mbHandle_);
        mbHandle_ = nullptr;
        lastError_ = 1;
        return false;
    }

    err = mbc_master_start(mbHandle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_master_start failed: %s", esp_err_to_name(err));
        mbc_master_delete(mbHandle_);
        mbHandle_ = nullptr;
        lastError_ = 1;
        return false;
    }

    // R2/R3: RS485 half-duplex mode on BOTH boards. Besides the automatic
    // RTS/DE framing on rev1, the TX-gated receive path suppresses the rev2
    // THVD1426 TX echo (RE̅ grounded, receiver always on): echo bytes are
    // physically simultaneous with transmission and never reach the driver.
    // The esp-modbus RTU T3.5 frame resynchronization is the fallback for
    // residual tail bytes. Electrically verified on rev2 at PR-14.
    err = uart_set_mode(static_cast<uart_port_t>(BOARD_RS485_UART_PORT),
                        UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_mode(RS485 half-duplex) failed: %s",
                 esp_err_to_name(err));
        mbc_master_delete(mbHandle_);
        mbHandle_ = nullptr;
        lastError_ = 1;
        return false;
    }

    // R4 (FW-2): internal pull-up on the RX pin, unconditionally on both
    // boards. On rev2 the THVD1426 SHDN̅ tracks SENS_PWR_EN — RO goes hi-Z
    // when the sensor power domain is off and the RX GPIO would otherwise
    // float into the UART, producing garbage bytes. Harmless on rev1
    // (ADM3485 RO drives push-pull).
    err = gpio_pullup_en(static_cast<gpio_num_t>(BOARD_PIN_RS485_RX));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_pullup_en(RX) failed: %s", esp_err_to_name(err));
        mbc_master_delete(mbHandle_);
        mbHandle_ = nullptr;
        lastError_ = 1;
        return false;
    }

    ESP_LOGI(TAG,
             "Modbus RTU master up: UART%d 9600 8N1, timeout %lu ms, "
             "RS485 half-duplex",
             BOARD_RS485_UART_PORT, static_cast<unsigned long>(timeoutMs_));
    initialized_ = true;
    lastError_ = 0;
    return true;
}

bool EspModbusClient::sendRequest(uint8_t deviceAddress, uint8_t command,
                                  uint16_t registerAddress, uint16_t count,
                                  void* data)
{
    // Exactly one bus attempt per call, exactly one counter increment
    // (interface contract; no retry — parity).
    if (!initialized_) {
        lastError_ = 1;
        ++errorCount_;
        return false;
    }

    mb_param_request_t request = {};
    request.slave_addr = deviceAddress;
    request.command = command;
    request.reg_start = registerAddress;
    request.reg_size = count;

    const esp_err_t err = mbc_master_send_request(mbHandle_, &request, data);
    lastError_ = map_esp_err(err);
    if (err != ESP_OK) {
        ++errorCount_;
        ESP_LOGW(TAG,
                 "request failed: cmd=0x%02x addr=%u reg=0x%04x n=%u: %s "
                 "(error %d)",
                 static_cast<unsigned>(command),
                 static_cast<unsigned>(deviceAddress),
                 static_cast<unsigned>(registerAddress),
                 static_cast<unsigned>(count), esp_err_to_name(err),
                 lastError_);
        return false;
    }
    ++successCount_;
    return true;
}

bool EspModbusClient::readHoldingRegisters(uint8_t deviceAddress,
                                           uint16_t startRegister,
                                           uint16_t count, uint16_t* buffer)
{
    // Modbus function 0x03: esp-modbus fills the caller buffer with the
    // register values only on success.
    return sendRequest(deviceAddress, 0x03, startRegister, count, buffer);
}

bool EspModbusClient::writeSingleRegister(uint8_t deviceAddress,
                                          uint16_t registerAddress,
                                          uint16_t value)
{
    // Modbus function 0x06: the esp-modbus master verifies the slave's
    // echo response internally — ESP_OK means the write was acknowledged.
    uint16_t writeValue = value;
    return sendRequest(deviceAddress, 0x06, registerAddress, 1, &writeValue);
}

int EspModbusClient::getLastError()
{
    return lastError_;
}

void EspModbusClient::setTimeout(uint32_t timeoutMs)
{
    timeoutMs_ = timeoutMs;
    if (initialized_) {
        // R5: no documented runtime timeout setter in esp-modbus 2.1.2 —
        // the stored value takes effect on a future re-initialization only.
        ESP_LOGW(TAG,
                 "setTimeout(%lu) after initialize(): applied at next "
                 "initialization only",
                 static_cast<unsigned long>(timeoutMs));
    }
}

void EspModbusClient::getStatistics(uint32_t* successCount,
                                    uint32_t* errorCount)
{
    if (successCount != nullptr) {
        *successCount = successCount_;
    }
    if (errorCount != nullptr) {
        *errorCount = errorCount_;
    }
}
