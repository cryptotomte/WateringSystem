// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ApiRoutes.h
 * @brief Pure /api/v1/ route table + matcher (host+target).
 *
 * The route table is plain data (path, method, handler id). The target
 * ApiServer registers its OWN httpd_uri_t[] (in ApiServer.cpp); this table and
 * its `matchRoute` matcher are host-test / documentation artifacts, kept in
 * lockstep with that registration and with `docs/api/openapi.yaml` BY HAND.
 * `matchRoute` is a small pure matcher (exact match, plus a prefix rule for the
 * per-pump command route) so routing is host-testable with NO esp_http_server
 * dependency; the host route test asserts `matchRoute`, not the YAML.
 *
 * Contract: contracts/api-envelope-and-routes.md; data-model.md §Route table.
 */

#ifndef WATERINGSYSTEM_API_APIROUTES_H
#define WATERINGSYSTEM_API_APIROUTES_H

#include <cstddef>

namespace api {

/// HTTP verbs the API uses (v1 has no PUT/DELETE/PATCH).
enum class HttpMethod {
    Get,
    Post
};

/**
 * @brief Identifier for the handler bound to each route.
 *
 * `NotFound` is the sentinel returned by matchRoute() for any unknown `/api/<path>`
 * path; it has no table entry (the ApiServer serves it the JSON 404 body).
 */
enum class HandlerId {
    Status,      ///< GET  /api/v1/status
    Sensors,     ///< GET  /api/v1/sensors
    History,     ///< GET  /api/v1/history
    PumpsList,   ///< GET  /api/v1/pumps
    PumpCmd,     ///< POST /api/v1/pumps/{name}
    ConfigGet,   ///< GET  /api/v1/config
    ConfigSet,   ///< POST /api/v1/config
    Power,       ///< GET  /api/v1/power (rev2)
    Events,      ///< GET  /api/v1/events
    SelfTest,    ///< POST /api/v1/selftest
    OtaStub,     ///< POST /api/v1/ota (contract stub, PR-13)
    NotFound     ///< sentinel: unknown /api/<path> route
};

/// One row of the static route table: exact path, method and handler id.
struct ApiRoute {
    const char* path;   ///< exact path (see kPumpCommandPrefix for PumpCmd)
    HttpMethod method;  ///< HTTP verb
    HandlerId id;       ///< handler to dispatch to
};

/**
 * @brief The `/api/v1/pumps/{name}` command prefix.
 *
 * The PumpCmd route matches by this prefix (a non-empty pump name must follow),
 * not by an exact string; its table entry stores the templated form for
 * documentation/enumeration only.
 */
extern const char kPumpCommandPrefix[];

/// Pointer to the first element of the static route table.
const ApiRoute* apiRoutes();

/// Number of entries in the static route table.
std::size_t apiRouteCount();

/**
 * @brief Resolve (@p method, @p path) to a HandlerId.
 *
 * Exact match against the route table for every route except PumpCmd, which
 * matches when @p path begins with `/api/v1/pumps/` and a non-empty name
 * follows. Any other `/api/<path>` path (or a null path) returns HandlerId::NotFound.
 */
HandlerId matchRoute(HttpMethod method, const char* path);

}  // namespace api

#endif /* WATERINGSYSTEM_API_APIROUTES_H */
