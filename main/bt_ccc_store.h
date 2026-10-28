#pragma once
#include "esp_hidd_ccc.h"

// local is the stable profile address; peer is its canonical bonded identity.
esp_err_t bt_ccc_load(const uint8_t local[6], const uint8_t peer[6], uint8_t peer_type, esp_hidd_ccc_t *ccc);
esp_err_t bt_ccc_save(const uint8_t local[6], const uint8_t peer[6], uint8_t peer_type, const esp_hidd_ccc_t *ccc);
esp_err_t bt_ccc_forget(const uint8_t local[6]);
