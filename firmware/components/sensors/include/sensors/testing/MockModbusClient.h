// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file MockModbusClient.h
 * @brief Scriptable IModbusClient test double (header-only).
 *
 * Serves the ModbusSoilSensor host tests: script register payloads per
 * (address, startRegister, count), queue per-call outcomes (success,
 * timeout, bus error, slave exception) for fail-then-recover scenarios,
 * and assert on the recorded call log (no-retry invariant, real-read
 * availability probe, calibration register writes) and on the
 * one-increment-per-call statistics. Never compiled into target builds
 * (only included from test code). No IDF includes.
 */

#ifndef WATERINGSYSTEM_SENSORS_TESTING_MOCKMODBUSCLIENT_H
#define WATERINGSYSTEM_SENSORS_TESTING_MOCKMODBUSCLIENT_H

#include <cstdint>
#include <optional>
#include <vector>

#include "interfaces/IModbusClient.h"

/**
 * @brief IModbusClient over scripted payloads, instrumented for tests.
 *
 * Outcome selection per read/write call: the front of `outcomeQueue` is
 * consumed if non-empty, otherwise `defaultOutcome` applies (error-code
 * convention: kOk = success). Reads that succeed return the payload
 * scripted for their exact (address, startRegister, count) key, else the
 * default payload, else all-zero registers.
 */
class MockModbusClient : public IModbusClient {
public:
    // Error codes (data-model.md table) as readable test constants.
    static constexpr int kOk = 0;
    static constexpr int kErrNotInitialized = 1;
    static constexpr int kErrBus = 2;              ///< CRC/framing/truncated
    static constexpr int kErrTimeout = 3;
    static constexpr int kErrExceptionBase = 100;  ///< 100 + n for slave exception n

    /// One recorded bus call (in `calls`, chronological).
    struct Call {
        enum class Type { Read, Write };
        Type type;
        uint8_t deviceAddress;
        uint16_t startRegister;  ///< registerAddress for writes
        uint16_t count;          ///< register count (reads); 1 for writes
        uint16_t value;          ///< written value (writes); 0 for reads
        bool succeeded;          ///< outcome reported to the caller
    };

    // Instrumentation (public, MockConfigStore style).
    std::vector<Call> calls;            ///< every read/write, in call order
    std::vector<uint32_t> timeoutCalls; ///< every setTimeout() argument
    int initializeCalls = 0;
    bool initializeResult = true;       ///< false: initialize() fails
    int defaultOutcome = kOk;           ///< used when outcomeQueue is empty

    // -- Scripting --------------------------------------------------------

    /**
     * @brief Script the payload returned for reads matching exactly
     * (deviceAddress, startRegister, count == values.size()).
     * Re-scripting the same key replaces the previous payload.
     */
    void setRegisters(uint8_t deviceAddress, uint16_t startRegister,
                      std::vector<uint16_t> values)
    {
        for (auto& entry : scripted_) {
            if (entry.deviceAddress == deviceAddress &&
                entry.startRegister == startRegister &&
                entry.values.size() == values.size()) {
                entry.values = std::move(values);
                return;
            }
        }
        scripted_.push_back({deviceAddress, startRegister, std::move(values)});
    }

    /**
     * @brief Fallback payload for successful reads with no exact-key script;
     * the first `count` values are returned (must hold at least `count`,
     * shorter defaults fall through to all-zero registers).
     */
    void setDefaultRegisters(std::vector<uint16_t> values)
    {
        defaultRegisters_ = std::move(values);
    }

    /**
     * @brief Queue the outcome for the NEXT read/write call (FIFO).
     *
     * kOk = success; kErrTimeout, kErrBus, kErrExceptionBase + n, ... force
     * that failure. Queue e.g. {kErrTimeout, kOk} for a fail-then-recover
     * scenario. When the queue is empty, defaultOutcome applies.
     */
    void queueOutcome(int errorCode) { outcomeQueue_.push_back(errorCode); }

    // -- IModbusClient -----------------------------------------------------

    bool initialize() override
    {
        ++initializeCalls;
        initialized_ = initializeResult;
        return initialized_;
    }

    bool readHoldingRegisters(uint8_t deviceAddress, uint16_t startRegister,
                              uint16_t count, uint16_t* buffer) override
    {
        Call call{Call::Type::Read, deviceAddress, startRegister, count, 0, false};
        if (!initialized_) {
            return finish(call, kErrNotInitialized);
        }
        const int outcome = nextOutcome();
        if (outcome != kOk) {
            return finish(call, outcome);
        }
        fillPayload(deviceAddress, startRegister, count, buffer);
        return finish(call, kOk);
    }

    bool writeSingleRegister(uint8_t deviceAddress, uint16_t registerAddress,
                             uint16_t value) override
    {
        Call call{Call::Type::Write, deviceAddress, registerAddress, 1, value, false};
        if (!initialized_) {
            return finish(call, kErrNotInitialized);
        }
        return finish(call, nextOutcome());
    }

    int getLastError() override { return lastError_; }

    void setTimeout(uint32_t timeoutMs) override
    {
        timeoutCalls.push_back(timeoutMs);
    }

    void getStatistics(uint32_t* successCount, uint32_t* errorCount) override
    {
        if (successCount != nullptr) {
            *successCount = successCount_;
        }
        if (errorCount != nullptr) {
            *errorCount = errorCount_;
        }
    }

private:
    struct ScriptedRead {
        uint8_t deviceAddress;
        uint16_t startRegister;
        std::vector<uint16_t> values;
    };

    int nextOutcome()
    {
        if (outcomeQueue_.empty()) {
            return defaultOutcome;
        }
        const int outcome = outcomeQueue_.front();
        outcomeQueue_.erase(outcomeQueue_.begin());
        return outcome;
    }

    void fillPayload(uint8_t deviceAddress, uint16_t startRegister,
                     uint16_t count, uint16_t* buffer) const
    {
        for (const auto& entry : scripted_) {
            if (entry.deviceAddress == deviceAddress &&
                entry.startRegister == startRegister &&
                entry.values.size() == count) {
                for (uint16_t i = 0; i < count; ++i) {
                    buffer[i] = entry.values[i];
                }
                return;
            }
        }
        if (defaultRegisters_.has_value() && defaultRegisters_->size() >= count) {
            for (uint16_t i = 0; i < count; ++i) {
                buffer[i] = (*defaultRegisters_)[i];
            }
            return;
        }
        for (uint16_t i = 0; i < count; ++i) {
            buffer[i] = 0;
        }
    }

    /// Record the call, apply the one-increment-per-call statistics
    /// contract, set lastError, and return the call result.
    bool finish(Call& call, int errorCode)
    {
        call.succeeded = (errorCode == kOk);
        calls.push_back(call);
        lastError_ = errorCode;
        if (call.succeeded) {
            ++successCount_;
        } else {
            ++errorCount_;
        }
        return call.succeeded;
    }

    bool initialized_ = false;
    int lastError_ = kOk;
    uint32_t successCount_ = 0;
    uint32_t errorCount_ = 0;
    std::vector<int> outcomeQueue_;
    std::vector<ScriptedRead> scripted_;
    std::optional<std::vector<uint16_t>> defaultRegisters_;
};

#endif /* WATERINGSYSTEM_SENSORS_TESTING_MOCKMODBUSCLIENT_H */
