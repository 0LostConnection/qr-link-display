#pragma once

#include "esp_err.h"

#include <cstddef>

namespace wifi {

constexpr char kApSsid[] = "QR-Link-Setup";
/** WPA2 SoftAP password (min 8 chars). Shown on the AP QR screen. */
constexpr char kApPassword[] = "qrlink123";
constexpr char kApIp[] = "192.168.4.1";

using UrlChangedCb = void (*)(const char *url);

/**
 * Start SoftAP + captive portal for configuring the displayed QR link.
 * Non-blocking: returns once the AP and HTTP server are up.
 * Optional callback runs (from the httpd task) after a successful save.
 */
esp_err_t start_ap(UrlChangedCb on_url_changed = nullptr);

/** SoftAP IPv4 as dotted quad (usually 192.168.4.1). */
esp_err_t get_ap_ip_str(char *out, size_t out_len);

}  // namespace wifi
