#pragma once

#include <cstddef>

namespace ui {

/** Create landscape QR screen (call under display lock). */
void create_qr_screen();

/** Update displayed QR payload. Empty string shows setup hint. */
void set_qr_link(const char *url);

/** Copy current link into out (NUL-terminated). */
void get_qr_link(char *out, size_t out_len);

/** Open / close settings overlay. */
void show_settings();
void hide_settings();
bool settings_open();

}  // namespace ui
