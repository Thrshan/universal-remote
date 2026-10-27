/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_hidd.h"

// Application-owned persistence extension to ESP-IDF 5.5.2 esp_hid.
// The tuple identifies a descriptor without persisting mutable GATT handles.
#define ESP_HIDD_CCC_MAX 16
typedef struct {
    uint8_t map_index, report_id, protocol_mode, value;
} esp_hidd_ccc_report_t;
typedef struct {
    uint8_t version, battery, count, reserved;
    esp_hidd_ccc_report_t reports[ESP_HIDD_CCC_MAX];
} esp_hidd_ccc_t;

esp_err_t esp_hidd_ble_get_ccc(esp_hidd_dev_t *dev, esp_hidd_ccc_t *ccc);
// Only before advertising/connection, after validating the selected bond.
// Rejects a different descriptor layout or unsupported CCC bits.
esp_err_t esp_hidd_ble_restore_ccc(esp_hidd_dev_t *dev, const esp_hidd_ccc_t *ccc);
