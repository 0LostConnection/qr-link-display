#include "ui/qr_ui.hpp"

#include "board/display.hpp"
#include "qr/store.hpp"
#include "ui/fonts.hpp"
#include "wifi/provision.hpp"

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lvgl.h"

#include <cstdio>
#include <cstring>

namespace ui {
namespace {

lv_obj_t *s_scr = nullptr;
lv_obj_t *s_qr = nullptr;
lv_obj_t *s_hint = nullptr;
lv_obj_t *s_caption = nullptr;
lv_obj_t *s_settings = nullptr;

char s_url[qr::kMaxUrlLen]{};

constexpr uint32_t kBg = 0x0E1216;
constexpr uint32_t kPanel = 0x1A2228;
constexpr uint32_t kMuted = 0x2A3540;
constexpr uint32_t kAccent = 0x3D9EFF;
constexpr uint32_t kTextLight = 0xE8EEF2;
constexpr uint32_t kTextMuted = 0x8AA0B0;
constexpr uint32_t kQrDark = 0x0E1216;
constexpr uint32_t kQrLight = 0xFFFFFF;
constexpr int kQrSizeLink = 260;
constexpr int kQrSizeAp = 180;
constexpr int kBrightnessMin = 5;
constexpr int64_t kDoubleTapUs = 400000; /* 400 ms */

int64_t s_last_tap_us = 0;

void style_btn(lv_obj_t *btn, uint32_t bg) {
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 4, 0);
}

void destroy_settings() {
    if (s_settings) {
        lv_obj_delete(s_settings);
        s_settings = nullptr;
    }
}

void wifi_join_payload(char *out, size_t out_len) {
    /* Standard phone WiFi QR — WPA2 SoftAP. */
    std::snprintf(out, out_len, "WIFI:T:WPA;S:%s;P:%s;;", wifi::kApSsid, wifi::kApPassword);
}

void refresh_qr_widget() {
    if (!s_qr || !s_hint || !s_caption) {
        return;
    }

    lv_obj_clear_flag(s_qr, LV_OBJ_FLAG_HIDDEN);
    lv_qrcode_set_dark_color(s_qr, lv_color_hex(kQrDark));
    lv_qrcode_set_light_color(s_qr, lv_color_hex(kQrLight));

    if (s_url[0] == '\0') {
        char wifi_qr[96]{};
        wifi_join_payload(wifi_qr, sizeof(wifi_qr));
        lv_qrcode_set_size(s_qr, kQrSizeAp);
        lv_qrcode_update(s_qr, wifi_qr, std::strlen(wifi_qr));

        lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_caption, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_hint, "Scan to join ESP32 WiFi");
        char cap[96]{};
        std::snprintf(cap, sizeof(cap), "Password: %s\nThen open http://192.168.4.1",
                      wifi::kApPassword);
        lv_label_set_text(s_caption, cap);
        return;
    }

    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_caption, LV_OBJ_FLAG_HIDDEN);
    lv_qrcode_set_size(s_qr, kQrSizeLink);
    lv_qrcode_update(s_qr, s_url, std::strlen(s_url));
}

void on_brightness_changed(lv_event_t *e) {
    auto *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
    int pct = static_cast<int>(lv_slider_get_value(slider));
    if (pct < kBrightnessMin) {
        pct = kBrightnessMin;
        lv_slider_set_value(slider, pct, LV_ANIM_OFF);
    }
    board::display::set_brightness(pct);
    if (auto *lbl = static_cast<lv_obj_t *>(lv_event_get_user_data(e))) {
        char buf[16]{};
        std::snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(lbl, buf);
    }
}

void fill_system_info(char *out, size_t out_len) {
    char ip[16] = "-";
    (void)wifi::get_ap_ip_str(ip, sizeof(ip));

    uint8_t mac[6]{};
    char mac_str[18] = "-";
    if (esp_wifi_get_mac(WIFI_IF_AP, mac) == ESP_OK) {
        std::snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
                      mac[2], mac[3], mac[4], mac[5]);
    }

    const esp_app_desc_t *app = esp_app_get_description();
    const char *ver = (app && app->version[0]) ? app->version : "-";
    const size_t heap_free = esp_get_free_heap_size();
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    std::snprintf(out, out_len,
                  "AP    %s\n"
                  "Pass  %s\n"
                  "Open  http://%s/\n"
                  "MAC   %s\n"
                  "Heap  %u KB free\n"
                  "PSRAM %u KB free\n"
                  "App   %s",
                  wifi::kApSsid, wifi::kApPassword, ip, mac_str,
                  static_cast<unsigned>(heap_free / 1024U),
                  static_cast<unsigned>(psram_free / 1024U), ver);
}

void on_clear_clicked(lv_event_t *e) {
    (void)e;
    qr::clear_url();
    set_qr_link("");
    destroy_settings();
}

void on_screen_clicked(lv_event_t *e) {
    (void)e;
    if (s_settings) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (s_last_tap_us > 0 && (now - s_last_tap_us) <= kDoubleTapUs) {
        s_last_tap_us = 0;
        show_settings();
        return;
    }
    s_last_tap_us = now;
}

}  // namespace

void create_qr_screen() {
    init_fonts();
    destroy_settings();

    s_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_scr, on_screen_clicked, LV_EVENT_CLICKED, nullptr);

    s_hint = lv_label_create(s_scr);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hint, 440);
    lv_label_set_text(s_hint, "Scan to join ESP32 WiFi");
    lv_obj_set_style_text_color(s_hint, lv_color_hex(kTextLight), 0);
    lv_obj_set_style_text_font(s_hint, font_20(), 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_EVENT_BUBBLE);

    s_qr = lv_qrcode_create(s_scr);
    lv_qrcode_set_size(s_qr, kQrSizeAp);
    lv_qrcode_set_dark_color(s_qr, lv_color_hex(kQrDark));
    lv_qrcode_set_light_color(s_qr, lv_color_hex(kQrLight));
    lv_obj_set_style_border_color(s_qr, lv_color_hex(kQrLight), 0);
    lv_obj_set_style_border_width(s_qr, 10, 0);
    lv_obj_set_style_radius(s_qr, 8, 0);
    lv_obj_align(s_qr, LV_ALIGN_CENTER, 0, 4);
    lv_obj_add_flag(s_qr, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(s_qr, LV_OBJ_FLAG_CLICKABLE);

    s_caption = lv_label_create(s_scr);
    lv_label_set_long_mode(s_caption, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_caption, 440);
    lv_label_set_text(s_caption, "Then open http://192.168.4.1 to set a link");
    lv_obj_set_style_text_color(s_caption, lv_color_hex(kTextMuted), 0);
    lv_obj_set_style_text_font(s_caption, font_14(), 0);
    lv_obj_set_style_text_align(s_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_caption, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_flag(s_caption, LV_OBJ_FLAG_EVENT_BUBBLE);

    refresh_qr_widget();
    lv_screen_load(s_scr);
}

void set_qr_link(const char *url) {
    if (url) {
        std::strncpy(s_url, url, sizeof(s_url) - 1);
        s_url[sizeof(s_url) - 1] = '\0';
    } else {
        s_url[0] = '\0';
    }
    refresh_qr_widget();
}

void get_qr_link(char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    std::snprintf(out, out_len, "%s", s_url);
}

bool settings_open() {
    return s_settings != nullptr;
}

void hide_settings() {
    destroy_settings();
}

void show_settings() {
    destroy_settings();

    s_settings = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_settings, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_settings, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_settings, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_settings, 0, 0);
    lv_obj_set_style_pad_all(s_settings, 0, 0);
    lv_obj_set_style_radius(s_settings, 0, 0);
    lv_obj_clear_flag(s_settings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(
        s_settings, [](lv_event_t *e) { (void)e; destroy_settings(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *panel = lv_obj_create(s_settings);
    lv_obj_set_size(panel, 400, 290);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(kPanel), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(kMuted), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, 8, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_add_event_cb(
        panel, [](lv_event_t *e) { lv_event_stop_bubbling(e); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *hdr = lv_obj_create(panel);
    lv_obj_set_size(hdr, 376, 32);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "Info / Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(kTextLight), 0);
    lv_obj_set_style_text_font(title, font_20(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *close_btn = lv_button_create(hdr);
    lv_obj_set_size(close_btn, 64, 28);
    style_btn(close_btn, kMuted);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(
        close_btn, [](lv_event_t *e) { (void)e; destroy_settings(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "CLOSE");
    lv_obj_set_style_text_color(close_lbl, lv_color_hex(kTextLight), 0);
    lv_obj_set_style_text_font(close_lbl, font_14(), 0);
    lv_obj_center(close_lbl);

    /* Brightness */
    lv_obj_t *br_hdr = lv_obj_create(panel);
    lv_obj_set_size(br_hdr, 376, 22);
    lv_obj_set_style_bg_opa(br_hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(br_hdr, 0, 0);
    lv_obj_set_style_pad_all(br_hdr, 0, 0);
    lv_obj_clear_flag(br_hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *br_title = lv_label_create(br_hdr);
    lv_label_set_text(br_title, "Brightness");
    lv_obj_set_style_text_color(br_title, lv_color_hex(kTextMuted), 0);
    lv_obj_set_style_text_font(br_title, font_14(), 0);
    lv_obj_align(br_title, LV_ALIGN_LEFT_MID, 0, 0);

    int brightness = board::display::get_brightness();
    if (brightness < kBrightnessMin) {
        brightness = kBrightnessMin;
    }
    lv_obj_t *br_pct = lv_label_create(br_hdr);
    char pct_buf[16]{};
    std::snprintf(pct_buf, sizeof(pct_buf), "%d%%", brightness);
    lv_label_set_text(br_pct, pct_buf);
    lv_obj_set_style_text_color(br_pct, lv_color_hex(kTextLight), 0);
    lv_obj_set_style_text_font(br_pct, font_14(), 0);
    lv_obj_align(br_pct, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *slider = lv_slider_create(panel);
    lv_obj_set_width(slider, 376);
    lv_obj_set_height(slider, 18);
    lv_slider_set_range(slider, kBrightnessMin, 100);
    lv_slider_set_value(slider, brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(kMuted), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(kAccent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(kTextLight), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, on_brightness_changed, LV_EVENT_VALUE_CHANGED, br_pct);

    /* Current link */
    lv_obj_t *link_title = lv_label_create(panel);
    lv_label_set_text(link_title, "Current link");
    lv_obj_set_style_text_color(link_title, lv_color_hex(kTextMuted), 0);
    lv_obj_set_style_text_font(link_title, font_14(), 0);
    lv_obj_set_width(link_title, 376);

    lv_obj_t *link_lbl = lv_label_create(panel);
    lv_label_set_long_mode(link_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(link_lbl, 376);
    lv_label_set_text(link_lbl, s_url[0] ? s_url : "(none — use hotspot portal)");
    lv_obj_set_style_text_color(link_lbl, lv_color_hex(kTextLight), 0);
    lv_obj_set_style_text_font(link_lbl, font_14(), 0);

    /* System info */
    lv_obj_t *info_title = lv_label_create(panel);
    lv_label_set_text(info_title, "System");
    lv_obj_set_style_text_color(info_title, lv_color_hex(kTextMuted), 0);
    lv_obj_set_style_text_font(info_title, font_14(), 0);
    lv_obj_set_width(info_title, 376);

    char info[320]{};
    fill_system_info(info, sizeof(info));
    lv_obj_t *info_lbl = lv_label_create(panel);
    lv_label_set_long_mode(info_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info_lbl, 376);
    lv_label_set_text(info_lbl, info);
    lv_obj_set_style_text_color(info_lbl, lv_color_hex(kTextLight), 0);
    lv_obj_set_style_text_font(info_lbl, font_14(), 0);

    lv_obj_t *clear_btn = lv_button_create(panel);
    lv_obj_set_size(clear_btn, 376, 36);
    style_btn(clear_btn, 0x8B1E2D);
    lv_obj_add_event_cb(clear_btn, on_clear_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *clear_lbl = lv_label_create(clear_btn);
    lv_label_set_text(clear_lbl, "Clear QR link");
    lv_obj_set_style_text_color(clear_lbl, lv_color_hex(kTextLight), 0);
    lv_obj_set_style_text_font(clear_lbl, font_14(), 0);
    lv_obj_center(clear_lbl);
}

}  // namespace ui
