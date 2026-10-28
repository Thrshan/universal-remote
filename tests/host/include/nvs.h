#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef unsigned nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef struct {
    size_t used_entries, free_entries, available_entries, total_entries, namespace_count;
} nvs_stats_t;
esp_err_t nvs_get_stats(const char *part_name, nvs_stats_t *stats);
esp_err_t nvs_get_used_entry_count(nvs_handle_t handle, size_t *entries);
esp_err_t nvs_open(const char *name, unsigned mode, nvs_handle_t *handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *blob, size_t *size);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *blob, size_t size);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
void nvs_close(nvs_handle_t handle);
