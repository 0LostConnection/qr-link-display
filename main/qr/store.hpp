#pragma once

#include "esp_err.h"

#include <cstddef>

namespace qr {

/** Max URL length stored in NVS (including NUL). */
constexpr size_t kMaxUrlLen = 512;

/** Load saved link from NVS. Returns ESP_ERR_NOT_FOUND if unset. */
esp_err_t load_url(char *out, size_t out_len);

/** Persist link to NVS. Empty string clears. */
esp_err_t save_url(const char *url);

/** Erase stored link. */
esp_err_t clear_url();

}  // namespace qr
