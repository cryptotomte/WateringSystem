// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file MockI2cBus.h
 * @brief Scriptable II2cBus test double (header-only).
 *
 * Serves the Bme280Sensor host tests: script a 256-byte register map per
 * device address (chip-ID, calibration block, data registers), control
 * probe results via device presence, inject failures (NACK on an absent
 * address, queued error on register read/write; corrupt data is simply
 * scripted wrong register bytes), and assert on the recorded call log
 * (config bytes written in order, burst-read shape, re-probe on recovery).
 * Never compiled into target builds (only included from test code). No IDF
 * includes.
 */

#ifndef WATERINGSYSTEM_SENSORS_TESTING_MOCKI2CBUS_H
#define WATERINGSYSTEM_SENSORS_TESTING_MOCKI2CBUS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "interfaces/II2cBus.h"

/**
 * @brief II2cBus over scripted per-address register maps, instrumented for
 * tests.
 *
 * Device presence drives the natural failure mode: probe() ACKs only
 * addresses added with addDevice(), and readRegisters()/writeRegister()
 * against an absent address fail (address NACK). For failures on a PRESENT
 * device (mid-read bus error), queue outcomes with queueReadOutcome()/
 * queueWriteOutcome(): the front of the queue is consumed per call; an
 * empty queue means success. Writes land in the register map, so config
 * sequences are observable both in `calls` and in the map itself.
 */
class MockI2cBus : public II2cBus {
public:
    /// One recorded bus transaction (in `calls`, chronological).
    struct Call {
        enum class Type { Probe, Read, Write };
        Type type;
        uint8_t address;
        uint8_t reg;    ///< startReg (reads) / register (writes); 0 for probes
        size_t len;     ///< byte count (reads); 1 for writes; 0 for probes
        uint8_t value;  ///< written value (writes); 0 otherwise
        bool succeeded; ///< outcome reported to the caller
    };

    // Instrumentation (public, MockModbusClient style).
    std::vector<Call> calls;  ///< every probe/read/write, in call order

    // -- Scripting ----------------------------------------------------------

    /// Make @p address7 present (ACKs probes) with an all-zero register map.
    void addDevice(uint8_t address7) { devices_[address7]; }

    /// Remove @p address7: probes NACK, reads/writes fail (unplug scenario).
    /// The register map is discarded — re-adding yields a fresh device.
    void removeDevice(uint8_t address7) { devices_.erase(address7); }

    /// Script one register byte on a device (added implicitly if absent).
    void setRegister(uint8_t address7, uint8_t reg, uint8_t value)
    {
        devices_[address7][reg] = value;
    }

    /// Script consecutive register bytes starting at @p startReg (the device
    /// is added implicitly if absent; the block must fit below 0x100).
    void setRegisters(uint8_t address7, uint8_t startReg,
                      const std::vector<uint8_t>& values)
    {
        auto& map = devices_[address7];
        for (size_t i = 0; i < values.size(); ++i) {
            map[startReg + i] = values[i];
        }
    }

    /**
     * @brief Queue the outcome of the NEXT readRegisters() call on a
     * PRESENT device (FIFO; empty queue = success). Queue e.g.
     * {false, true} for a fail-then-recover scenario.
     */
    void queueReadOutcome(bool ok) { readOutcomes_.push_back(ok); }

    /// Same as queueReadOutcome() for writeRegister() calls.
    void queueWriteOutcome(bool ok) { writeOutcomes_.push_back(ok); }

    // -- II2cBus -------------------------------------------------------------

    bool probe(uint8_t address7) override
    {
        const bool present = devices_.count(address7) != 0;
        calls.push_back({Call::Type::Probe, address7, 0, 0, 0, present});
        return present;
    }

    bool readRegisters(uint8_t address7, uint8_t startReg, uint8_t* buf,
                       size_t len) override
    {
        Call call{Call::Type::Read, address7, startReg, len, 0, false};
        const auto device = devices_.find(address7);
        if (device == devices_.end() || !nextOutcome(readOutcomes_)) {
            calls.push_back(call);
            return false;
        }
        for (size_t i = 0; i < len; ++i) {
            buf[i] = device->second[static_cast<uint8_t>(startReg + i)];
        }
        call.succeeded = true;
        calls.push_back(call);
        return true;
    }

    bool writeRegister(uint8_t address7, uint8_t reg, uint8_t value) override
    {
        Call call{Call::Type::Write, address7, reg, 1, value, false};
        const auto device = devices_.find(address7);
        if (device == devices_.end() || !nextOutcome(writeOutcomes_)) {
            calls.push_back(call);
            return false;
        }
        device->second[reg] = value;
        call.succeeded = true;
        calls.push_back(call);
        return true;
    }

private:
    /// Consume the front of @p queue; an empty queue means success.
    static bool nextOutcome(std::vector<bool>& queue)
    {
        if (queue.empty()) {
            return true;
        }
        const bool ok = queue.front();
        queue.erase(queue.begin());
        return ok;
    }

    std::map<uint8_t, std::array<uint8_t, 256>> devices_;
    std::vector<bool> readOutcomes_;
    std::vector<bool> writeOutcomes_;
};

#endif /* WATERINGSYSTEM_SENSORS_TESTING_MOCKI2CBUS_H */
