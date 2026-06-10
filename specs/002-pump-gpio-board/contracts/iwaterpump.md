# Contract: IActuator / IWaterPump / ITimeProvider

**Feature**: 002-pump-gpio-board — C++ interface contract (header-only component
`firmware/components/interfaces/`, no IDF includes allowed in these headers).

Ported from the Arduino interfaces (`include/actuators/IActuator.h`,
`IWaterPump.h`) with Arduino types removed. Semantics below are normative; exact
signatures may be refined during implementation but MUST keep these guarantees.

## ITimeProvider

```cpp
class ITimeProvider {
public:
    virtual ~ITimeProvider() = default;
    virtual int64_t nowMs() = 0;   // monotonic milliseconds; never decreases
};
```

## IActuator

```cpp
class IActuator {
public:
    virtual ~IActuator() = default;
    virtual bool initialize() = 0;          // idempotent; forces safe OFF state
    virtual bool isAvailable() const = 0;
    virtual const std::string& getName() const = 0;
    virtual int getLastError() const = 0;
};
```

## IWaterPump (extends IActuator)

```cpp
class IWaterPump : public IActuator {
public:
    // Start a timed run. Contract:
    //  - durationS <= 0            -> rejected (false): no indefinite runs
    //  - durationS > maxRunTimeS   -> rejected (false): no silent clamping
    //  - already running           -> rejected (false): clock NOT restarted
    //  - success                   -> output ON exactly once, true returned
    virtual bool runFor(int durationS) = 0;

    // Stop. Always allowed; stopping a stopped pump is a successful no-op.
    virtual bool stop() = 0;

    virtual bool isRunning() const = 0;

    // Periodic enforcement; call at main-loop cadence (>= 10 Hz recommended).
    // Stops the pump when duration elapses or max runtime (300 s) is reached.
    // Max-runtime stop is logged at warning/error level and observable via
    // getLastStopReason().
    virtual void update() = 0;

    // Statistics / status (for diagnostics now, API later):
    virtual int64_t getCurrentRunTimeMs() const = 0;   // 0 when stopped
    virtual int64_t getAccumulatedRunTimeMs() const = 0;
    virtual StopReason getLastStopReason() const = 0;  // None|Commanded|DurationElapsed|MaxRuntimeForced
};
```

## Invariants (host-tested)

1. Output transitions are exactly paired: every ON has exactly one OFF; no double-ON.
2. `initialize()` drives output OFF before any other action (boot fail-safe chain).
3. No code path keeps the output ON past `maxRunTime` by more than one update poll.
4. Rejected `runFor` calls cause no output change and no state change.
5. The interfaces component compiles with no IDF/Arduino headers (host-includable).
