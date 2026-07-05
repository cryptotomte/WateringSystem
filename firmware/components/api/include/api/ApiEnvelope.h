// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ApiEnvelope.h
 * @brief Pure JSON response-envelope builders for the /api/v1/ API (host+target).
 *
 * Every API response shares one envelope shape (contracts/api-envelope-and-
 * routes.md):
 *   - success: `{ "success": true, ...payload }`
 *   - error:   `{ "success": false, "error": "<message>" }`
 *   - unknown `/api/<path>`: the error envelope with message "not found" (status 404).
 *
 * These helpers are PURE C++ over cJSON (the IDF `json` component, which links
 * on the linux preview target) — NO esp_http_server / esp_* dependency — so the
 * exact byte output is deterministic and host-tested. The thin target-only
 * ApiServer maps the ApiStatus enum onto the HTTP status line and sends the
 * returned string body.
 */

#ifndef WATERINGSYSTEM_API_APIENVELOPE_H
#define WATERINGSYSTEM_API_APIENVELOPE_H

#include <string>

#include "cJSON.h"

namespace api {

/**
 * @brief HTTP status codes the API emits (mapped to the status line by ApiServer).
 *
 * Kept small and pure so handlers/tests reason about outcomes without pulling
 * in esp_http_server's httpd status constants.
 */
enum class ApiStatus {
    Ok = 200,             ///< successful request
    BadRequest = 400,     ///< malformed JSON or failed validation
    NotFound = 404,       ///< unknown `/api/<path>` route or unknown resource name
    NotImplemented = 501, ///< contract stub (e.g. OTA — PR-13)
    Unavailable = 503     ///< a required subsystem is unavailable
};

/**
 * @brief Build the success envelope `{ "success": true, ...payload }`.
 *
 * Takes OWNERSHIP of @p payload: each member of the payload object is moved
 * (spread) into the envelope alongside `"success": true`, then @p payload is
 * deleted. @p payload must be a cJSON object (or nullptr for an empty payload);
 * a non-object is ignored and its storage freed.
 *
 * @param payload cJSON object whose members become top-level response fields;
 *                ownership transfers to this function (freed before return).
 * @return the serialized JSON body (`cJSON_PrintUnformatted`).
 */
std::string successBody(cJSON* payload);

/**
 * @brief Build the error envelope `{ "success": false, "error": "<message>" }`.
 *
 * @param message human-readable error text (never a secret; wifi password is
 *                never serialized anywhere in the API).
 * @return the serialized JSON body.
 */
std::string errorBody(const std::string& message);

/**
 * @brief The canonical 404 body for an unknown `/api/<path>` route.
 *
 * Equivalent to `errorBody("not found")`; the ApiServer 404 handler returns
 * this with ApiStatus::NotFound so unknown routes answer JSON, not HTML
 * (parity §4).
 */
std::string notFoundBody();

}  // namespace api

#endif /* WATERINGSYSTEM_API_APIENVELOPE_H */
