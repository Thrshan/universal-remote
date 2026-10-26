#include "bt_advertising.h"
#include "esp_gap_ble_api.h"

esp_err_t bt_configure_advertising(uint16_t appearance)
{
    // Bluedroid's structured advertising API accepts UUIDs in 128-bit form
    // only. It compresses this Bluetooth-base UUID to 16-bit 0x1812 on air.
    static uint8_t service_uuid[] = {
        0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
    };
    esp_ble_adv_data_t advertising = {
        .appearance = appearance,
        .service_uuid_len = sizeof(service_uuid), .p_service_uuid = service_uuid,
        .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
    };
    // Keep the complete name in the scan response so the legacy advertising
    // packet has room for flags, keyboard appearance, and the HID service.
    esp_ble_adv_data_t scan = {.set_scan_rsp = true, .include_name = true};
    esp_err_t err = esp_ble_gap_config_adv_data(&advertising);
    if (err != ESP_OK) return err;
    return esp_ble_gap_config_adv_data(&scan);
}
