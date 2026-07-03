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
 * Feature 006 (INA226) extends it with 16-bit writes: writeRegister16()
 * records BIG-ENDIAN into the same per-address byte map (so byte-level
 * assertions stay valid) and into the call log, and shares the write
 * outcome queue with the 8-bit writes (one FIFO covers a mixed-width write
 * sequence).
 *
 * Two register models coexist (feature 006): the BYTE map models
 * auto-incrementing byte registers (BME280 — an N-byte burst walks N
 * consecutive register addresses), while the WORD overlay
 * (setRegister16()) models pointer-addressed 16-bit registers (INA226 — a
 * 2-byte read at one pointer returns THAT register's MSB,LSB and never
 * spills into the numerically next register, so adjacent word registers
 * like 0x02/0x03/0x04 or 0xFE/0xFF do not collide). An exact 2-byte read
 * is served from the word overlay when the register is scripted there;
 * everything else falls through to the byte map — EXCEPT when the read
 * range touches a register whose only truth is the word overlay
 * (setRegister16() scripted, byte map incoherent): that is a mis-shaped
 * test script, and instead of silently serving byte-map zeros the mock
 * fails loudly (abort(), host-only code — see readRegisters()).
 * writeRegister16() keeps the byte map coherent (big-endian mirror), so
 * byte-level reads of driver-written registers remain valid. Never
 * compiled into target builds (only included from test code). No IDF
 * includes.
 */

#ifndef WATERINGSYSTEM_SENSORS_TESTING_MOCKI2CBUS_H
#define WATERINGSYSTEM_SENSORS_TESTING_MOCKI2CBUS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
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
        enum class Type { Probe, Read, Write, Write16 };
        Type type;
        uint8_t address;
        uint8_t reg;    ///< startReg (reads) / register (writes); 0 for probes
        size_t len;     ///< byte count (reads); 1/2 for writes; 0 for probes
        uint8_t value;  ///< written value (8-bit writes); 0 otherwise
        bool succeeded; ///< outcome reported to the caller
        uint16_t value16 = 0;  ///< written value (16-bit writes); 0 otherwise
    };

    // Instrumentation (public, MockModbusClient style).
    std::vector<Call> calls;  ///< every probe/read/write, in call order

    // -- Scripting ----------------------------------------------------------

    /// Make @p address7 present (ACKs probes) with an all-zero register map.
    void addDevice(uint8_t address7) { devices_[address7]; }

    /// Remove @p address7: probes NACK, reads/writes fail (unplug scenario).
    /// The register maps (byte + word) are discarded — re-adding yields a
    /// fresh device.
    void removeDevice(uint8_t address7)
    {
        devices_.erase(address7);
        wordRegisters_.erase(address7);
        byteIncoherentWords_.erase(address7);
    }

    /// Script one register byte on a device (added implicitly if absent).
    void setRegister(uint8_t address7, uint8_t reg, uint8_t value)
    {
        devices_[address7][reg] = value;
    }

    /// Script consecutive register bytes starting at @p startReg (the device
    /// is added implicitly if absent; indices wrap at 0xFF, matching
    /// readRegisters()).
    void setRegisters(uint8_t address7, uint8_t startReg,
                      const std::vector<uint8_t>& values)
    {
        auto& map = devices_[address7];
        for (size_t i = 0; i < values.size(); ++i) {
            map[static_cast<uint8_t>(startReg + i)] = values[i];
        }
    }

    /**
     * @brief Script one 16-bit WORD register (INA226 model — see the file
     * comment: pointer-addressed, adjacent word registers never collide).
     *
     * Served big-endian by readRegisters()/readRegister16() for exact
     * 2-byte reads at @p reg. The device is added implicitly if absent.
     */
    void setRegister16(uint8_t address7, uint8_t reg, uint16_t value)
    {
        devices_[address7];  // implicit add, setRegister() convention
        wordRegisters_[address7][reg] = value;
        // The byte map is deliberately NOT mirrored (a pointer-addressed
        // register has no byte-map representation): mark the register
        // byte-incoherent so a mis-shaped read fails loudly instead of
        // serving byte-map zeros (see readRegisters()).
        byteIncoherentWords_[address7].insert(reg);
    }

    /**
     * @brief Queue the outcome of the NEXT readRegisters() call on a
     * PRESENT device (FIFO; empty queue = success). Queue e.g.
     * {false, true} for a fail-then-recover scenario.
     */
    void queueReadOutcome(bool ok) { readOutcomes_.push_back(ok); }

    /// Same as queueReadOutcome() for writeRegister()/writeRegister16()
    /// calls — ONE shared FIFO covers both widths, in call order, so a
    /// mixed-width write sequence (INA226 config + calibration) scripts
    /// naturally.
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
        // WORD-overlay hit: an exact 2-byte read of a scripted 16-bit
        // register serves that register big-endian (INA226 model — never
        // spills into the numerically next register).
        if (len == 2) {
            const auto words = wordRegisters_.find(address7);
            if (words != wordRegisters_.end()) {
                const auto word = words->second.find(startReg);
                if (word != words->second.end()) {
                    buf[0] = static_cast<uint8_t>(word->second >> 8);
                    buf[1] = static_cast<uint8_t>(word->second & 0xFF);
                    call.succeeded = true;
                    calls.push_back(call);
                    return true;
                }
            }
        }
        // Loud-failure guard (host-only code): a read that is NOT an exact
        // 2-byte overlay hit but whose range touches a register that ONLY
        // exists in the word overlay (setRegister16() scripted, never
        // writeRegister16()-mirrored into the byte map) would silently
        // serve byte-map zeros — always a mis-shaped test script, never a
        // modeled bus behavior. Abort with a message instead; this path
        // can therefore not appear as a passing test, only as a build-time
        // documented contract (see the file comment).
        const auto incoherent = byteIncoherentWords_.find(address7);
        if (incoherent != byteIncoherentWords_.end()) {
            for (size_t i = 0; i < len; ++i) {
                const uint8_t r = static_cast<uint8_t>(startReg + i);
                if (incoherent->second.count(r) != 0) {
                    std::fprintf(
                        stderr,
                        "MockI2cBus: read [reg 0x%02x, len %zu] at addr "
                        "0x%02x overlaps word-scripted register 0x%02x "
                        "without an exact 2-byte hit — mis-shaped test "
                        "script (use readRegister16/exact 2-byte reads)\n",
                        startReg, len, address7, r);
                    std::abort();
                }
            }
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

    bool writeRegister16(uint8_t address7, uint8_t reg,
                         uint16_t value) override
    {
        Call call{Call::Type::Write16, address7, reg, 2, 0, false, value};
        const auto device = devices_.find(address7);
        if (device == devices_.end() || !nextOutcome(writeOutcomes_)) {
            calls.push_back(call);
            return false;
        }
        // BIG-ENDIAN into the per-address byte map, so byte-level
        // assertions (and readRegisters()/readRegister16() round trips)
        // stay valid. Indices wrap at 0xFF via the uint8_t cast — the same
        // convention as setRegisters()/readRegisters().
        device->second[reg] = static_cast<uint8_t>(value >> 8);
        device->second[static_cast<uint8_t>(reg + 1)] =
            static_cast<uint8_t>(value & 0xFF);
        // Also into the WORD overlay, so a write-then-read16 round trip is
        // coherent for pointer-addressed devices (INA226 config readback).
        // The byte map is now coherent for this register — clear any
        // setRegister16() incoherence mark (loud-failure guard above).
        wordRegisters_[address7][reg] = value;
        byteIncoherentWords_[address7].erase(reg);
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
    /// Pointer-addressed 16-bit registers (setRegister16(), INA226 model);
    /// consulted only for exact 2-byte reads.
    std::map<uint8_t, std::map<uint8_t, uint16_t>> wordRegisters_;
    /// Word registers whose byte-map view is NOT valid (setRegister16()
    /// scripted and not since writeRegister16()-mirrored): any non-exact
    /// read touching one aborts loudly (see readRegisters()).
    std::map<uint8_t, std::set<uint8_t>> byteIncoherentWords_;
    std::vector<bool> readOutcomes_;
    std::vector<bool> writeOutcomes_;
};

#endif /* WATERINGSYSTEM_SENSORS_TESTING_MOCKI2CBUS_H */
