#include "board/touch.hpp"

#include "board/panel_hw.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

namespace board::touch {
namespace {

constexpr char TAG[] = "board.touch";

}  // namespace

esp_err_t start(lv_display_t *disp, Handles *out) {
    ESP_RETURN_ON_FALSE(disp && out, ESP_ERR_INVALID_ARG, TAG, "null args");

    esp_lcd_touch_handle_t tp = nullptr;
    ESP_RETURN_ON_ERROR(board_touch_create(&tp), TAG, "touch hw");

    lvgl_port_touch_cfg_t touch_cfg{};
    touch_cfg.disp = disp;
    touch_cfg.handle = tp;

    lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg);
    ESP_RETURN_ON_FALSE(indev, ESP_FAIL, TAG, "add_touch");

    out->tp = tp;
    out->indev = indev;
    ESP_LOGI(TAG, "touch ready");
    return ESP_OK;
}

}  // namespace board::touch
