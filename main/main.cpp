#include "board/display.hpp"
#include "board/touch.hpp"
#include "qr/store.hpp"
#include "ui/qr_ui.hpp"
#include "ui/status_ui.hpp"
#include "wifi/provision.hpp"

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr char TAG[] = "app";
constexpr uint32_t kNetTaskStack = 8192;
constexpr UBaseType_t kNetTaskPrio = 5;

/* Pending URL from SoftAP portal (httpd task → UI under display lock). */
SemaphoreHandle_t s_url_mu = nullptr;
char s_pending_url[qr::kMaxUrlLen]{};
volatile bool s_url_dirty = false;

void log_boot_info() {
    esp_chip_info_t chip{};
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "ESP32-S3 cores=%d rev=%d", chip.cores, chip.revision);
    ESP_LOGI(TAG, "free heap=%u free psram=%u",
             static_cast<unsigned>(esp_get_free_heap_size()),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void status(const char *title, const char *detail) {
    board::display::lock(0);
    ui::create_status(title, detail);
    board::display::unlock();
}

void on_url_changed(const char *url) {
    if (!s_url_mu) {
        return;
    }
    xSemaphoreTake(s_url_mu, portMAX_DELAY);
    if (url) {
        std::strncpy(s_pending_url, url, sizeof(s_pending_url) - 1);
        s_pending_url[sizeof(s_pending_url) - 1] = '\0';
    } else {
        s_pending_url[0] = '\0';
    }
    s_url_dirty = true;
    xSemaphoreGive(s_url_mu);
}

void apply_pending_url() {
    if (!s_url_dirty || !s_url_mu) {
        return;
    }
    char local[qr::kMaxUrlLen]{};
    xSemaphoreTake(s_url_mu, portMAX_DELAY);
    std::memcpy(local, s_pending_url, sizeof(local));
    s_url_dirty = false;
    xSemaphoreGive(s_url_mu);

    board::display::lock(0);
    ui::hide_settings();
    ui::set_qr_link(local);
    board::display::unlock();
    ESP_LOGI(TAG, "QR updated len=%u", static_cast<unsigned>(std::strlen(local)));
}

void net_task(void *) {
    s_url_mu = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_url_mu ? ESP_OK : ESP_ERR_NO_MEM);

    char boot_detail[160]{};
    std::snprintf(boot_detail, sizeof(boot_detail),
                  "Starting hotspot…\n"
                  "Join WiFi: %s\n"
                  "Password: %s\n"
                  "Open http://%s",
                  wifi::kApSsid, wifi::kApPassword, wifi::kApIp);
    status("QR Link", boot_detail);

    ESP_ERROR_CHECK(wifi::start_ap(on_url_changed));

    char saved[qr::kMaxUrlLen]{};
    const bool have_url = qr::load_url(saved, sizeof(saved)) == ESP_OK && saved[0] != '\0';

    board::display::lock(0);
    ui::create_qr_screen();
    if (have_url) {
        ui::set_qr_link(saved);
    } else {
        ui::set_qr_link("");
    }
    board::display::unlock();

    ESP_LOGI(TAG, "ready — SoftAP %s", wifi::kApSsid);

    while (true) {
        apply_pending_url();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

}  // namespace

extern "C" void app_main() {
    log_boot_info();

    board::display::Handles display{};
    ESP_ERROR_CHECK(board::display::start(&display));

    board::touch::Handles touch{};
    ESP_ERROR_CHECK(board::touch::start(display.lv_disp, &touch));

    status("QR Link", "Booting…");

    BaseType_t ok = xTaskCreatePinnedToCore(net_task, "net", kNetTaskStack, nullptr, kNetTaskPrio,
                                            nullptr, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create net task");
        status("Error", "Could not start network task");
    }
}
