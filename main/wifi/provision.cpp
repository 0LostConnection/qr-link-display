#include "wifi/provision.hpp"

#include "qr/store.hpp"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace wifi {
namespace {

constexpr char TAG[] = "wifi.ap";
constexpr uint16_t kDnsPort = 53;

httpd_handle_t s_httpd = nullptr;
TaskHandle_t s_dns_task = nullptr;
volatile bool s_dns_run = false;
UrlChangedCb s_on_url_changed = nullptr;

constexpr char kIndexHtml[] =
    "<!DOCTYPE html><html><head><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>QR Link Setup</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;background:#0e1216;color:#e8eef2;margin:0;padding:24px}"
    "h1{font-size:1.4rem;margin:0 0 8px}"
    "p{color:#8aa0b0;line-height:1.4}"
    "label{display:block;margin-top:16px;font-size:.9rem;color:#8aa0b0}"
    "input,textarea,button{font-size:16px;width:100%;box-sizing:border-box;margin:8px 0;"
    "padding:12px;border-radius:10px;border:0}"
    "textarea{min-height:96px;resize:vertical}"
    "button{background:#3d9eff;color:#041018;font-weight:700}"
    ".row{display:flex;gap:8px}.row button{flex:1}"
    ".secondary{background:#2a3540;color:#e8eef2}"
    "</style></head><body>"
    "<h1>QR Link</h1>"
    "<p>Set the URL or text shown as a QR code on the display.</p>"
    "<form method=POST action=/save>"
    "<label for=url>Link</label>"
    "<textarea id=url name=url maxlength=511 placeholder=\"https://example.com\" required></textarea>"
    "<button type=submit>Save &amp; Show QR</button>"
    "</form>"
    "<form method=POST action=/clear style=\"margin-top:12px\">"
    "<button class=secondary type=submit>Clear QR</button>"
    "</form>"
    "</body></html>";

constexpr char kSavedHtml[] =
    "<!DOCTYPE html><html><body style=\"font-family:system-ui,sans-serif;background:#0e1216;"
    "color:#e8eef2;padding:24px\"><h1>Saved</h1>"
    "<p>The display QR code was updated. You can disconnect from the hotspot.</p>"
    "<p><a href=/ style=\"color:#3d9eff\">Edit again</a></p></body></html>";

constexpr char kClearedHtml[] =
    "<!DOCTYPE html><html><body style=\"font-family:system-ui,sans-serif;background:#0e1216;"
    "color:#e8eef2;padding:24px\"><h1>Cleared</h1>"
    "<p>QR link removed from the display.</p>"
    "<p><a href=/ style=\"color:#3d9eff\">Set a new link</a></p></body></html>";

esp_err_t nvs_init_once() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    return err;
}

void url_decode_inplace(char *s) {
    char *r = s;
    char *w = s;
    while (*r) {
        if (*r == '+') {
            *w++ = ' ';
            ++r;
        } else if (*r == '%' && r[1] && r[2]) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f') {
                    return c - 'a' + 10;
                }
                if (c >= 'A' && c <= 'F') {
                    return c - 'A' + 10;
                }
                return -1;
            };
            const int hi = hex(r[1]);
            const int lo = hex(r[2]);
            if (hi >= 0 && lo >= 0) {
                *w++ = static_cast<char>((hi << 4) | lo);
                r += 3;
            } else {
                *w++ = *r++;
            }
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

bool form_get(const char *body, const char *key, char *out, size_t out_len) {
    const size_t key_len = std::strlen(key);
    const char *p = body;
    while (p && *p) {
        if (std::strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            p += key_len + 1;
            size_t i = 0;
            while (*p && *p != '&' && i + 1 < out_len) {
                out[i++] = *p++;
            }
            out[i] = '\0';
            url_decode_inplace(out);
            return true;
        }
        p = std::strchr(p, '&');
        if (p) {
            ++p;
        }
    }
    return false;
}

void notify_url(const char *url) {
    if (s_on_url_changed) {
        s_on_url_changed(url ? url : "");
    }
}

esp_err_t http_root(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t http_save(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len > 700) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }
    char body[704]{};
    int got = httpd_req_recv(req, body, req->content_len);
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
        return ESP_FAIL;
    }
    body[got] = '\0';

    char url[qr::kMaxUrlLen]{};
    if (!form_get(body, "url", url, sizeof(url)) || url[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "url required");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "portal saved url len=%u", static_cast<unsigned>(std::strlen(url)));
    ESP_RETURN_ON_ERROR(qr::save_url(url), TAG, "save url");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, kSavedHtml, HTTPD_RESP_USE_STRLEN);
    notify_url(url);
    return ESP_OK;
}

esp_err_t http_clear(httpd_req_t *req) {
    ESP_RETURN_ON_ERROR(qr::clear_url(), TAG, "clear url");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, kClearedHtml, HTTPD_RESP_USE_STRLEN);
    notify_url("");
    return ESP_OK;
}

esp_err_t http_captive(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

void dns_server_task(void *) {
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "dns socket");
        vTaskDelete(nullptr);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kDnsPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "dns bind");
        close(sock);
        vTaskDelete(nullptr);
        return;
    }

    uint8_t buf[512];
    while (s_dns_run) {
        sockaddr_in src{};
        socklen_t src_len = sizeof(src);
        const int len = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr *>(&src), &src_len);
        if (len < 12) {
            continue;
        }
        buf[2] = 0x81;
        buf[3] = 0x80;
        buf[6] = 0;
        buf[7] = 1;
        buf[8] = 0;
        buf[9] = 0;
        buf[10] = 0;
        buf[11] = 0;

        int resp_len = len;
        if (resp_len + 16 > static_cast<int>(sizeof(buf))) {
            continue;
        }
        buf[resp_len++] = 0xc0;
        buf[resp_len++] = 0x0c;
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x01;
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x01;
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x1e;
        buf[resp_len++] = 0x00;
        buf[resp_len++] = 0x04;
        buf[resp_len++] = 192;
        buf[resp_len++] = 168;
        buf[resp_len++] = 4;
        buf[resp_len++] = 1;
        sendto(sock, buf, resp_len, 0, reinterpret_cast<sockaddr *>(&src), src_len);
    }
    close(sock);
    vTaskDelete(nullptr);
}

esp_err_t start_httpd() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 10;
    cfg.stack_size = 8192;
    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &cfg), TAG, "httpd");

    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = http_root, .user_ctx = nullptr};
    const httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = http_save, .user_ctx = nullptr};
    const httpd_uri_t clear = {.uri = "/clear", .method = HTTP_POST, .handler = http_clear, .user_ctx = nullptr};
    const httpd_uri_t generater = {
        .uri = "/generate_204", .method = HTTP_GET, .handler = http_captive, .user_ctx = nullptr};
    const httpd_uri_t hotspot = {
        .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = http_captive, .user_ctx = nullptr};
    const httpd_uri_t ncsi = {
        .uri = "/ncsi.txt", .method = HTTP_GET, .handler = http_captive, .user_ctx = nullptr};

    httpd_register_uri_handler(s_httpd, &root);
    httpd_register_uri_handler(s_httpd, &save);
    httpd_register_uri_handler(s_httpd, &clear);
    httpd_register_uri_handler(s_httpd, &generater);
    httpd_register_uri_handler(s_httpd, &hotspot);
    httpd_register_uri_handler(s_httpd, &ncsi);
    return ESP_OK;
}

}  // namespace

esp_err_t get_ap_ip_str(char *out, size_t out_len) {
    ESP_RETURN_ON_FALSE(out && out_len >= 16, ESP_ERR_INVALID_ARG, TAG, "buf");
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!netif) {
        std::snprintf(out, out_len, "%s", kApIp);
        return ESP_OK;
    }
    esp_netif_ip_info_t ip{};
    ESP_RETURN_ON_ERROR(esp_netif_get_ip_info(netif, &ip), TAG, "ip info");
    std::snprintf(out, out_len, IPSTR, IP2STR(&ip.ip));
    return ESP_OK;
}

esp_err_t start_ap(UrlChangedCb on_url_changed) {
    s_on_url_changed = on_url_changed;
    ESP_RETURN_ON_ERROR(nvs_init_once(), TAG, "nvs");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");

    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");

    wifi_config_t wcfg{};
    std::strncpy(reinterpret_cast<char *>(wcfg.ap.ssid), kApSsid, sizeof(wcfg.ap.ssid) - 1);
    wcfg.ap.ssid_len = static_cast<uint8_t>(std::strlen(kApSsid));
    std::strncpy(reinterpret_cast<char *>(wcfg.ap.password), kApPassword, sizeof(wcfg.ap.password) - 1);
    wcfg.ap.channel = 1;
    wcfg.ap.max_connection = 4;
    wcfg.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wcfg), TAG, "cfg");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    ESP_RETURN_ON_ERROR(start_httpd(), TAG, "httpd");

    s_dns_run = true;
    xTaskCreate(dns_server_task, "dns_cap", 4096, nullptr, 5, &s_dns_task);

    ESP_LOGI(TAG, "SoftAP up SSID=%s (WPA2) http://%s/", kApSsid, kApIp);
    return ESP_OK;
}

}  // namespace wifi
