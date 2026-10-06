#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

namespace board::touch {

struct Handles {
    esp_lcd_touch_handle_t tp = nullptr;
    lv_indev_t *indev = nullptr;
};

/** Init I2C + AXS15231B touch and register it with LVGL. */
esp_err_t start(lv_display_t *disp, Handles *out);

}  // namespace board::touch
