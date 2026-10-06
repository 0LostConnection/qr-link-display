#include "board/display.hpp"

#include "board/panel_hw.h"
#include "board/pins.hpp"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace board::display {
namespace {

constexpr char TAG[] = "board.display";
constexpr ledc_channel_t kBlChannel = LEDC_CHANNEL_1;
constexpr ledc_timer_t kBlTimer = LEDC_TIMER_1;
constexpr uint32_t kTeTvdlMs = 13;
constexpr uint32_t kTeTimeoutMs = 50;
constexpr int kTeTaskPrio = 4;
constexpr uint32_t kTeTaskStack = 2048;
/* Strip budget ~1/10 frame; landscape flush sends vertical logical strips. */
constexpr uint32_t kBouncePx = (static_cast<uint32_t>(pins::kLcdHRes) * pins::kLcdVRes) / 10;

struct TearContext {
    SemaphoreHandle_t te_v_sync_sem = nullptr;
    SemaphoreHandle_t te_catch_sem = nullptr;
    uint32_t time_tvdl_ms = kTeTvdlMs;
};

struct BounceContext {
    esp_lcd_panel_handle_t panel = nullptr;
    uint16_t *bufs[2] = {};
    uint32_t strip_px = 0;
    SemaphoreHandle_t trans_done = nullptr;
    int active = 0;
};

TearContext *s_tear = nullptr;
BounceContext *s_bounce = nullptr;

void te_sync_task(void *arg) {
    auto *tear = static_cast<TearContext *>(arg);
    while (true) {
        if (xSemaphoreTake(tear->te_catch_sem, pdMS_TO_TICKS(tear->time_tvdl_ms)) != pdPASS) {
            xSemaphoreTake(tear->te_v_sync_sem, 0);
        }
    }
}

void IRAM_ATTR te_isr(void *arg) {
    auto *tear = static_cast<TearContext *>(arg);
    BaseType_t hp_task_woken = pdFALSE;
    if (tear->te_v_sync_sem) {
        xSemaphoreGiveFromISR(tear->te_v_sync_sem, &hp_task_woken);
        if (hp_task_woken) {
            portYIELD_FROM_ISR();
        }
    }
}

void wait_for_te(TearContext *tear) {
    if (!tear || !tear->te_catch_sem || !tear->te_v_sync_sem) {
        return;
    }
    xSemaphoreGive(tear->te_catch_sem);
    if (xSemaphoreTake(tear->te_v_sync_sem, pdMS_TO_TICKS(kTeTimeoutMs)) != pdTRUE) {
        ESP_LOGD(TAG, "TE timeout — flushing without sync");
    }
}

bool IRAM_ATTR bounce_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *,
                                 void *user_ctx) {
    auto *bounce = static_cast<BounceContext *>(user_ctx);
    BaseType_t hp_task_woken = pdFALSE;
    if (bounce && bounce->trans_done) {
        xSemaphoreGiveFromISR(bounce->trans_done, &hp_task_woken);
    }
    return hp_task_woken == pdTRUE;
}

/**
 * Landscape (logical 480×320) → native panel (320×480) via 90° CW, then BE swap.
 * Same mapping as the vendor demo LV_DISP_ROT_90 bounce path (required: QSPI skips RASET).
 */
void bounce_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    BounceContext *bounce = s_bounce;
    if (!bounce || !bounce->panel || !bounce->bufs[0] || !bounce->trans_done) {
        lv_display_flush_ready(disp);
        return;
    }

    const int x1 = area->x1;
    const int x2 = area->x2;
    const int y1 = area->y1;
    const int y2 = area->y2;
    const int width = x2 - x1 + 1;
    const int height = y2 - y1 + 1;
    const uint16_t *src = reinterpret_cast<const uint16_t *>(px_map);

    int max_width = static_cast<int>(bounce->strip_px / static_cast<uint32_t>(height));
    if (max_width < 1) {
        max_width = 1;
    }

    if (y1 == 0) {
        wait_for_te(s_tear);
    }

    int x_start_tmp = x1;
    bool first = true;
    while (x_start_tmp <= x2) {
        const int trans_width = (x2 - x_start_tmp + 1) < max_width ? (x2 - x_start_tmp + 1) : max_width;
        const int x_end_tmp = x_start_tmp + trans_width - 1;

        bounce->active ^= 1;
        uint16_t *dst = bounce->bufs[bounce->active];

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < trans_width; ++x) {
                const uint16_t v = src[static_cast<size_t>(y) * static_cast<size_t>(width) +
                                       static_cast<size_t>(x_start_tmp - x1 + x)];
                dst[static_cast<size_t>(x) * static_cast<size_t>(height) +
                    static_cast<size_t>(height - y - 1)] =
                    static_cast<uint16_t>((v >> 8) | (v << 8));
            }
        }

        const int px_draw_start = pins::kLcdVRes - y2 - 1;
        const int px_draw_end = pins::kLcdVRes - y1 - 1;
        const int py_draw_start = x_start_tmp;
        const int py_draw_end = x_end_tmp;

        if (first) {
            xSemaphoreGive(bounce->trans_done);
            first = false;
        }
        xSemaphoreTake(bounce->trans_done, portMAX_DELAY);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(bounce->panel, px_draw_start, py_draw_start,
                                                  px_draw_end + 1, py_draw_end + 1, dst));
        x_start_tmp += max_width;
    }

    xSemaphoreTake(bounce->trans_done, portMAX_DELAY);
    xSemaphoreGive(bounce->trans_done);
    lv_display_flush_ready(disp);
}

esp_err_t brightness_init() {
    ledc_timer_config_t timer_cfg{};
    timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_cfg.duty_resolution = LEDC_TIMER_10_BIT;
    timer_cfg.timer_num = kBlTimer;
    timer_cfg.freq_hz = 5000;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "ledc timer");

    ledc_channel_config_t ch_cfg{};
    ch_cfg.gpio_num = pins::kBacklight;
    ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_cfg.channel = kBlChannel;
    ch_cfg.intr_type = LEDC_INTR_DISABLE;
    ch_cfg.timer_sel = kBlTimer;
    ch_cfg.duty = 0;
    ch_cfg.hpoint = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch_cfg), TAG, "ledc channel");
    return ESP_OK;
}

esp_err_t tear_init(TearContext **out) {
    auto *tear = static_cast<TearContext *>(calloc(1, sizeof(TearContext)));
    ESP_RETURN_ON_FALSE(tear, ESP_ERR_NO_MEM, TAG, "tear ctx");

    tear->te_v_sync_sem = xSemaphoreCreateCounting(1, 0);
    tear->te_catch_sem = xSemaphoreCreateCounting(1, 0);
    tear->time_tvdl_ms = kTeTvdlMs;

    if (!tear->te_v_sync_sem || !tear->te_catch_sem) {
        if (tear->te_v_sync_sem) {
            vSemaphoreDelete(tear->te_v_sync_sem);
        }
        if (tear->te_catch_sem) {
            vSemaphoreDelete(tear->te_catch_sem);
        }
        free(tear);
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t te_cfg{};
    te_cfg.pin_bit_mask = 1ULL << pins::kQspiTe;
    te_cfg.mode = GPIO_MODE_INPUT;
    te_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    te_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    te_cfg.intr_type = GPIO_INTR_NEGEDGE;
    ESP_RETURN_ON_ERROR(gpio_config(&te_cfg), TAG, "te gpio");

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(pins::kQspiTe, te_isr, tear), TAG, "te isr");

    BaseType_t ok = xTaskCreate(te_sync_task, "lcd_te", kTeTaskStack, tear, kTeTaskPrio, nullptr);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_FAIL, TAG, "te task");

    *out = tear;
    return ESP_OK;
}

esp_err_t bounce_init(esp_lcd_panel_handle_t panel, BounceContext **out) {
    auto *bounce = static_cast<BounceContext *>(calloc(1, sizeof(BounceContext)));
    ESP_RETURN_ON_FALSE(bounce, ESP_ERR_NO_MEM, TAG, "bounce ctx");

    bounce->panel = panel;
    bounce->strip_px = kBouncePx;
    const size_t strip_bytes = bounce->strip_px * sizeof(uint16_t);

    bounce->bufs[0] = static_cast<uint16_t *>(
        heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    bounce->bufs[1] = static_cast<uint16_t *>(
        heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    bounce->trans_done = xSemaphoreCreateCounting(1, 0);

    if (!bounce->bufs[0] || !bounce->bufs[1] || !bounce->trans_done) {
        if (bounce->bufs[0]) {
            free(bounce->bufs[0]);
        }
        if (bounce->bufs[1]) {
            free(bounce->bufs[1]);
        }
        if (bounce->trans_done) {
            vSemaphoreDelete(bounce->trans_done);
        }
        free(bounce);
        return ESP_ERR_NO_MEM;
    }

    *out = bounce;
    return ESP_OK;
}

}  // namespace

static int s_brightness_pct = kDefaultBrightness;

esp_err_t set_brightness(int percent) {
    if (percent > 100) {
        percent = 100;
    }
    if (percent < 0) {
        percent = 0;
    }
    const uint32_t duty = (1023U * static_cast<uint32_t>(percent)) / 100U;
    ESP_LOGI(TAG, "backlight %d%%", percent);
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, kBlChannel, duty), TAG, "duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, kBlChannel), TAG, "update");
    s_brightness_pct = percent;
    return ESP_OK;
}

int get_brightness() {
    return s_brightness_pct;
}

esp_err_t backlight_on() {
    return set_brightness(kDefaultBrightness);
}

esp_err_t backlight_off() {
    return set_brightness(0);
}

bool lock(uint32_t timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void unlock() {
    lvgl_port_unlock();
}

esp_err_t start(Handles *out) {
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null out");

    ESP_RETURN_ON_ERROR(brightness_init(), TAG, "brightness");
    ESP_RETURN_ON_ERROR(tear_init(&s_tear), TAG, "tear");

    Handles handles{};
    ESP_RETURN_ON_ERROR(board_panel_create(&handles.io, &handles.panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(bounce_init(handles.panel, &s_bounce), TAG, "bounce");

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 8192;
    lvgl_cfg.task_affinity = 1;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl_port_init");

    const uint32_t frame_px = static_cast<uint32_t>(pins::kLcdHRes) * pins::kLcdVRes;
    lvgl_port_display_cfg_t disp_cfg{};
    disp_cfg.io_handle = handles.io;
    disp_cfg.panel_handle = handles.panel;
    disp_cfg.buffer_size = frame_px;
    disp_cfg.double_buffer = true;
    disp_cfg.trans_size = 0;
    disp_cfg.hres = pins::kLcdHRes;
    disp_cfg.vres = pins::kLcdVRes;
    disp_cfg.monochrome = false;
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.buff_dma = false;
    disp_cfg.flags.buff_spiram = true;
    disp_cfg.flags.sw_rotate = false;
    disp_cfg.flags.swap_bytes = false; /* bounce_flush_cb endian-swaps for the panel */
    disp_cfg.flags.full_refresh = true;
    disp_cfg.flags.direct_mode = false;

    handles.lv_disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(handles.lv_disp, ESP_FAIL, TAG, "add_disp");

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = bounce_trans_done,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_register_event_callbacks(handles.io, &cbs, s_bounce), TAG, "io cbs");
    lv_display_set_flush_cb(handles.lv_disp, bounce_flush_cb);

    ESP_RETURN_ON_ERROR(backlight_on(), TAG, "backlight");
    *out = handles;

    ESP_LOGI(TAG, "display ready %ux%u landscape (90 CW) PSRAM+bounce TE", pins::kLcdHRes,
             pins::kLcdVRes);
    return ESP_OK;
}

}  // namespace board::display
