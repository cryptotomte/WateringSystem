// SPDX-FileCopyrightText: 2026 Cryptotomte
// SPDX-License-Identifier: AGPL-3.0-or-later
/**
 * @file ProvisioningPortal.cpp
 * @brief First-boot SoftAP setup portal implementation (target-only).
 *
 * See ProvisioningPortal.h and specs/007-wifi-provisioning/contracts/
 * provisioning-portal.md. Excluded from the linux build (esp_http_server has
 * no host port). The only reusable policy — credential validation — is the
 * pure, host-tested validateWifiCredentials(); this file holds the HTTP
 * plumbing (file-local route handlers, urlencoded form parsing) and the
 * post-save restart scheduling. The server handle is the only IDF type that
 * touches this class and is kept opaque in the header (PRIV rule). Credential
 * VALUES are never logged or echoed (FR-004).
 */

#include "network/ProvisioningPortal.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <utility>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "network/WifiCredentialValidation.h"

namespace {

const char* TAG = "prov_portal";

/// Cap on the accepted POST body: two short fields plus urlencoding overhead.
/// Larger bodies are rejected rather than buffered (the fields are bounded by
/// the SSID/password max lengths, PR-06).
constexpr std::size_t kMaxFormBodyLen = 512;

/// Priority for the short-lived restart task (just above idle; it only
/// sleeps then reboots).
constexpr UBaseType_t kRestartTaskPriority = tskIDLE_PRIORITY + 1;

// -- Setup / result pages ----------------------------------------------------
// Small, self-contained (no external assets) to keep the app within the OTA
// slot budget (research R2). English only.

const char* kSetupPage =
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>WateringSystem Setup</title></head><body>"
    "<h1>WateringSystem WiFi Setup</h1>"
    "<p>Enter the credentials of the WiFi network the device should join.</p>"
    "<form method=\"POST\" action=\"/wifi/config\">"
    "<p><label>Network name (SSID)<br>"
    "<input name=\"ssid\" type=\"text\" maxlength=\"32\" required></label></p>"
    "<p><label>Password<br>"
    "<input name=\"password\" type=\"password\" maxlength=\"64\"></label></p>"
    "<p><button type=\"submit\">Save and restart</button></p>"
    "</form></body></html>";

const char* kSuccessPage =
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Saved</title></head><body>"
    "<h1>Credentials saved</h1>"
    "<p>The device will restart shortly to join the network. You can close "
    "this page.</p>"
    "</body></html>";

/// Decode one application/x-www-form-urlencoded value ('+' -> space, %XX ->
/// byte). Malformed trailing escapes are copied literally.
std::string urlDecode(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < in.size() &&
                   std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            const std::string hex = in.substr(i + 1, 2);
            out.push_back(static_cast<char>(std::stoi(hex, nullptr, 16)));
            i += 2;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

/// Extract the urldecoded value of `key` from an urlencoded form body.
/// Returns true when the key is present (its value may be empty).
bool formField(const std::string& body, const std::string& key,
               std::string& out)
{
    const std::string needle = key + "=";
    std::size_t pos = 0;
    while (pos < body.size()) {
        // A field starts at the beginning or right after a '&'.
        if (body.compare(pos, needle.size(), needle) == 0) {
            const std::size_t valueStart = pos + needle.size();
            const std::size_t amp = body.find('&', valueStart);
            const std::size_t end =
                (amp == std::string::npos) ? body.size() : amp;
            out = urlDecode(body.substr(valueStart, end - valueStart));
            return true;
        }
        const std::size_t amp = body.find('&', pos);
        if (amp == std::string::npos) {
            break;
        }
        pos = amp + 1;
    }
    return false;
}

/// Short-lived task: wait kRestartDelayMs, then reboot.
[[noreturn]] void restartTask(void* /*arg*/)
{
    vTaskDelay(pdMS_TO_TICKS(ProvisioningPortal::kRestartDelayMs));
    ESP_LOGI(TAG, "restarting to apply new WiFi credentials");
    esp_restart();
    for (;;) {
        // esp_restart() never returns; guard against a hypothetical return.
        vTaskDelay(portMAX_DELAY);
    }
}

/// Default restart hook: schedule the deferred reboot on a short-lived task so
/// the HTTP success response is delivered first (esp_restart stays off the
/// handler's direct call path).
void scheduleDeferredRestart()
{
    if (xTaskCreate(restartTask, "prov_restart", 2048, nullptr,
                    kRestartTaskPriority, nullptr) != pdPASS) {
        // The task could not be created: fall back to an immediate restart.
        // The credentials are already persisted, so a reboot is still the
        // correct outcome even without the response-delivery delay.
        ESP_LOGW(TAG, "restart task create failed; restarting now");
        esp_restart();
    }
}

/// Send a small text/html response with an explicit status line.
esp_err_t sendHtml(httpd_req_t* req, const char* status, const char* body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, body);
}

// -- Route handlers (file-local; recover the portal via req->user_ctx) -------

esp_err_t rootGetHandler(httpd_req_t* req)
{
    // GET / (and, via wildcard matching, any unknown path) serves the setup
    // page (contracts/provisioning-portal.md).
    return sendHtml(req, "200 OK", kSetupPage);
}

esp_err_t wifiConfigPostHandler(httpd_req_t* req)
{
    auto* portal = static_cast<ProvisioningPortal*>(req->user_ctx);
    if (portal == nullptr) {
        return sendHtml(req, "500 Internal Server Error",
                        "Server misconfigured.");
    }

    if (req->content_len == 0 || req->content_len > kMaxFormBodyLen) {
        ESP_LOGW(TAG, "rejecting form: body length %d out of range",
                 static_cast<int>(req->content_len));
        return sendHtml(req, "400 Bad Request", "Invalid form submission.");
    }

    std::string body(req->content_len, '\0');
    std::size_t received = 0;
    while (received < req->content_len) {
        const int r = httpd_req_recv(req, &body[received],
                                     req->content_len - received);
        if (r <= 0) {
            ESP_LOGW(TAG, "form body receive failed (%d)", r);
            return sendHtml(req, "400 Bad Request",
                            "Could not read the form.");
        }
        received += static_cast<std::size_t>(r);
    }

    std::string ssid;
    std::string password;
    // A missing ssid key is a malformed form (not a too-short SSID): report it
    // distinctly rather than falling through to the credential length error.
    if (!formField(body, "ssid", ssid)) {
        ESP_LOGW(TAG, "rejecting form: ssid field missing");
        return sendHtml(req, "400 Bad Request",
                        "Invalid form submission: the ssid field is missing.");
    }
    formField(body, "password", password);  // absent = open network (empty)

    // Validate with the same pure rule the host tests cover. Never log the
    // values — only the typed reason code.
    const CredentialCheck check = validateWifiCredentials(ssid, password);
    if (!isValid(check)) {
        ESP_LOGW(TAG, "credential submission rejected (reason %d)",
                 static_cast<int>(check));
        return sendHtml(req, "400 Bad Request",
                        "Invalid credentials: the network name must be "
                        "1-32 characters and the password must be empty or "
                        "8-64 characters.");
    }

    if (!portal->persistCredentials(ssid, password)) {
        ESP_LOGE(TAG, "credential persist failed");
        return sendHtml(req, "500 Internal Server Error",
                        "Could not save the credentials. Please try again.");
    }

    // Deliver the success page, THEN schedule the deferred restart so the
    // response reaches the browser before the reboot (FR-007).
    const esp_err_t sendErr = sendHtml(req, "200 OK", kSuccessPage);
    portal->scheduleRestart();
    return sendErr;
}

}  // namespace

ProvisioningPortal::ProvisioningPortal(IConfigStore& configStore,
                                       RestartHook restartHook)
    : configStore_(configStore),
      restartHook_(restartHook ? std::move(restartHook)
                               : RestartHook(&scheduleDeferredRestart))
{
}

ProvisioningPortal::~ProvisioningPortal()
{
    stop();
}

bool ProvisioningPortal::persistCredentials(const std::string& ssid,
                                            const std::string& password)
{
    return configStore_.setWifiCredentials(ssid, password);
}

void ProvisioningPortal::scheduleRestart()
{
    if (restartHook_) {
        restartHook_();
    }
}

esp_err_t ProvisioningPortal::start()
{
    if (server_ != nullptr) {
        return ESP_OK;  // already started (idempotent)
    }

    httpd_handle_t server = nullptr;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Wildcard matching so GET on any unknown path falls through to the setup
    // page (contracts/provisioning-portal.md).
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t postConfig = {
        .uri = "/wifi/config",
        .method = HTTP_POST,
        .handler = wifiConfigPostHandler,
        .user_ctx = this,
    };
    err = httpd_register_uri_handler(server, &postConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register POST /wifi/config failed: %s",
                 esp_err_to_name(err));
        httpd_stop(server);
        return err;
    }

    // Registered after the specific POST route above; the GET wildcard only
    // matches GET requests, so it never shadows POST /wifi/config.
    const httpd_uri_t getRoot = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = rootGetHandler,
        .user_ctx = this,
    };
    err = httpd_register_uri_handler(server, &getRoot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register GET /* failed: %s", esp_err_to_name(err));
        httpd_stop(server);
        return err;
    }

    server_ = server;
    ESP_LOGI(TAG, "provisioning portal started");
    return ESP_OK;
}

void ProvisioningPortal::stop()
{
    if (server_ != nullptr) {
        httpd_stop(static_cast<httpd_handle_t>(server_));
        server_ = nullptr;
        ESP_LOGI(TAG, "provisioning portal stopped");
    }
}
