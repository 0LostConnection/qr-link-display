#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_types.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

namespace board::display {

struct Handles {
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;
    lv_display_t *lv_disp = nullptr;
};

/** Init panel, TE sync, backlight, LVGL, and bounce-buffer flush. */
esp_err_t start(Handles *out);

esp_err_t set_brightness(int percent);
/** Current backlight percent (0–100). */
int get_brightness();
/** Boot / “on” brightness — change this to set the default. */
constexpr int kDefaultBrightness = 30;
esp_err_t backlight_on();
esp_err_t backlight_off();

bool lock(uint32_t timeout_ms);
void unlock();

}  // namespace board::display
