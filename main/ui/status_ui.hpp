#pragma once

#include "lvgl.h"

namespace ui {

/** Landscape (480×320) status / setup screen. */
void create_status(const char *title, const char *detail);

/** Update the detail line (call under display lock). */
void set_status_detail(const char *detail);

}  // namespace ui
