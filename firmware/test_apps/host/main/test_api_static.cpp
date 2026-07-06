// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// test_api_static.cpp
// Host tests for the pure static-asset helpers (feature 010 / PR-10):
// URI-to-asset-path sanitization and extension-to-Content-Type mapping.

#include <optional>
#include <string>

#include "api/ApiStatic.h"
#include "unity.h"

using namespace api;

static void sanitize_root_maps_to_index(void)
{
    auto res = sanitizeAssetPath("/");
    TEST_ASSERT_TRUE(res.has_value());
    TEST_ASSERT_EQUAL_STRING("index.html", res->c_str());
}

static void sanitize_index_strips_leading_slash(void)
{
    auto res = sanitizeAssetPath("/index.html");
    TEST_ASSERT_TRUE(res.has_value());
    TEST_ASSERT_EQUAL_STRING("index.html", res->c_str());
}

static void sanitize_nested_asset(void)
{
    auto res = sanitizeAssetPath("/vendor/chart.min.js");
    TEST_ASSERT_TRUE(res.has_value());
    TEST_ASSERT_EQUAL_STRING("vendor/chart.min.js", res->c_str());
}

static void sanitize_strips_query(void)
{
    auto res = sanitizeAssetPath("/x.js?v=2");
    TEST_ASSERT_TRUE(res.has_value());
    TEST_ASSERT_EQUAL_STRING("x.js", res->c_str());
}

static void sanitize_dotdot_within_segment_allowed(void)
{
    auto res = sanitizeAssetPath("/a..b.js");
    TEST_ASSERT_TRUE(res.has_value());
    TEST_ASSERT_EQUAL_STRING("a..b.js", res->c_str());
}

static void sanitize_leading_dotdot_rejected(void)
{
    auto res = sanitizeAssetPath("/../secret");
    TEST_ASSERT_FALSE(res.has_value());
}

static void sanitize_traversal_rejected(void)
{
    auto res = sanitizeAssetPath("/a/../../b");
    TEST_ASSERT_FALSE(res.has_value());
}

static void sanitize_vendor_traversal_rejected(void)
{
    auto res = sanitizeAssetPath("/vendor/../../etc/passwd");
    TEST_ASSERT_FALSE(res.has_value());
}

static void sanitize_embedded_nul_rejected(void)
{
    std::string s = "/x";
    s.push_back('\0');
    s += "y";
    auto res = sanitizeAssetPath(s);
    TEST_ASSERT_FALSE(res.has_value());
}

static void sanitize_backslash_rejected(void)
{
    auto res = sanitizeAssetPath("/a\\b");
    TEST_ASSERT_FALSE(res.has_value());
}

static void content_type_html(void)
{
    TEST_ASSERT_EQUAL_STRING("text/html", contentTypeForPath("index.html"));
}

static void content_type_js(void)
{
    TEST_ASSERT_EQUAL_STRING("application/javascript", contentTypeForPath("a.js"));
}

static void content_type_css(void)
{
    TEST_ASSERT_EQUAL_STRING("text/css", contentTypeForPath("a.css"));
}

static void content_type_ico(void)
{
    TEST_ASSERT_EQUAL_STRING("image/x-icon", contentTypeForPath("f.ico"));
}

static void content_type_json(void)
{
    TEST_ASSERT_EQUAL_STRING("application/json", contentTypeForPath("d.json"));
}

static void content_type_svg(void)
{
    TEST_ASSERT_EQUAL_STRING("image/svg+xml", contentTypeForPath("i.svg"));
}

static void content_type_unknown_extension(void)
{
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", contentTypeForPath("x.bin"));
}

static void content_type_no_extension(void)
{
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", contentTypeForPath("noext"));
}

void run_api_static_tests(void)
{
    RUN_TEST(sanitize_root_maps_to_index);
    RUN_TEST(sanitize_index_strips_leading_slash);
    RUN_TEST(sanitize_nested_asset);
    RUN_TEST(sanitize_strips_query);
    RUN_TEST(sanitize_dotdot_within_segment_allowed);
    RUN_TEST(sanitize_leading_dotdot_rejected);
    RUN_TEST(sanitize_traversal_rejected);
    RUN_TEST(sanitize_vendor_traversal_rejected);
    RUN_TEST(sanitize_embedded_nul_rejected);
    RUN_TEST(sanitize_backslash_rejected);
    RUN_TEST(content_type_html);
    RUN_TEST(content_type_js);
    RUN_TEST(content_type_css);
    RUN_TEST(content_type_ico);
    RUN_TEST(content_type_json);
    RUN_TEST(content_type_svg);
    RUN_TEST(content_type_unknown_extension);
    RUN_TEST(content_type_no_extension);
}
