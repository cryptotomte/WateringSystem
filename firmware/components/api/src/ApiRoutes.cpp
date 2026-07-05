// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ApiRoutes.cpp
 * @brief The static /api/v1/ route table and the pure matcher.
 */

#include "api/ApiRoutes.h"

#include <cstring>

namespace api {

const char kPumpCommandPrefix[] = "/api/v1/pumps/";

namespace {

// The route table (data-model.md §Route table / contracts path set). The
// PumpCmd row stores the templated path for documentation and enumeration; the
// matcher resolves it by kPumpCommandPrefix rather than by exact string.
constexpr ApiRoute kRoutes[] = {
    {"/api/v1/status",       HttpMethod::Get,  HandlerId::Status},
    {"/api/v1/sensors",      HttpMethod::Get,  HandlerId::Sensors},
    {"/api/v1/history",      HttpMethod::Get,  HandlerId::History},
    {"/api/v1/pumps",        HttpMethod::Get,  HandlerId::PumpsList},
    {"/api/v1/pumps/{name}", HttpMethod::Post, HandlerId::PumpCmd},
    {"/api/v1/config",       HttpMethod::Get,  HandlerId::ConfigGet},
    {"/api/v1/config",       HttpMethod::Post, HandlerId::ConfigSet},
    {"/api/v1/power",        HttpMethod::Get,  HandlerId::Power},
    {"/api/v1/events",       HttpMethod::Get,  HandlerId::Events},
    {"/api/v1/selftest",     HttpMethod::Post, HandlerId::SelfTest},
    {"/api/v1/ota",          HttpMethod::Post, HandlerId::OtaStub},
};

}  // namespace

const ApiRoute* apiRoutes()
{
    return kRoutes;
}

std::size_t apiRouteCount()
{
    return sizeof(kRoutes) / sizeof(kRoutes[0]);
}

HandlerId matchRoute(HttpMethod method, const char* path)
{
    if (path == nullptr) {
        return HandlerId::NotFound;
    }

    // Exact match for every route except the per-pump command (prefix rule).
    for (const ApiRoute& route : kRoutes) {
        if (route.id == HandlerId::PumpCmd) {
            continue;
        }
        if (route.method == method && std::strcmp(route.path, path) == 0) {
            return route.id;
        }
    }

    // POST /api/v1/pumps/{name} — the prefix must be followed by a non-empty
    // name (a bare "/api/v1/pumps" GET is PumpsList, matched exactly above).
    if (method == HttpMethod::Post) {
        const std::size_t prefixLen = std::strlen(kPumpCommandPrefix);
        if (std::strncmp(path, kPumpCommandPrefix, prefixLen) == 0 &&
            path[prefixLen] != '\0') {
            return HandlerId::PumpCmd;
        }
    }

    return HandlerId::NotFound;
}

}  // namespace api
