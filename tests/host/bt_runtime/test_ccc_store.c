#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "bt_ccc_store.h"

static struct { char key[16]; unsigned char bytes[128]; size_t size; } entries[3];
static unsigned checks, commits;
static esp_err_t write_error, commit_error;
#define CHECK(v) do { ++checks; assert(v); } while (0)
esp_err_t nvs_open(const char *name, unsigned mode, nvs_handle_t *handle)
{ CHECK(strcmp(name, "ur_hid") == 0); *handle = 1; return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *blob, size_t *size)
{
    for (unsigned i = 0; i < 3; ++i) if (entries[i].size && !strcmp(entries[i].key, key)) {
        if (*size < entries[i].size) return ESP_ERR_INVALID_SIZE;
        memcpy(blob, entries[i].bytes, entries[i].size); *size = entries[i].size; return ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *blob, size_t size)
{
    if (write_error) return write_error;
    CHECK(strlen(key) < 16 && size < 128);
    for (unsigned i = 0; i < 3; ++i) if (!entries[i].size || !strcmp(entries[i].key, key)) {
        strcpy(entries[i].key, key); memcpy(entries[i].bytes, blob, size); entries[i].size = size; return ESP_OK;
    }
    return ESP_FAIL;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    for (unsigned i = 0; i < 3; ++i) if (!strcmp(entries[i].key, key) && entries[i].size) {
        entries[i].size = 0; return ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_commit(nvs_handle_t h) { ++commits; return commit_error; }
void nvs_close(nvs_handle_t h) {}
int main(void)
{
    uint8_t a[6] = {0xc1,2,3,4,5,6}, b[6] = {0xc1,2,3,4,5,7}, peer[6] = {1,2,3,4,5,6};
    esp_hidd_ccc_t ccc = {.version=1,.count=2,.reports={{0,1,1,1},{0,2,1,0}}}, loaded;
    CHECK(bt_ccc_load(a, peer, 0, &loaded) == ESP_ERR_NOT_FOUND);
    CHECK(bt_ccc_save(a, peer, 0, &ccc) == ESP_OK && commits == 1);
    CHECK(bt_ccc_load(a, peer, 0, &loaded) == ESP_OK && !memcmp(&ccc, &loaded, sizeof(ccc)));
    CHECK(bt_ccc_load(b, peer, 0, &loaded) == ESP_ERR_NOT_FOUND);
    CHECK(bt_ccc_load(a, peer, 1, &loaded) == ESP_ERR_NOT_FOUND);
    peer[5] ^= 1; CHECK(bt_ccc_load(a, peer, 0, &loaded) == ESP_ERR_NOT_FOUND); peer[5] ^= 1;
    ccc.reports[0].value = 0;
    CHECK(bt_ccc_save(b, peer, 0, &ccc) == ESP_OK);
    CHECK(bt_ccc_forget(a) == ESP_OK);
    CHECK(bt_ccc_load(a, peer, 0, &loaded) == ESP_ERR_NOT_FOUND);
    CHECK(bt_ccc_load(b, peer, 0, &loaded) == ESP_OK && loaded.reports[0].value == 0);
    CHECK(bt_ccc_forget(a) == ESP_OK); // Idempotent unpair.
    entries[1].bytes[8] = 99; // Unsupported snapshot version.
    CHECK(bt_ccc_load(b, peer, 0, &loaded) == ESP_ERR_INVALID_SIZE);
    write_error = ESP_FAIL;
    CHECK(bt_ccc_save(a, peer, 0, &ccc) == ESP_FAIL);
    write_error = 0; commit_error = ESP_FAIL;
    CHECK(bt_ccc_save(a, peer, 0, &ccc) == ESP_FAIL);
    printf("PASS: %u bonded CCC storage checks.\n", checks);
}

