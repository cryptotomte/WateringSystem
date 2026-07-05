// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file test_api_routes.cpp
 * @brief Host suite for the pure API route table + matcher (feature 009, T008).
 *
 * Asserts matchRoute() resolves every /api/v1/ route to its HandlerId (the
 * full set, so US2/US3 need not re-touch this file), that POST /pumps/{name}
 * matches by prefix with a non-empty name, and that unknown /api/ paths and a
 * method mismatch resolve to HandlerId::NotFound (the JSON 404 path, FR-004).
 */

#include <cstddef>
#include <cstring>

#include "unity.h"

#include "api/ApiRoutes.h"

namespace {

using api::ApiRoute;
using api::apiRouteCount;
using api::apiRoutes;
using api::HandlerId;
using api::HttpMethod;
using api::matchRoute;

// The expected {path, method} contract set, maintained BY HAND to mirror the
// docs/api/openapi.yaml paths block under its /api/v1 server base (status GET,
// sensors GET, history GET, pumps GET, pumps/{name} POST, config GET, config
// POST, power GET, events GET, selftest POST, ota POST). This array plus the
// two-direction check below is the route/openapi drift barrier (A2): adding,
// removing or re-verbing a route without updating both the table and the
// contract fails the suite.
struct ExpectedRoute {
    const char* path;
    HttpMethod method;
};

const ExpectedRoute kExpectedRoutes[] = {
    {"/api/v1/status",       HttpMethod::Get},
    {"/api/v1/sensors",      HttpMethod::Get},
    {"/api/v1/history",      HttpMethod::Get},
    {"/api/v1/pumps",        HttpMethod::Get},
    {"/api/v1/pumps/{name}", HttpMethod::Post},
    {"/api/v1/config",       HttpMethod::Get},
    {"/api/v1/config",       HttpMethod::Post},
    {"/api/v1/power",        HttpMethod::Get},
    {"/api/v1/events",       HttpMethod::Get},
    {"/api/v1/selftest",     HttpMethod::Post},
    {"/api/v1/ota",          HttpMethod::Post},
};

void test_routes_resolve_to_handlers(void)
{
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Get, "/api/v1/status") ==
                     HandlerId::Status);
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Get, "/api/v1/sensors") ==
                     HandlerId::Sensors);
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Get, "/api/v1/power") ==
                     HandlerId::Power);
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Get, "/api/v1/pumps") ==
                     HandlerId::PumpsList);
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Get, "/api/v1/config") ==
                     HandlerId::ConfigGet);
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Post, "/api/v1/config") ==
                     HandlerId::ConfigSet);
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Get, "/api/v1/history") ==
                     HandlerId::History);
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Get, "/api/v1/events") ==
                     HandlerId::Events);
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Post, "/api/v1/selftest") ==
                     HandlerId::SelfTest);
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Post, "/api/v1/ota") ==
                     HandlerId::OtaStub);
}

void test_pump_command_matches_by_prefix(void)
{
    // POST /api/v1/pumps/{name} — prefix rule with a non-empty name.
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Post, "/api/v1/pumps/plant") ==
                     HandlerId::PumpCmd);
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Post, "/api/v1/pumps/reservoir") ==
                     HandlerId::PumpCmd);
    // A bare prefix with no name is not a command.
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Post, "/api/v1/pumps/") ==
                     HandlerId::NotFound);
    // GET /api/v1/pumps is the list, never the command handler.
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Get, "/api/v1/pumps") ==
                     HandlerId::PumpsList);
}

void test_unknown_paths_are_not_found(void)
{
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Get, "/api/v1/bogus") ==
                     HandlerId::NotFound);
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Get, "/api/unknown") ==
                     HandlerId::NotFound);
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Get, nullptr) ==
                     HandlerId::NotFound);
}

void test_method_mismatch_is_not_found(void)
{
    // The path exists but only under a different verb.
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Post, "/api/v1/status") ==
                     HandlerId::NotFound);
    TEST_ASSERT_TRUE(matchRoute(HttpMethod::Get, "/api/v1/selftest") ==
                     HandlerId::NotFound);
}

void test_route_table_matches_openapi_contract(void)
{
    const ApiRoute* routes = apiRoutes();
    const std::size_t count = apiRouteCount();
    const std::size_t expectedCount =
        sizeof(kExpectedRoutes) / sizeof(kExpectedRoutes[0]);

    // Size parity: no route added or dropped without touching the contract.
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(expectedCount),
                           static_cast<unsigned>(count));

    // Direction 1: every table entry appears in the expected contract set.
    for (std::size_t i = 0; i < count; ++i) {
        bool found = false;
        for (const ExpectedRoute& e : kExpectedRoutes) {
            if (routes[i].method == e.method &&
                std::strcmp(routes[i].path, e.path) == 0) {
                found = true;
                break;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, routes[i].path);
    }

    // Direction 2: every expected contract entry appears in the table.
    for (const ExpectedRoute& e : kExpectedRoutes) {
        bool found = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (routes[i].method == e.method &&
                std::strcmp(routes[i].path, e.path) == 0) {
                found = true;
                break;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, e.path);
    }
}

}  // namespace

void run_api_routes_tests(void)
{
    RUN_TEST(test_routes_resolve_to_handlers);
    RUN_TEST(test_pump_command_matches_by_prefix);
    RUN_TEST(test_unknown_paths_are_not_found);
    RUN_TEST(test_method_mismatch_is_not_found);
    RUN_TEST(test_route_table_matches_openapi_contract);
}
