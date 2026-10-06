#pragma once

#include "esp_lcd_axs15231b.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const axs15231b_lcd_init_cmd_t board_lcd_init_cmds[];
extern const size_t board_lcd_init_cmds_size;

#ifdef __cplusplus
}
#endif
