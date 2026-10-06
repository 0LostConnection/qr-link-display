#include "qr/store.hpp"

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <cctype>
#include <cstring>

namespace qr {
namespace {

constexpr char TAG[] = "qr.store";
constexpr char kNvsNs[] = "qr";
constexpr char kNvsUrl[] = "url";

esp_err_t nvs_init_once() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    return err;
}

void sanitize_url(char *s, size_t max_chars) {
    if (!s) {
        return;
    }
    char *start = s;
    while (*start && std::isspace(static_cast<unsigned char>(*start))) {
        ++start;
    }
    if (start != s) {
        std::memmove(s, start, std::strlen(start) + 1);
    }
    size_t len = std::strlen(s);
    while (len > 0 && std::isspace(static_cast<unsigned char>(s[len - 1]))) {
        s[--len] = '\0';
    }
    for (size_t i = 0; s[i];) {
        if (s[i] == '\r' || s[i] == '\n') {
            std::memmove(s + i, s + i + 1, std::strlen(s + i));
            continue;
        }
        ++i;
    }
    len = std::strlen(s);
    if (len > max_chars) {
        s[max_chars] = '\0';
    }
}

}  // namespace

esp_err_t load_url(char *out, size_t out_len) {
    ESP_RETURN_ON_FALSE(out && out_len >= 2, ESP_ERR_INVALID_ARG, TAG, "buf");
    out[0] = '\0';
    ESP_RETURN_ON_ERROR(nvs_init_once(), TAG, "nvs");
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNvsNs, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND; /* namespace not created yet */
    }
    ESP_RETURN_ON_ERROR(err, TAG, "open");
    size_t len = out_len;
    err = nvs_get_str(h, kNvsUrl, out, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err == ESP_OK) {
        sanitize_url(out, out_len - 1);
        if (out[0] == '\0') {
            return ESP_ERR_NOT_FOUND;
        }
    }
    return err;
}

esp_err_t save_url(const char *url) {
    ESP_RETURN_ON_FALSE(url, ESP_ERR_INVALID_ARG, TAG, "null");
    char buf[kMaxUrlLen]{};
    std::strncpy(buf, url, sizeof(buf) - 1);
    sanitize_url(buf, sizeof(buf) - 1);

    ESP_RETURN_ON_ERROR(nvs_init_once(), TAG, "nvs");
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(kNvsNs, NVS_READWRITE, &h), TAG, "open");
    esp_err_t err;
    if (buf[0] == '\0') {
        err = nvs_erase_key(h, kNvsUrl);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    } else {
        err = nvs_set_str(h, kNvsUrl, buf);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t clear_url() {
    return save_url("");
}

}  // namespace qr
