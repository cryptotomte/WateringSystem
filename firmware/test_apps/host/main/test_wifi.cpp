// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_wifi.cpp
 * @brief Unity host suite for WiFi provisioning & station management
 * (feature 007).
 *
 * US1 (tasks T008/T009): the pure credential validation rules and the
 * boot-mode truth table, the two host-testable pieces of the provisioning
 * MVP. Normative sources: specs/007-wifi-provisioning/data-model.md
 * ("Validation rules") and contracts/wifi-manager-states.md ("Boot-mode
 * contract"). The reconnect schedule, isolation and no-boot-loop tests
 * arrive with US2/US3 (T015+), exercising the pure WifiManager over
 * MockWifiDriver + FakeTimeProvider.
 *
 * Scoped-enum comparisons are wrapped in static_cast<int> (project Unity
 * convention, see test_soil_sensor.cpp).
 */

#include <string>

#include "unity.h"

#include "actuators/testing/FakeTimeProvider.h"
#include "network/WifiBootMode.h"
#include "network/WifiCredentialValidation.h"
#include "network/WifiManager.h"
#include "network/WifiState.h"
#include "network/testing/MockWifiDriver.h"
#include "storage/testing/MockConfigStore.h"

namespace {

/// static_cast<int> a CredentialCheck for Unity's integer comparison.
int checkInt(CredentialCheck c) { return static_cast<int>(c); }

/// static_cast<int> a WifiBootMode for Unity's integer comparison.
int modeInt(WifiBootMode m) { return static_cast<int>(m); }

/// static_cast<int> a WifiState for Unity's integer comparison.
int stateInt(WifiState s) { return static_cast<int>(s); }

}  // namespace

// ---------------------------------------------------------------------------
// T008 — validateWifiCredentials: SSID length rules
// (empty→reject, 1..32→accept, >32→reject; data-model.md validation rules)
// ---------------------------------------------------------------------------
static void test_validate_ssid_length_rules(void)
{
    // Empty SSID is rejected regardless of the password (checked first).
    TEST_ASSERT_EQUAL(checkInt(CredentialCheck::SsidEmpty),
                      checkInt(validateWifiCredentials("", "")));

    // A 1-char SSID with an (accepted) empty password is Ok.
    TEST_ASSERT_EQUAL(checkInt(CredentialCheck::Ok),
                      checkInt(validateWifiCredentials("a", "")));

    // The 32-byte upper bound is accepted.
    TEST_ASSERT_EQUAL(checkInt(CredentialCheck::Ok),
                      checkInt(validateWifiCredentials(std::string(32, 's'), "")));

    // 33 bytes is one over the bound → SsidTooLong.
    TEST_ASSERT_EQUAL(checkInt(CredentialCheck::SsidTooLong),
                      checkInt(validateWifiCredentials(std::string(33, 's'), "")));
}

// ---------------------------------------------------------------------------
// T008 — validateWifiCredentials: password length rules
// (empty→accept, 1..7→reject, 8→accept, 64→accept, 65→reject)
// ---------------------------------------------------------------------------
static void test_validate_password_length_rules(void)
{
    const std::string ssid = "net";

    // Empty password = open network → accepted.
    TEST_ASSERT_EQUAL(checkInt(CredentialCheck::Ok),
                      checkInt(validateWifiCredentials(ssid, "")));

    // Every non-empty length 1..7 is too short for WPA2.
    for (std::size_t len = 1; len <= 7; ++len) {
        TEST_ASSERT_EQUAL(
            checkInt(CredentialCheck::PasswordTooShort),
            checkInt(validateWifiCredentials(ssid, std::string(len, 'p'))));
    }

    // 8 bytes is the minimum accepted passphrase.
    TEST_ASSERT_EQUAL(
        checkInt(CredentialCheck::Ok),
        checkInt(validateWifiCredentials(ssid, std::string(8, 'p'))));

    // 64 bytes is the upper bound and accepted.
    TEST_ASSERT_EQUAL(
        checkInt(CredentialCheck::Ok),
        checkInt(validateWifiCredentials(ssid, std::string(64, 'p'))));

    // 65 bytes is one over the bound → PasswordTooLong.
    TEST_ASSERT_EQUAL(
        checkInt(CredentialCheck::PasswordTooLong),
        checkInt(validateWifiCredentials(ssid, std::string(65, 'p'))));
}

// ---------------------------------------------------------------------------
// T008 — isValid() convenience predicate agrees with the reason codes
// ---------------------------------------------------------------------------
static void test_validate_isvalid_predicate(void)
{
    TEST_ASSERT_TRUE(isValid(validateWifiCredentials("net", "password1")));
    TEST_ASSERT_TRUE(isValid(CredentialCheck::Ok));
    TEST_ASSERT_FALSE(isValid(CredentialCheck::SsidEmpty));
    TEST_ASSERT_FALSE(isValid(CredentialCheck::SsidTooLong));
    TEST_ASSERT_FALSE(isValid(CredentialCheck::PasswordTooShort));
    TEST_ASSERT_FALSE(isValid(CredentialCheck::PasswordTooLong));
}

// ---------------------------------------------------------------------------
// T009 — decideBootMode: all four rows of the truth table
// (contract wifi-manager-states.md "Boot-mode contract")
// ---------------------------------------------------------------------------
static void test_boot_mode_truth_table(void)
{
    // credentialsPresent=false, buttonHeld=false → Provisioning.
    TEST_ASSERT_EQUAL(modeInt(WifiBootMode::Provisioning),
                      modeInt(decideBootMode(false, false)));

    // credentialsPresent=false, buttonHeld=true → Provisioning.
    TEST_ASSERT_EQUAL(modeInt(WifiBootMode::Provisioning),
                      modeInt(decideBootMode(false, true)));

    // credentialsPresent=true, buttonHeld=false → Station.
    TEST_ASSERT_EQUAL(modeInt(WifiBootMode::Station),
                      modeInt(decideBootMode(true, false)));

    // credentialsPresent=true, buttonHeld=true → Provisioning (emergency).
    TEST_ASSERT_EQUAL(modeInt(WifiBootMode::Provisioning),
                      modeInt(decideBootMode(true, true)));
}

// ===========================================================================
// US2 — pure WifiManager over MockWifiDriver + FakeTimeProvider (T015/T016).
//
// FR-014 isolation, documented and structurally enforced: WifiManager's
// constructor takes ONLY (IWifiDriver&, IConfigStore&, ITimeProvider&,
// ReconnectPolicy) — no watering/pump/sensor type. Every construction below
// is that exact four-argument signature; the type does not compile with a
// watering reference (there is no such parameter to pass one to).
// ===========================================================================

namespace {

/// Seed a MockConfigStore with a valid SSID/password pair for STA attempts.
void seedCredentials(MockConfigStore& config)
{
    config.setWifiCredentials("net", "password1");
}

}  // namespace

// ---------------------------------------------------------------------------
// T015 #1 — connect happy path + connected-health rssi monitor cadence.
// (contract wifi-manager-states.md §Timing 1 and §Timing 4 rssi refresh)
// ---------------------------------------------------------------------------
static void test_wifi_connect_happy_path(void)
{
    MockWifiDriver driver;
    MockConfigStore config;
    FakeTimeProvider clock;
    seedCredentials(config);

    WifiManager manager(driver, config, clock, ReconnectPolicy{});

    // begin(Station) issues exactly one STA attempt (#1) with the stored creds.
    manager.begin(WifiBootMode::Station);
    TEST_ASSERT_EQUAL(1, driver.staConnectCalls);
    TEST_ASSERT_EQUAL_STRING("net", driver.lastStaSsid.c_str());
    TEST_ASSERT_EQUAL_STRING("password1", driver.lastStaPassword.c_str());

    // Connected (L2) then GotIp → fully connected, failures reset, ip acquired.
    driver.setRssi(-50);
    driver.scriptConnectSuccess();
    manager.tick();

    WifiConnectionSnapshot snap = manager.snapshot();
    TEST_ASSERT_EQUAL(stateInt(WifiState::Connected), stateInt(snap.state));
    TEST_ASSERT_EQUAL_UINT8(0, snap.consecutiveFailures);
    TEST_ASSERT_TRUE(snap.ipAcquired);
    TEST_ASSERT_EQUAL_INT8(-50, snap.rssi);  // primed on GotIp

    // Health monitor refreshes rssi only once monitorIntervalMs (5 s) elapses.
    driver.setRssi(-60);
    clock.advance(4999);
    manager.tick();
    TEST_ASSERT_EQUAL_INT8(-50, manager.snapshot().rssi);  // not yet
    clock.advance(1);  // exactly 5000 ms since GotIp
    manager.tick();
    TEST_ASSERT_EQUAL_INT8(-60, manager.snapshot().rssi);  // refreshed
    // No spurious STA attempts while connected.
    TEST_ASSERT_EQUAL(1, driver.staConnectCalls);
}

// ---------------------------------------------------------------------------
// T015 #2 — retry only at 10 s: no attempt at 9999 ms, one at 10000 ms.
// (contract §Timing 2)
// ---------------------------------------------------------------------------
static void test_wifi_retry_only_at_10s(void)
{
    MockWifiDriver driver;
    MockConfigStore config;
    FakeTimeProvider clock;
    seedCredentials(config);

    WifiManager manager(driver, config, clock, ReconnectPolicy{});
    manager.begin(WifiBootMode::Station);  // attempt #1
    TEST_ASSERT_EQUAL(1, driver.staConnectCalls);

    // First failure → Reconnecting; the retry is scheduled 10 s out, so the
    // same tick does NOT re-attempt.
    driver.scriptConnectFailure();
    manager.tick();
    TEST_ASSERT_EQUAL(stateInt(WifiState::Reconnecting),
                      stateInt(manager.snapshot().state));
    TEST_ASSERT_EQUAL_UINT8(1, manager.snapshot().consecutiveFailures);
    TEST_ASSERT_EQUAL(1, driver.staConnectCalls);

    // 1 ms short of the interval: still no new attempt.
    clock.advance(9999);
    manager.tick();
    TEST_ASSERT_EQUAL(1, driver.staConnectCalls);

    // At exactly 10 000 ms: exactly one new attempt, back to Connecting, and
    // the failure count is NOT reset on a retry.
    clock.advance(1);
    manager.tick();
    TEST_ASSERT_EQUAL(2, driver.staConnectCalls);
    TEST_ASSERT_EQUAL(stateInt(WifiState::Connecting),
                      stateInt(manager.snapshot().state));
    TEST_ASSERT_EQUAL_UINT8(1, manager.snapshot().consecutiveFailures);
}

// ---------------------------------------------------------------------------
// T015 #3 — pause after 5 consecutive failures: no attempts during the 60 s
// pause; a fresh attempt with failures reset only after it elapses.
// (contract §Timing 3)
// ---------------------------------------------------------------------------
static void test_wifi_pause_after_5_failures(void)
{
    MockWifiDriver driver;
    MockConfigStore config;
    FakeTimeProvider clock;
    seedCredentials(config);

    WifiManager manager(driver, config, clock, ReconnectPolicy{});
    manager.begin(WifiBootMode::Station);  // attempt #1

    // Drive 5 consecutive failures, advancing 10 s to issue each retry between
    // them. After the 5th failure the machine enters ReconnectPaused.
    for (int i = 1; i <= 5; ++i) {
        driver.scriptConnectFailure();
        manager.tick();
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(i),
                                manager.snapshot().consecutiveFailures);
        if (i < 5) {
            TEST_ASSERT_EQUAL(stateInt(WifiState::Reconnecting),
                              stateInt(manager.snapshot().state));
            clock.advance(10000);
            manager.tick();  // issues retry #(i+1)
        }
    }

    // Attempts so far: begin (#1) + four retries (#2..#5) = 5.
    TEST_ASSERT_EQUAL(stateInt(WifiState::ReconnectPaused),
                      stateInt(manager.snapshot().state));
    TEST_ASSERT_EQUAL(5, driver.staConnectCalls);

    // No attempt anywhere inside the pause window (1 ms short of 60 s).
    clock.advance(59999);
    manager.tick();
    TEST_ASSERT_EQUAL(5, driver.staConnectCalls);
    TEST_ASSERT_EQUAL(stateInt(WifiState::ReconnectPaused),
                      stateInt(manager.snapshot().state));

    // At 60 000 ms: exactly one new attempt, failures reset for the new round.
    clock.advance(1);
    manager.tick();
    TEST_ASSERT_EQUAL(6, driver.staConnectCalls);
    TEST_ASSERT_EQUAL(stateInt(WifiState::Connecting),
                      stateInt(manager.snapshot().state));
    TEST_ASSERT_EQUAL_UINT8(0, manager.snapshot().consecutiveFailures);
}

// ---------------------------------------------------------------------------
// T015 #4 — monitor/disconnect: a drop from Connected returns to Reconnecting
// and increments disconnectCount. (contract §Timing 4)
// ---------------------------------------------------------------------------
static void test_wifi_monitor_disconnect(void)
{
    MockWifiDriver driver;
    MockConfigStore config;
    FakeTimeProvider clock;
    seedCredentials(config);

    WifiManager manager(driver, config, clock, ReconnectPolicy{});
    manager.begin(WifiBootMode::Station);
    driver.scriptConnectSuccess();
    manager.tick();
    TEST_ASSERT_EQUAL(stateInt(WifiState::Connected),
                      stateInt(manager.snapshot().state));
    TEST_ASSERT_EQUAL_UINT32(0, manager.snapshot().disconnectCount);

    // A link drop while connected: Reconnecting, disconnectCount++, ip lost.
    driver.queueEvent(WifiEvent::Disconnected);
    manager.tick();

    WifiConnectionSnapshot snap = manager.snapshot();
    TEST_ASSERT_EQUAL(stateInt(WifiState::Reconnecting), stateInt(snap.state));
    TEST_ASSERT_EQUAL_UINT32(1, snap.disconnectCount);
    TEST_ASSERT_EQUAL_UINT8(1, snap.consecutiveFailures);
    TEST_ASSERT_FALSE(snap.ipAcquired);
}

// ---------------------------------------------------------------------------
// T015 #5 — AP/provisioning mode suspends all STA monitoring and reconnect,
// regardless of elapsed time, and never leaves Provisioning. (contract §6)
// ---------------------------------------------------------------------------
static void test_wifi_ap_mode_suspends(void)
{
    MockWifiDriver driver;
    MockConfigStore config;
    FakeTimeProvider clock;
    seedCredentials(config);

    WifiManager manager(driver, config, clock, ReconnectPolicy{});

    // begin(Provisioning) makes NO driver calls (AP radio is brought up in
    // T018 at the wiring site).
    manager.begin(WifiBootMode::Provisioning);
    TEST_ASSERT_EQUAL(0, driver.staConnectCalls);
    TEST_ASSERT_EQUAL(0, driver.apStartCalls);

    // Many ticks with large time advances issue ZERO STA attempts and never
    // leave Provisioning.
    for (int i = 0; i < 20; ++i) {
        clock.advance(60000);
        manager.tick();
    }
    TEST_ASSERT_EQUAL(0, driver.staConnectCalls);
    TEST_ASSERT_EQUAL(stateInt(WifiState::Provisioning),
                      stateInt(manager.snapshot().state));
}

// ---------------------------------------------------------------------------
// T016 — FR-014 isolation: tick() never blocks with a silent/"hung" driver
// (pollEvent() always None, staConnect yields no events) and issues no phantom
// retries while Connecting; the state stays sane over many advances.
//
// Dependency set (documented FR-014 guarantee): the WifiManager below is
// constructed with exactly IWifiDriver&, IConfigStore&, ITimeProvider& and a
// ReconnectPolicy value — there is no watering/pump/sensor parameter, so the
// manager cannot reference watering state even in principle.
// ---------------------------------------------------------------------------
static void test_wifi_isolation_no_block_no_watering_dep(void)
{
    MockWifiDriver driver;  // empty queue: pollEvent() always returns None
    MockConfigStore config;
    FakeTimeProvider clock;
    seedCredentials(config);

    WifiManager manager(driver, config, clock, ReconnectPolicy{});
    manager.begin(WifiBootMode::Station);
    TEST_ASSERT_EQUAL(1, driver.staConnectCalls);

    // 1000 ticks over ~1000 s: each returns promptly (no hang — tick() has no
    // loops that wait on the driver). With no events the machine sits in
    // Connecting and issues no further attempts.
    for (int i = 0; i < 1000; ++i) {
        clock.advance(1000);
        manager.tick();
    }

    WifiConnectionSnapshot snap = manager.snapshot();
    TEST_ASSERT_EQUAL(stateInt(WifiState::Connecting), stateInt(snap.state));
    TEST_ASSERT_EQUAL(1, driver.staConnectCalls);  // no phantom retries
    TEST_ASSERT_EQUAL_UINT8(0, snap.consecutiveFailures);
}

// ===========================================================================
// US3 — recovery paths (T022/T023).
// ===========================================================================

// ---------------------------------------------------------------------------
// T022 — no boot loop under permanent failure (FR-013). An infinite
// ConnectFailed stream over many rounds keeps the machine in the failure cycle
// (Connecting / Reconnecting / ReconnectPaused) forever: it never reaches
// Connected, never falls back to Provisioning, never requests a restart (the
// pure WifiManager has no reboot surface at all), and its counters stay bounded
// (consecutiveFailures never exceeds failuresBeforePause — reset to 0 when the
// pause elapses; no overflow/UB). (contract §Timing 5)
// ---------------------------------------------------------------------------
static void test_wifi_no_boot_loop_under_permanent_failure(void)
{
    MockWifiDriver driver;
    MockConfigStore config;
    FakeTimeProvider clock;
    seedCredentials(config);

    const ReconnectPolicy policy{};  // parity defaults (10 s / 5 / 60 s)
    WifiManager manager(driver, config, clock, policy);
    manager.begin(WifiBootMode::Station);  // attempt #1 → Connecting

    // Drive 800 rounds of pure failure. Each round either fails an in-flight
    // Connecting attempt or advances past whatever wait the machine is in
    // (60 s covers both the 10 s retry and the 60 s pause), so the machine
    // marches Connecting → Reconnecting → … → ReconnectPaused → Connecting
    // indefinitely. One ReconnectPaused occurs roughly every 10 rounds (5
    // failures + their retries), so 800 rounds drives well over 50 retry/pause
    // cycles.
    int pausesSeen = 0;
    for (int round = 0; round < 800; ++round) {
        if (manager.snapshot().state == WifiState::Connecting) {
            driver.scriptConnectFailure();
        } else {
            clock.advance(60000);
        }
        manager.tick();

        const WifiConnectionSnapshot snap = manager.snapshot();
        // Only the three failure-cycle states are ever legal here: no
        // Connected (nothing ever succeeds) and no Provisioning (the machine
        // never reboots into a different boot mode) — FR-013 no boot loop.
        const bool inCycle = snap.state == WifiState::Connecting ||
                             snap.state == WifiState::Reconnecting ||
                             snap.state == WifiState::ReconnectPaused;
        TEST_ASSERT_TRUE(inCycle);
        // Counter never exceeds the pause threshold (bounded — no overflow).
        TEST_ASSERT_TRUE(snap.consecutiveFailures <= policy.failuresBeforePause);
        if (snap.state == WifiState::ReconnectPaused) {
            ++pausesSeen;
        }
    }

    // The pause path was exercised many times (proving real retry/pause
    // cycling, not a stuck state), and the machine is still in the failure
    // cycle with a bounded counter at the end.
    TEST_ASSERT_TRUE(pausesSeen >= 50);
    const WifiConnectionSnapshot finalSnap = manager.snapshot();
    TEST_ASSERT_TRUE(finalSnap.state != WifiState::Connected &&
                     finalSnap.state != WifiState::Provisioning);
    TEST_ASSERT_TRUE(finalSnap.consecutiveFailures <= policy.failuresBeforePause);
}

// ---------------------------------------------------------------------------
// T023 — emergency-reset boot decision + the paired clear-credentials intent.
// decideBootMode forces Provisioning when the config button is held on a
// configured device; the pure shouldClearCredentialsOnBoot helper (wired into
// app_main's provisioning branch, T025) is true ONLY when both hold, so a
// forced re-provisioning starts from a clean unconfigured state. (contract
// "Boot-mode contract" row 4 + data-model.md boot rule)
// ---------------------------------------------------------------------------
static void test_wifi_emergency_reset_clear_intent(void)
{
    // The emergency-reset entry: configured device + button held → Provisioning.
    TEST_ASSERT_EQUAL(modeInt(WifiBootMode::Provisioning),
                      modeInt(decideBootMode(true, true)));

    // Clear-credentials intent: true only for (configured && button held).
    TEST_ASSERT_TRUE(shouldClearCredentialsOnBoot(true, true));    // forced reset
    TEST_ASSERT_FALSE(shouldClearCredentialsOnBoot(true, false));  // normal station
    TEST_ASSERT_FALSE(shouldClearCredentialsOnBoot(false, true));  // nothing stored
    TEST_ASSERT_FALSE(shouldClearCredentialsOnBoot(false, false)); // first boot
}

void run_wifi_tests(void)
{
    // T008 — credential validation.
    RUN_TEST(test_validate_ssid_length_rules);
    RUN_TEST(test_validate_password_length_rules);
    RUN_TEST(test_validate_isvalid_predicate);
    // T009 — boot-mode truth table.
    RUN_TEST(test_boot_mode_truth_table);
    // T015 — WifiManager reconnect schedule.
    RUN_TEST(test_wifi_connect_happy_path);
    RUN_TEST(test_wifi_retry_only_at_10s);
    RUN_TEST(test_wifi_pause_after_5_failures);
    RUN_TEST(test_wifi_monitor_disconnect);
    RUN_TEST(test_wifi_ap_mode_suspends);
    // T016 — FR-014 isolation / non-blocking tick.
    RUN_TEST(test_wifi_isolation_no_block_no_watering_dep);
    // T022 — no boot loop under permanent failure (FR-013).
    RUN_TEST(test_wifi_no_boot_loop_under_permanent_failure);
    // T023 — emergency-reset boot decision + clear-credentials intent.
    RUN_TEST(test_wifi_emergency_reset_clear_intent);
}
