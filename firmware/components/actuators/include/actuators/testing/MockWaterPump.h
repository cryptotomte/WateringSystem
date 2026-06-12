// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file MockWaterPump.h
 * @brief Host-test pump: records every applyOutput() call (header-only).
 *
 * The mock overrides ONLY the hardware touchpoint; all timing/safety logic
 * under test is the REAL WaterPump base class code (research.md D2).
 * Never compiled into target builds (only included from test code).
 */

#ifndef WATERINGSYSTEM_ACTUATORS_TESTING_MOCKWATERPUMP_H
#define WATERINGSYSTEM_ACTUATORS_TESTING_MOCKWATERPUMP_H

#include <vector>

#include "actuators/WaterPump.h"

/**
 * @brief WaterPump that records output transitions for invariant checks.
 *
 * outputCalls holds every applyOutput() argument in call order, e.g. a full
 * init/start/stop cycle yields {false, true, false}.
 */
class MockWaterPump : public WaterPump {
public:
    using WaterPump::WaterPump;  // same constructors as the base

    /// Every applyOutput() argument, in call order.
    std::vector<bool> outputCalls;

protected:
    bool applyOutput(bool on) override
    {
        outputCalls.push_back(on);
        return true;
    }
};

#endif /* WATERINGSYSTEM_ACTUATORS_TESTING_MOCKWATERPUMP_H */
