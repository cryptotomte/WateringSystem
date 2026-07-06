// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ApiStatic.cpp
// Implementation of the pure static-asset helpers (feature 010 / PR-10).

#include "api/ApiStatic.h"

#include <cstddef>

namespace api {

std::optional<std::string> sanitizeAssetPath(const std::string& uri)
{
    std::string path = uri;

    // Reject an embedded NUL outright (path-truncation defence).
    if (path.find('\0') != std::string::npos) {
        return std::nullopt;
    }

    // Strip the query string, if any.
    std::size_t query = path.find('?');
    if (query != std::string::npos) {
        path.erase(query);
    }

    // Root (or empty) maps to the SPA entry point.
    if (path.empty() || path == "/") {
        return std::string("index.html");
    }

    // Backslash is never a valid separator in a littlefs asset path.
    if (path.find('\\') != std::string::npos) {
        return std::nullopt;
    }

    // Strip exactly one leading '/'. An empty remainder, or a second
    // leading '/' (a "//..." absolute-escape attempt), is rejected.
    if (path.front() == '/') {
        path.erase(0, 1);
        if (path.empty()) {
            return std::nullopt;
        }
        if (path.front() == '/') {
            return std::nullopt;
        }
    }

    // Reject any whole-segment "..". A segment that merely contains ".."
    // among other characters (e.g. "a..b") is allowed.
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t slash = path.find('/', start);
        std::size_t end = (slash == std::string::npos) ? path.size() : slash;
        if (path.compare(start, end - start, "..") == 0) {
            return std::nullopt;
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }

    return path;
}

const char* contentTypeForPath(const std::string& path)
{
    std::size_t dot = path.rfind('.');
    if (dot == std::string::npos) {
        return "application/octet-stream";
    }

    // Lowercase the extension for a case-insensitive compare.
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }

    if (ext == "html" || ext == "htm") {
        return "text/html";
    }
    if (ext == "js") {
        return "application/javascript";
    }
    if (ext == "css") {
        return "text/css";
    }
    if (ext == "ico") {
        return "image/x-icon";
    }
    if (ext == "json") {
        return "application/json";
    }
    if (ext == "svg") {
        return "image/svg+xml";
    }

    return "application/octet-stream";
}

}  // namespace api
