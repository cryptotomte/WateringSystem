// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file WifiCredentialValidation.h
 * @brief Pure WiFi credential validation (host + target).
 *
 * The single source of truth for "is this SSID/password pair acceptable"
 * (feature 007). Shared by the provisioning portal (POST /wifi/config) and
 * the host test suite. No IDF includes — part of the header-only surface of
 * the `network` component, compiled on the linux preview target.
 *
 * Rules (data-model.md "Validation rules"):
 *  - SSID must be 1..kWifiSsidMaxLen (32) bytes.
 *  - Password is either empty (open network) or kWifiPasswordMinLen (8) ..
 *    kWifiPasswordMaxLen (64) bytes.
 *
 * The result is a typed reason code so callers can render a specific error
 * without re-deriving the rule. Credential VALUES are never logged here.
 */

#ifndef WATERINGSYSTEM_NETWORK_WIFICREDENTIALVALIDATION_H
#define WATERINGSYSTEM_NETWORK_WIFICREDENTIALVALIDATION_H

#include <cstddef>
#include <string>

#include "interfaces/IConfigStore.h"

/**
 * @brief Outcome of validateWifiCredentials().
 *
 * `Ok` is the only accepting value; every other value is a specific reject
 * reason (data-model.md validation rules).
 */
enum class CredentialCheck {
    Ok,               ///< SSID 1..32 and password empty-or-8..64
    SsidEmpty,        ///< SSID is the empty string (unconfigured input)
    SsidTooLong,      ///< SSID exceeds kWifiSsidMaxLen (32) bytes
    PasswordTooShort, ///< non-empty password below kWifiPasswordMinLen (8)
    PasswordTooLong   ///< password exceeds kWifiPasswordMaxLen (64) bytes
};

/// Minimum length of a non-empty WPA2 passphrase (empty = open network).
inline constexpr std::size_t kWifiPasswordMinLen = 8;

/**
 * @brief Validate a candidate SSID/password pair.
 *
 * Rejection order (first failing rule wins): empty SSID, over-long SSID,
 * too-short non-empty password, over-long password. An empty password is
 * accepted (open network).
 *
 * @param ssid Candidate network name.
 * @param password Candidate passphrase (empty for an open network).
 * @return CredentialCheck::Ok when acceptable, otherwise the reject reason.
 */
inline CredentialCheck validateWifiCredentials(const std::string& ssid,
                                               const std::string& password)
{
    if (ssid.empty()) {
        return CredentialCheck::SsidEmpty;
    }
    if (ssid.size() > IConfigStore::kWifiSsidMaxLen) {
        return CredentialCheck::SsidTooLong;
    }
    if (!password.empty() && password.size() < kWifiPasswordMinLen) {
        return CredentialCheck::PasswordTooShort;
    }
    if (password.size() > IConfigStore::kWifiPasswordMaxLen) {
        return CredentialCheck::PasswordTooLong;
    }
    return CredentialCheck::Ok;
}

/// Convenience predicate: true only for CredentialCheck::Ok.
inline bool isValid(CredentialCheck check)
{
    return check == CredentialCheck::Ok;
}

#endif /* WATERINGSYSTEM_NETWORK_WIFICREDENTIALVALIDATION_H */
