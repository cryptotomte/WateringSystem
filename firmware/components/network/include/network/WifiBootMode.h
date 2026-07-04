// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file WifiBootMode.h
 * @brief Pure boot-mode decision (host + target).
 *
 * Decides whether the device comes up in station mode or first-boot/recovery
 * provisioning (feature 007). No IDF includes — part of the header-only
 * surface of the `network` component, compiled on the linux preview target
 * and host-tested against the boot-mode truth table.
 *
 * Normative contract: specs/007-wifi-provisioning/contracts/
 * wifi-manager-states.md ("Boot-mode contract") and data-model.md.
 */

#ifndef WATERINGSYSTEM_NETWORK_WIFIBOOTMODE_H
#define WATERINGSYSTEM_NETWORK_WIFIBOOTMODE_H

/**
 * @brief The mode the WiFi subsystem enters at boot.
 *
 * `Station` joins the stored network; `Provisioning` brings up the SoftAP
 * setup portal (first boot, missing credentials, or an operator-forced
 * recovery via the config button).
 */
enum class WifiBootMode { Station, Provisioning };

/**
 * @brief Decide the boot mode from stored-credential presence and the
 * config-button hold state at boot.
 *
 * Rule (truth table, all four rows host-tested): a held config button OR the
 * absence of stored credentials forces `Provisioning`; only a configured
 * device with the button released comes up in `Station`. On a button-forced
 * provisioning the caller clears the stored credentials first (data-model.md
 * boot rule) — that side effect lives at the wiring site, not here.
 *
 * @param credentialsPresent true when a non-empty SSID is stored.
 * @param configButtonHeld true when the config button is held at boot.
 * @return WifiBootMode::Provisioning or WifiBootMode::Station.
 */
inline WifiBootMode decideBootMode(bool credentialsPresent,
                                   bool configButtonHeld)
{
    if (configButtonHeld || !credentialsPresent) {
        return WifiBootMode::Provisioning;
    }
    return WifiBootMode::Station;
}

/**
 * @brief Whether the stored WiFi credentials must be cleared before entering
 * provisioning at boot.
 *
 * The emergency-reset path (data-model.md boot rule): only when a device that
 * DOES have stored credentials is forced into provisioning by a held config
 * button do we wipe them first, so re-provisioning starts from a clean
 * unconfigured state and cannot silently keep the old network. An already
 * unconfigured device has nothing to clear, and a normal station boot (button
 * released) must never touch the credentials.
 *
 * Pure boolean intent, host-tested; the actual `clearWifiCredentials()` side
 * effect lives at the app_main wiring site (T025).
 *
 * @param credentialsPresent true when a non-empty SSID is stored.
 * @param configButtonHeld true when the config button is held at boot.
 * @return true iff (credentialsPresent && configButtonHeld).
 */
inline bool shouldClearCredentialsOnBoot(bool credentialsPresent,
                                         bool configButtonHeld)
{
    return credentialsPresent && configButtonHeld;
}

#endif /* WATERINGSYSTEM_NETWORK_WIFIBOOTMODE_H */
