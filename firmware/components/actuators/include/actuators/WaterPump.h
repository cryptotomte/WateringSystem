// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file WaterPump.h
 * @brief Pure C++ pump base class: ALL timing and safety logic.
 *
 * Template-method design (research.md D2): timed-run state machine,
 * hard max-runtime enforcement and runtime statistics live here, driven by
 * an injected monotonic clock (ITimeProvider) and a polled update(). The
 * single hardware touchpoint is the pure virtual applyOutput(bool), so the
 * REAL enforcement logic is exercised on the host via MockWaterPump +
 * FakeTimeProvider.
 *
 * This class MUST NOT call esp_timer or any hardware API directly — it is
 * compiled and tested on the IDF linux preview target.
 */

#ifndef WATERINGSYSTEM_ACTUATORS_WATERPUMP_H
#define WATERINGSYSTEM_ACTUATORS_WATERPUMP_H

#include <cstdint>
#include <string>

#include "interfaces/ITimeProvider.h"
#include "interfaces/IWaterPump.h"

/**
 * @brief Pump state machine with max-runtime enforcement (data-model.md).
 *
 * Invariants (host-tested):
 *  1. Output transitions are exactly paired: every ON has exactly one OFF.
 *  2. initialize() drives the output OFF before any other action.
 *  3. No code path keeps the output ON past maxRunTime by more than one
 *     update() poll.
 *  4. Rejected runFor() calls cause no output change and no state change.
 */
class WaterPump : public IWaterPump {
public:
    /// Hard cap on a single run (300 s) — constexpr until NVS config (PR-06).
    static constexpr int64_t kDefaultMaxRunTimeMs = 300'000;

    /**
     * @brief Construct a pump.
     *
     * @param name         Identity for logs/diagnostics ("plant", "reservoir").
     * @param timeProvider Injected monotonic millisecond clock; must outlive
     *                     this object.
     * @param maxRunTimeMs Hard cap on a single run, in milliseconds.
     */
    WaterPump(std::string name, ITimeProvider& timeProvider,
              int64_t maxRunTimeMs = kDefaultMaxRunTimeMs);

    ~WaterPump() override = default;

    WaterPump(const WaterPump&) = delete;
    WaterPump& operator=(const WaterPump&) = delete;

    // IActuator
    bool initialize() override;
    bool isAvailable() const override;
    const std::string& getName() const override;
    int getLastError() const override;

    // IWaterPump
    bool runFor(int durationS) override;
    bool stop() override;
    bool isRunning() const override;
    void update() override;
    int64_t getCurrentRunTimeMs() const override;
    int64_t getAccumulatedRunTimeMs() const override;
    StopReason getLastStopReason() const override;

    /// Hard cap for this instance (for diagnostics/error messages).
    int64_t getMaxRunTimeMs() const { return maxRunTimeMs_; }

protected:
    /**
     * @brief Drive the physical output. The ONLY hardware touchpoint.
     *
     * @param on true = pump on, false = pump off.
     * @return true on success, false on hardware error.
     */
    virtual bool applyOutput(bool on) = 0;

    /// Record a hardware/driver error code (0 clears).
    void setLastError(int error) { lastError_ = error; }

private:
    /// Transition Running -> Stopped: paired applyOutput(false), statistics.
    void stopWith(StopReason reason);

    std::string name_;
    ITimeProvider& timeProvider_;
    int64_t maxRunTimeMs_;

    bool initialized_ = false;
    bool running_ = false;
    int64_t runStartedAtMs_ = 0;
    int64_t runDurationMs_ = 0;
    int64_t accumulatedRunTimeMs_ = 0;
    StopReason lastStopReason_ = StopReason::None;
    int lastError_ = 0;
};

#endif /* WATERINGSYSTEM_ACTUATORS_WATERPUMP_H */
