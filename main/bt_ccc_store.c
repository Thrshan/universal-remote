#include "bt_ccc_store.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t peer[6], peer_type, reserved;
    esp_hidd_ccc_t ccc;
} ccc_record_t;

static void record_key(const uint8_t *local, char key[15])
{
    snprintf(key, 15, "c%02x%02x%02x%02x%02x%02x", local[0], local[1], local[2], local[3], local[4], local[5]);
}
esp_err_t bt_ccc_load(const uint8_t local[6], const uint8_t peer[6], uint8_t peer_type, esp_hidd_ccc_t *ccc)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ur_hid", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) return err;
    char key[15]; record_key(local, key);
    ccc_record_t record = {0};
    size_t size = sizeof(record);
    err = nvs_get_blob(handle, key, &record, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) return err;
    if (size != sizeof(record) || record.reserved || record.ccc.version != 1 ||
        record.ccc.count > ESP_HIDD_CCC_MAX) return ESP_ERR_INVALID_SIZE;
    if (record.peer_type != peer_type || memcmp(record.peer, peer, 6)) return ESP_ERR_NOT_FOUND;
    *ccc = record.ccc;
    return ESP_OK;
}
esp_err_t bt_ccc_save(const uint8_t local[6], const uint8_t peer[6], uint8_t peer_type, const esp_hidd_ccc_t *ccc)
{
    ccc_record_t record = {.peer_type = peer_type, .ccc = *ccc};
    memcpy(record.peer, peer, 6);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ur_hid", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    char key[15]; record_key(local, key);
    err = nvs_set_blob(handle, key, &record, sizeof(record));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
esp_err_t bt_ccc_forget(const uint8_t local[6])
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ur_hid", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    char key[15]; record_key(local, key);
    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
