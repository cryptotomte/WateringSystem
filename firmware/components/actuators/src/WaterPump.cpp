// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file WaterPump.cpp
 * @brief Pump state machine implementation (pure C++, host-testable).
 *
 * Uses ESP_LOG only — the log component is simulated on the IDF linux
 * preview target, so this file builds and runs in the host test suite.
 */

#include "actuators/WaterPump.h"

#include <utility>

#include "esp_log.h"

static const char *TAG = "waterpump";

WaterPump::WaterPump(std::string name, ITimeProvider& timeProvider,
                     int64_t maxRunTimeMs)
    : name_(std::move(name)),
      timeProvider_(timeProvider),
      maxRunTimeMs_(maxRunTimeMs)
{
}

bool WaterPump::initialize()
{
    // Boot fail-safe chain: drive the output OFF before anything else.
    // Idempotent — safe to call again at any time.
    const bool ok = applyOutput(false);
    running_ = false;
    if (!ok) {
        ESP_LOGE(TAG, "%s: initialize failed to drive output OFF",
                 name_.c_str());
        initialized_ = false;
        return false;
    }
    initialized_ = true;
    return true;
}

bool WaterPump::isAvailable() const
{
    return initialized_ && lastError_ == 0;
}

const std::string& WaterPump::getName() const
{
    return name_;
}

int WaterPump::getLastError() const
{
    return lastError_;
}

bool WaterPump::runFor(int durationS)
{
    // Rejections cause no output change and no state change (invariant 4).
    if (durationS <= 0) {
        ESP_LOGW(TAG, "%s: runFor rejected: duration %d s <= 0 "
                 "(no indefinite runs)", name_.c_str(), durationS);
        return false;
    }
    const int64_t durationMs = static_cast<int64_t>(durationS) * 1000;
    if (durationMs > maxRunTimeMs_) {
        ESP_LOGW(TAG, "%s: runFor rejected: duration %d s exceeds max %lld s "
                 "(no silent clamping)", name_.c_str(), durationS,
                 static_cast<long long>(maxRunTimeMs_ / 1000));
        return false;
    }
    if (running_) {
        ESP_LOGW(TAG, "%s: runFor rejected: already running "
                 "(clock not restarted)", name_.c_str());
        return false;
    }

    if (!applyOutput(true)) {
        // Hardware failure on the way ON: force OFF for safety; the pump
        // never enters the Running state.
        ESP_LOGE(TAG, "%s: failed to switch output ON", name_.c_str());
        applyOutput(false);
        return false;
    }
    running_ = true;
    runStartedAtMs_ = timeProvider_.nowMs();
    runDurationMs_ = durationMs;
    ESP_LOGI(TAG, "%s: running for %d s", name_.c_str(), durationS);
    return true;
}

bool WaterPump::stop()
{
    if (!running_) {
        // Stopping a stopped pump is a successful no-op.
        return true;
    }
    stopWith(StopReason::Commanded);
    return true;
}

bool WaterPump::isRunning() const
{
    return running_;
}

void WaterPump::update()
{
    if (!running_) {
        return;
    }
    const int64_t elapsedMs = timeProvider_.nowMs() - runStartedAtMs_;

    // Max-runtime check FIRST: when a 300 s run hits the 300 s cap, the
    // observable reason is MaxRuntimeForced (contract HIL item 4).
    if (elapsedMs >= maxRunTimeMs_) {
        ESP_LOGE(TAG, "%s: max runtime %lld s reached — forcing stop",
                 name_.c_str(),
                 static_cast<long long>(maxRunTimeMs_ / 1000));
        stopWith(StopReason::MaxRuntimeForced);
        return;
    }
    if (elapsedMs >= runDurationMs_) {
        stopWith(StopReason::DurationElapsed);
    }
}

int64_t WaterPump::getCurrentRunTimeMs() const
{
    if (!running_) {
        return 0;
    }
    return timeProvider_.nowMs() - runStartedAtMs_;
}

int64_t WaterPump::getAccumulatedRunTimeMs() const
{
    return accumulatedRunTimeMs_;
}

StopReason WaterPump::getLastStopReason() const
{
    return lastStopReason_;
}

void WaterPump::stopWith(StopReason reason)
{
    // Paired transition (invariant 1): exactly one OFF per ON. Even if the
    // hardware write fails, the state machine stops — never the other way
    // around (fail towards OFF).
    if (!applyOutput(false)) {
        ESP_LOGE(TAG, "%s: failed to switch output OFF", name_.c_str());
    }
    const int64_t ranMs = timeProvider_.nowMs() - runStartedAtMs_;
    accumulatedRunTimeMs_ += ranMs;
    running_ = false;
    lastStopReason_ = reason;
    ESP_LOGI(TAG, "%s: stopped after %lld ms", name_.c_str(),
             static_cast<long long>(ranMs));
}
