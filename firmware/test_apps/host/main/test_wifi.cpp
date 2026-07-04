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

#include "network/WifiBootMode.h"
#include "network/WifiCredentialValidation.h"

namespace {

/// static_cast<int> a CredentialCheck for Unity's integer comparison.
int checkInt(CredentialCheck c) { return static_cast<int>(c); }

/// static_cast<int> a WifiBootMode for Unity's integer comparison.
int modeInt(WifiBootMode m) { return static_cast<int>(m); }

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

void run_wifi_tests(void)
{
    // T008 — credential validation.
    RUN_TEST(test_validate_ssid_length_rules);
    RUN_TEST(test_validate_password_length_rules);
    RUN_TEST(test_validate_isvalid_predicate);
    // T009 — boot-mode truth table.
    RUN_TEST(test_boot_mode_truth_table);
}
