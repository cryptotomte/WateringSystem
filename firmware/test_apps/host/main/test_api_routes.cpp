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

#include "unity.h"

#include "api/ApiRoutes.h"

namespace {

using api::HandlerId;
using api::HttpMethod;
using api::matchRoute;

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

}  // namespace

void run_api_routes_tests(void)
{
    RUN_TEST(test_routes_resolve_to_handlers);
    RUN_TEST(test_pump_command_matches_by_prefix);
    RUN_TEST(test_unknown_paths_are_not_found);
    RUN_TEST(test_method_mismatch_is_not_found);
}
