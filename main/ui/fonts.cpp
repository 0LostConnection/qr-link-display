#include "ui/fonts.hpp"

namespace ui {
namespace {

lv_font_t s_font14{};
lv_font_t s_font20{};
bool s_ready = false;

}  // namespace

void init_fonts() {
    if (s_ready) {
        return;
    }
    /* Copy font descriptors into RAM. The LVGL-provided Montserrat objects live in
     * flash (.rodata); writing .fallback there → "Cache disabled but cached memory
     * region accessed" / boot loop on ESP32-S3. */
    s_font14 = lv_font_montserrat_14;
    s_font20 = lv_font_montserrat_20;
    s_font14.fallback = &lv_font_source_han_sans_sc_16_cjk;
    s_font20.fallback = &lv_font_source_han_sans_sc_16_cjk;
    s_ready = true;
}

const lv_font_t *font_14() {
    init_fonts();
    return &s_font14;
}

const lv_font_t *font_20() {
    init_fonts();
    return &s_font20;
}

}  // namespace ui
