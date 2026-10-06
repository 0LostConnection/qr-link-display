#pragma once

#include "lvgl.h"

namespace ui {

/** Montserrat 14/20 in RAM with Source Han CJK fallback. Call once after LVGL init. */
void init_fonts();

const lv_font_t *font_14();
const lv_font_t *font_20();

}  // namespace ui
