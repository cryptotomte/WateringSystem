# Interface Contracts: Modbus Soil Sensor (004)

**Date**: 2026-07-02. These are the host-includable pure C++ contracts (constitution
II). Ported from the legacy headers (`include/communication/IModbusClient.h`,
`include/sensors/ISoilSensor.h`, read-only reference) with the same method surface;
Arduino-era base-class baggage is trimmed to what the new codebase defines.

## interfaces/IModbusClient.h

```cpp
class IModbusClient {
public:
    virtual ~IModbusClient() = default;
    virtual bool initialize() = 0;
    virtual bool readHoldingRegisters(uint8_t deviceAddress, uint16_t startRegister,
                                      uint16_t count, uint16_t* buffer) = 0;
    virtual bool writeSingleRegister(uint8_t deviceAddress, uint16_t registerAddress,
                                     uint16_t value) = 0;
    virtual int getLastError() = 0;              // 0 = OK; see data-model error table
    virtual void setTimeout(uint32_t timeoutMs) = 0;
    virtual void getStatistics(uint32_t* successCount, uint32_t* errorCount) = 0;
};
```

Contract notes:
- `readHoldingRegisters`/`writeSingleRegister` perform exactly ONE bus attempt;
  false ⇒ `getLastError()` is set. No internal retry (parity).
- `writeSingleRegister` success means the addressed slave returned a well-formed
  FC06 response (address/function/CRC validated); implementations are NOT required
  to compare the echoed register/value byte-for-byte against the request (the
  legacy client did — documented parity divergence, see EspModbusClient.cpp and
  plan.md Risks item 6).
- Statistics: every call increments exactly one of success/error.

## interfaces/ISoilSensor.h

```cpp
class ISoilSensor {
public:
    virtual ~ISoilSensor() = default;
    virtual bool initialize() = 0;
    virtual bool read() = 0;                     // one 9-register transaction
    virtual bool isAvailable() = 0;              // real 1-register bus probe (parity)
    virtual int getLastError() = 0;
    // Values from the most recent successful read():
    virtual float getMoisture() = 0;             // %
    virtual float getTemperature() = 0;          // °C, signed
    virtual float getHumidity() = 0;             // ≡ getMoisture() (parity)
    virtual float getPH() = 0;
    virtual float getEC() = 0;                   // µS/cm
    virtual float getNitrogen() = 0;             // mg/kg
    virtual float getPhosphorus() = 0;           // mg/kg
    virtual float getPotassium() = 0;            // mg/kg
    // Calibration (CP1 decision A — legacy semantics):
    virtual bool calibrateMoisture(float referenceValue) = 0;
    virtual bool calibratePH(float referenceValue) = 0;
    virtual bool calibrateEC(float referenceValue) = 0;
};
```

Contract notes:
- `read()` false ⇒ data invalid; last-good getter values MUST NOT be mistaken for
  fresh (PR-11 gates on the read result/error, per spec FR-005).
- Legacy `setValidRange`/`isWithinValidRange` are NOT ported: ranges are the fixed
  parity constants (moisture 0–100, temp −40–80, pH 3–9); no caller in the legacy
  firmware ever changed them at runtime. Recorded as a deliberate trim.
- `LockedSoilSensor` decorator provides per-call mutual exclusion (REPL vs main
  loop), same pattern/caveats as `LockedWaterPump`/`LockedConfigStore`.

## Console contract (HIL surface, FR-013)

| Command | Behavior |
|---|---|
| `soil` | One `read()`; prints all 7 values or error code + name |
| `rs485test` | Raw 1-register probe; prints outcome + success/error counters |
| `soil_cal_moisture <ref>` / `soil_cal_ph <ref>` / `soil_cal_ec <ref>` | Runs calibration; prints success/error plus whether the non-fatal sensor-register write succeeded (no factor value — factors are private, deliberate) |
