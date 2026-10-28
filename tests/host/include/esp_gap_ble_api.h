#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define ESP_BLE_ADV_FLAG_GEN_DISC 0x02
#define ESP_BLE_ADV_FLAG_BREDR_NOT_SPT 0x04
typedef struct {
    bool set_scan_rsp;
    bool include_name;
    uint16_t appearance;
    uint16_t service_uuid_len;
    uint8_t *p_service_uuid;
    uint8_t flag;
} esp_ble_adv_data_t;
esp_err_t esp_ble_gap_config_adv_data(esp_ble_adv_data_t *data);
