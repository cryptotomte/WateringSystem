// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ApiStatic.h
// Pure helpers for serving the static frontend assets from littlefs
// (feature 010 / PR-10). Both functions are PURE C++ with no
// esp_http_server dependency, so they build on the linux preview target
// and are host-tested. The target-only ApiServer uses them to map a
// request URI onto a littlefs asset path and its Content-Type header.

#ifndef WATERINGSYSTEM_API_APISTATIC_H
#define WATERINGSYSTEM_API_APISTATIC_H

#include <optional>
#include <string>

namespace api {

// Map a request URI to a safe relative asset path, or nullopt to reject (404).
// Strips query, maps "/" -> "index.html", strips one leading '/', rejects ".."
// segments, NUL, backslash, and absolute-escape. Does NOT append ".gz".
std::optional<std::string> sanitizeAssetPath(const std::string& uri);

// Content-Type for a path by extension; "application/octet-stream" default.
const char* contentTypeForPath(const std::string& path);

}  // namespace api

#endif  // WATERINGSYSTEM_API_APISTATIC_H
