// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ApiEnvelope.cpp
 * @brief Implementation of the pure JSON response-envelope builders.
 */

#include "api/ApiEnvelope.h"

namespace api {

namespace {

/// Static fallback body for the OOM path: a valid error envelope so a
/// serialization failure never sends an empty 200.
const char* kSerializationFailed =
    "{\"success\":false,\"error\":\"serialization failed\"}";

/// Serialize @p root, free it, and return the compact JSON string. If cJSON
/// cannot allocate the output (or @p root is null), return the static error
/// envelope rather than an empty string, so no handler ever emits an empty 200.
std::string printAndFree(cJSON* root)
{
    char* text = cJSON_PrintUnformatted(root);
    std::string out =
        (text != nullptr) ? std::string(text) : std::string(kSerializationFailed);
    cJSON_free(text);
    cJSON_Delete(root);
    return out;
}

}  // namespace

const char* statusLine(ApiStatus status)
{
    switch (status) {
    case ApiStatus::Ok:
        return "200 OK";
    case ApiStatus::BadRequest:
        return "400 Bad Request";
    case ApiStatus::NotFound:
        return "404 Not Found";
    case ApiStatus::Conflict:
        return "409 Conflict";
    case ApiStatus::InternalError:
        return "500 Internal Server Error";
    case ApiStatus::NotImplemented:
        return "501 Not Implemented";
    }
    return "500 Internal Server Error";
}

std::string successBody(cJSON* payload)
{
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        // OOM building the envelope: free the caller's payload (before any
        // child is detached, so nothing leaks) and emit the static error body
        // rather than sending an empty 200.
        cJSON_Delete(payload);
        return std::string(kSerializationFailed);
    }
    cJSON_AddBoolToObject(root, "success", true);

    // Spread the payload's members into the envelope, then free the (now
    // emptied) payload container. Only objects carry named members; anything
    // else is silently dropped after its storage is released.
    if (payload != nullptr) {
        if (cJSON_IsObject(payload)) {
            cJSON* child = payload->child;
            while (child != nullptr) {
                cJSON* next = child->next;
                cJSON_DetachItemViaPointer(payload, child);
                cJSON_AddItemToObject(root, child->string, child);
                child = next;
            }
        }
        cJSON_Delete(payload);
    }

    return printAndFree(root);
}

std::string errorBody(const std::string& message)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddStringToObject(root, "error", message.c_str());
    return printAndFree(root);
}

std::string notFoundBody()
{
    return errorBody("not found");
}

}  // namespace api
