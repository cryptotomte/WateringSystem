// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ApiEnvelope.cpp
 * @brief Implementation of the pure JSON response-envelope builders.
 */

#include "api/ApiEnvelope.h"

namespace api {

namespace {

/// Serialize @p root, free it, and return the compact JSON string.
std::string printAndFree(cJSON* root)
{
    char* text = cJSON_PrintUnformatted(root);
    std::string out = (text != nullptr) ? std::string(text) : std::string();
    cJSON_free(text);
    cJSON_Delete(root);
    return out;
}

}  // namespace

std::string successBody(cJSON* payload)
{
    cJSON* root = cJSON_CreateObject();
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
