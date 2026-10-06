#include "ui/status_ui.hpp"

#include "ui/fonts.hpp"

namespace ui {
namespace {

lv_obj_t *s_detail = nullptr;

}  // namespace

void create_status(const char *title, const char *detail) {
    init_fonts();

    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_pad_all(scr, 16, 0);

    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, title ? title : "");
    lv_obj_set_style_text_color(t, lv_color_hex(0xE8EEF2), 0);
    lv_obj_set_style_text_font(t, font_20(), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 8);

    s_detail = lv_label_create(scr);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_detail, 448);
    lv_label_set_text(s_detail, detail ? detail : "");
    lv_obj_set_style_text_color(s_detail, lv_color_hex(0x8AA0B0), 0);
    lv_obj_set_style_text_font(s_detail, font_14(), 0);
    lv_obj_align(s_detail, LV_ALIGN_TOP_LEFT, 0, 48);

    lv_screen_load(scr);
}

void set_status_detail(const char *detail) {
    if (s_detail && detail) {
        lv_label_set_text(s_detail, detail);
    }
}

}  // namespace ui
