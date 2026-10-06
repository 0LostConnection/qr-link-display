#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t board_panel_create(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel);
esp_err_t board_touch_create(esp_lcd_touch_handle_t *tp);

#ifdef __cplusplus
}
#endif
